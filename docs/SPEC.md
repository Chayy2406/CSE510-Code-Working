# SPEC: paged C++ query engine and plan-comparison study

Source of truth: `docs/reference/proposal_revised.pdf` and `docs/reference/final_presentation.pdf`.
This file turns them into concrete decisions. Items marked **[ASSUMPTION]** were not fixed by the proposal; they are sensible defaults and can be changed (record changes in `docs/DECISIONS.md`).

## 1. What we are doing

Build a small query engine in C++17 from scratch (no existing DBMS), with fixed-size pages, an LRU buffer pool that counts every page read and write, iterator-style operators, a paged B+ tree, and four join algorithms. Run multi-predicate queries under many equivalent plans, changing one factor at a time, and compare **query time** (main metric) and **page I/O** (secondary) against analytical cost formulas. This is an empirical study with an engineering component. We are not proposing a new algorithm.

Success criteria (from the proposal):
- All plans return identical results on every test (checked against an oracle).
- Measured I/O is within 25% of the formula, or the deviation is explained by measured cardinalities or skew.
- The cost estimator's top-ranked plan is compared with the measured fastest plan, and the measured/estimated ratio is reported.

## 2. Repository layout (keep this)

```
CMakeLists.txt
src/
  common/      types.h (PageId, Rid, constants), config, stats counters, timer
  storage/     disk_manager.{h,cpp}, page.h, buffer_pool.{h,cpp}
  heap/        schema.h, heap_file.{h,cpp}   (fixed-width tuples)
  index/       bplus_tree.{h,cpp}
  catalog/     catalog.{h,cpp}, statistics
  exec/        operator.h (Open/Next/Close), seq_scan, index_scan, selection,
               projection, external_sort, bnlj, inlj, smj, ghj, group_by, sink
  plan/        plan_node.h, plan_builder.{h,cpp}, cost_estimator.{h,cpp}
  gen/         data_generator.{h,cpp}
  tools/       gen_data.cpp, run_query.cpp (CLI), ref_executor.cpp (tiny in-memory reference)
tests/         one test executable per module + test_util.h
scripts/       run_experiments.py, oracle_check.py, make_plots.py, run_all.sh, requirements.txt
configs/       e1.json ... e6.json, smoke.json
data/          generated (gitignored)
results/       CSV output (tracked at the end for the report)
figures/       PNG graphs
docs/          SPEC.md, PLAN.md, DECISIONS.md, AI_USAGE.md, reference/
```

## 3. Storage and buffer pool

- **Page size 4096 bytes [ASSUMPTION]**, configurable at compile time via one constant.
- `DiskManager`: one file per table / index / temp file, accessed with `pread`/`pwrite`. API: `ReadPage(file, page_no, buf)`, `WritePage(file, page_no, buf)`, `AllocatePage(file)`. **Every actual call to ReadPage/WritePage increments the global counters** `page_reads` / `page_writes`. Nothing else may touch the files.
- `BufferPool(B frames)`: LRU replacement, pin counts, dirty bits. API: `Fetch(file, page_no)` (pins, may miss and read), `NewPage(file)`, `Unpin(page, dirty)`, `FlushAll()`, `Reset()` (empties the pool, used to start each run cold). A miss = one counted read. Evicting a dirty frame = one counted write. A hit is free but counted separately as `buffer_hits`. If every frame is pinned and a new page is requested, throw a clear `BufferFullError`.
- Counters also split by file kind (base table, index, temp) so experiments can show where I/O comes from. Also count `tuples_output`.
- **Timing caveat to document:** the OS page cache will absorb real disk latency, so wall-clock time here reflects CPU plus buffer-pool behavior more than raw disk speed. Page I/O counts are exact. Record this in the report's experiment-setup text. (Optional later: `posix_fadvise` / `O_DIRECT` experiments; not required.)

## 4. Tuples and heap files

- Fixed-width tuples, all columns `int32`, plus a configurable zero-padding so tuple width is a parameter. Page layout: small header (tuple count) then tuples packed back-to-back. `tuples_per_page = (4096 - header) / tuple_bytes`.
- `HeapFile`: append, sequential scan by page, fetch by `Rid {page_no, slot}`. Pages are always read through the buffer pool.
- Temp files for sort runs, hash partitions and materialized intermediate results are heap files too, so their I/O is counted the same way.

## 5. Schema and data generator

Schema (Sailors / Reserves / Boats, the lecture schema, simplified to ints):

| Table | Columns | Default tuple bytes [ASSUMPTION; verify against lecture 12/13 slides] |
|---|---|---|
| Sailors S | `sid` (unique), `rating` (1..10), `age` (16..70) | 50 |
| Reserves R | `sid`, `bid`, `day` (1..365) | 100 |
| Boats B | `bid` (unique), `color` (0..K-1, e.g. 0=red,1=green,2=blue,3=yellow...) | 50 |

Base scale (adjustable by a `scale` factor): S = 40,000 tuples, R = 100,000 tuples, B = 100 tuples. With 4096-byte pages that gives roughly S ~ 500 pages, R ~ 2500 pages. If the lecture slides use other numbers (for example S = 500 pages, R = 1000 pages with a 5-page buffer), add a `lecture` preset that reproduces them exactly, since reproducing slide numbers is a good correctness check.

Generator parameters (all seeded; same seed + params => byte-identical data):
- `--scale`, per-table row counts, `--tuple-bytes` per table.
- `--key-dist uniform|zipf` and `--zipf-theta` for `R.sid` (and optionally `R.bid`): uniform is the default; Zipf makes a few sailors/boats very popular.
- `--corr c` in [0,1]: makes `S.rating` correlated with `S.age` (and optionally `B.color` with `bid`), so independent-predicate estimates are wrong. `c=0` means independent.
- `--order random|sorted` and `--cluster-by <col>`: physical order of the stored file (sorted by join key = clustered).
- The generator also writes a CSV copy of every table (`data/<name>/S.csv`, etc.) for the SQLite oracle.
- Statistics computed at load time into the catalog: tuple count, page count, distinct count per column, min/max, index height. The cost estimator must use these (not the data itself).

## 6. Operators

Iterator interface: `Open()`, `bool Next(Tuple&)`, `Close()`. Each blocking operator is given a frame budget `B_op` and may only hold that many frames pinned or in private memory (hash tables and sort buffers count against it).

| Operator | Notes |
|---|---|
| `SeqScan` | with an optional pushed-down predicate list |
| `IndexScan` | B+ tree range or equality lookup; unclustered = one data-page fetch per RID unless the file is sorted by the key (clustered) |
| `Selection`, `Projection` | pipelined |
| `ExternalSort` | run formation with B_op frames, then (B_op - 1)-way merge passes; supports sorting a heap file or an operator's output; ties are stable |
| `BNLJ` | block = B_op - 2 pages of outer, one page for inner, one for output; inner rescanned per block |
| `INLJ` | for each outer tuple probe the inner's B+ tree; fetch the matching tuples |
| `SMJ` | sort both inputs (skipping a sort if the input is already ordered on the key), merge; the final merge pass is fused with the join; correct for many-to-many duplicate keys (buffer the duplicate group, which must fit the budget or fall back to rescanning) |
| `GHJ` | partition both inputs into up to B_op - 1 partitions with hash h1 (spill via buffer pool), then per partition build an in-memory hash table (h2) on the smaller side and probe with the other. If a build partition exceeds B_op - 2 pages, **recursively partition** with a new hash seed, up to a depth limit; if still too big (heavy skew on one key), fall back to block nested loop for that partition and record `ghj_overflow_partitions` in the stats. |
| `GroupBy` | sort-based via ExternalSort (preserves an interesting order), or in-memory hash aggregate when the group count is small (QC groups on `rating`, 10 groups); HAVING as a filter after aggregation |
| `Sink` | counts rows and computes an order-insensitive checksum; optionally writes the result to CSV; does **not** write pages (output cost is excluded from join formulas) |

**Multi-join plans (QB..QD):** every join except the last materializes its output to a temp heap file (counted I/O), so only one join is active at a time and each gets the full buffer budget B. The last join pipelines into the sink, group-by, or final sort. Record this in `docs/DECISIONS.md`.

## 7. Queries

Schema aliases: S = Sailors, R = Reserves, B = Boats. Constants are parameters (set by config) so selectivities can be varied.

- **QA** (2 tables, 2 predicates): `SELECT S.sid, S.rating, R.bid, R.day FROM Sailors S, Reserves R WHERE S.sid = R.sid AND S.rating > :rmin AND R.day >= :dmin`
- **QB** (3 tables, 4 predicates): `SELECT S.sid, R.day, B.color FROM Sailors S, Reserves R, Boats B WHERE S.sid = R.sid AND R.bid = B.bid AND S.rating > :rmin AND S.age < :amax AND B.color = :color AND R.day BETWEEN :d1 AND :d2`
- **QC** (QB + group-by): `SELECT S.rating, COUNT(*) FROM <QB joins and predicates> GROUP BY S.rating HAVING COUNT(*) > :h`
- **QD** (QB + order-by): `... ORDER BY S.sid` (an interesting order: a sort-merge join on `sid` can deliver it for free).

The proposal and slides refer to the multi-predicate queries from lectures 12 and 13. If those slides use different exact predicates or group/order columns, adopt them and note it in DECISIONS.md.

## 8. Plan space, plan builder, cost estimator

**Plan space** (enumerated from config, not a full optimizer):
- selection **pushdown**: on (selections at the scans) or off (selections after the joins)
- **join order**: left-deep only, no cross products (QB has two orders: (S,R,B) and (R,B,S) joined as ((R⋈B)⋈S); QA has two orders for which table is outer)
- **join method** per join: BNLJ, INLJ, SMJ, GHJ (INLJ only where the inner has an index)
- **access path**: sequential scan vs. B+ tree index scan for selective predicates

A plan is a tree of `PlanNode`s; the builder turns a config entry into an executable operator tree. `run_query` prints the plan as text.

**Cost estimator** (System R style, from the lecture and Selinger et al. [5] in the proposal):
- Reduction factors: `col = v` -> 1/NKeys(col); `col > v` -> (high - v)/(high - low); `a AND b` -> product (independence); join size = |R|·|S| / max(NKeys(R.k), NKeys(S.k)).
- I/O formulas (M = pages of outer R, N = pages of inner S, B = frames for the operator):
  - BNLJ: `M + ceil(M/(B-2)) * N`
  - INLJ: `M + |R| * c`, where c = cost of one index lookup (index height + data page fetches, depends on clustered vs. unclustered)
  - SMJ: sorting a table of P pages with F = B-1 merge fan-in: run formation reads and writes P; each extra merge pass beyond the final reads and writes P; the final merge is fused with the join and reads P. When one merge pass suffices (about B >= sqrt(P)) the total is `3(M+N)`. Implement the general form and check that it reduces to `3(M+N)`.
  - GHJ: `3(M+N)` when partitions fit (about B > sqrt(f·min(M,N)), f a fudge factor of about 1.2); add `2(M+N)` per extra recursive partitioning level.
  - Intermediate result between joins: pages written plus pages read, using the estimated size.
- Output of the estimator per plan: predicted cardinalities, predicted I/O (reads + writes), predicted rank. The experiment framework joins this with measured values and reports the **measured/estimated I/O ratio** and whether the estimator's top plan equals the measured fastest.

## 9. Correctness: oracle and reference executor

- **Oracle (required):** `scripts/oracle_check.py` loads the generated CSVs into an in-memory SQLite DB, runs the matching SQL for QA..QD with the same constants, and compares against the engine's result (row count + order-insensitive checksum; for QD also verify ordering by `S.sid`). Every plan on every test config must match.
- **Reference executor:** a trivial all-in-memory nested-loop executor in `tools/ref_executor.cpp`, used only for unit tests on small data (for example to test each join operator directly with duplicate keys, empty inputs, one-page inputs, and tiny buffers).
- **Unit tests** to include: buffer pool (hit/miss/eviction/dirty write counts), heap file round trip, B+ tree (insert, range, duplicates), external sort (sizes that need 0, 1, 2 merge passes), each join vs. the reference on random data with several B values, GHJ with a skewed key that forces recursion/fallback.

## 10. Metrics and CSV output

One row per (experiment, dataset config, query, plan, buffer size, repeat) in `results/<experiment>.csv` with at least:
`experiment, query, plan_id, plan_desc, dataset_id, seed, scale, key_dist, zipf_theta, corr, order, B, run, time_ms, page_reads, page_writes, reads_base, reads_index, reads_temp, writes_temp, buffer_hits, tuples_out, checksum, est_io, est_out_rows, ghj_overflow_partitions, oracle_ok`.

Timing: `std::chrono::steady_clock`. Each measured run starts with `BufferPool::Reset()` and counters zeroed (cold buffer pool). One warm-up run is discarded, then **5 runs; report the median** (min and max also recorded). I/O counts are deterministic and should be identical across repeats; assert that.

## 11. Experiments (six sets, all in `configs/`)

| ID | Question | Vary | Fixed |
|---|---|---|---|
| E1 | How big is the cost spread between plans across table sizes? | table size (scale) for QA..QD | B, uniform keys |
| E2 | When does pushdown help most? | selectivity of predicates (constants), pushdown on/off | scale, B |
| E3 | Join method vs. buffer size on three-table queries | B from just above the minimum to large, method per join | QB, scale |
| E4 | Does skew hurt hash join more than sort-merge? | `zipf_theta` (0 = uniform up to heavy skew) | QA/QB, B |
| E5 | Is the cost model's best plan the measured best? | `corr` of correlated columns | QB, B |
| E6 | How much do indexes and input order matter? | index vs. none, clustered vs. unclustered, sorted vs. random input | QA, B |

Each produces one graph plus a table (via `scripts/make_plots.py`): x-axis the varied factor, one line per plan or method, y = time (and a second panel of page I/O). E1..E6 each also emit the estimated-vs-measured comparison.

## 12. The one-command pipeline

`./scripts/run_all.sh [config_dir]` (and `python3 scripts/run_experiments.py configs/e3.json` for one experiment) does: build (Release) -> generate data from the config -> build indexes and statistics -> run every plan -> oracle check -> write CSVs -> make graphs and tables. Non-zero exit if any oracle check fails. A `configs/smoke.json` runs everything on tiny data in under a minute and is used for testing the pipeline itself.

## 13. Out of scope

Full cost-based optimizer search, variable-length records, transactions/concurrency/recovery, SQL parsing, multi-threading, `O_DIRECT` tuning. Do not build these.

## 14. Open items to confirm with the students (do not block on them)

1. Exact lecture 12/13 query predicates and lecture schema sizes (preset `lecture`).
2. Whether the instructor expects raw disk I/O timing (we use file-backed pages plus the OS cache; documented caveat).
3. Final dataset sizes that keep the full experiment suite under about an hour on the students' laptops.
