# PLAN: build order and checkpoints

Dates (MST): presentation **Tue Nov 24**, report **Sun Nov 29 11:59 PM**, possible demo Dec 1, artifacts **Fri Dec 4 11:59 PM**.
Each phase ends with a **checkpoint**: build clean, tests pass, commit, show the output, stop. Do not start the next phase until told (unless told to continue through phase N).

Ownership column follows the proposal's workload split (Surya: storage, indexing, catalog, generator, nested loop joins; Rimpa: sort/hash joins, operators, plan builder, cost estimator). Both: integration, correctness, pipeline, analysis. Claude Code may build any phase; ownership only matters if the two students work in parallel on separate branches. Phases 1 and 2 block everything else, so do those first.

## Presentation slice (must be done for Nov 24): Phases 0 to 6

The deck's "Success Criteria and Showcase" slide has an empty table: `Plan | Time | Reads | Writes` for Block NLJ, Index NLJ, Sort Merge, Grace Hash. Filling that table with **real measured numbers from QA** (plus one buffer-size sweep, so there is a graph) is the target. If time runs out, the policy allows presenting unfinished work as long as it is declared. Cut order: first INLJ numbers (Phase 5), then the sweep. Never cut the oracle check.

| # | Phase | Owner | Done when (acceptance) |
|---|---|---|---|
| 0 | **Scaffold**: CMake (Release default), `tests/test_util.h`, CTest wiring, `.gitignore`, `scripts/requirements.txt`, git init, `docs/DECISIONS.md` seeded | both | `cmake` + `ctest` run a trivial test |
| 1 | **Storage**: `DiskManager`, `BufferPool` (LRU, pins, dirty, `Reset`, counters by file kind) | Surya | unit tests prove: reads on miss only, hit is free, dirty eviction = 1 write, pinned pages never evicted, `BufferFullError` when all pinned, counters match a hand-computed access trace |
| 2 | **Heap + schema + generator + catalog**: fixed-width tuples, `HeapFile` (append, scan, fetch by Rid), generator with uniform/zipf/corr/order, CSV export, stats in catalog, `gen_data` CLI | Surya | `gen_data` creates S, R, B at base scale deterministically (same seed => identical files); prints tuples/page counts; unit tests for round trip, determinism, zipf skew visible in key histogram |
| 3 | **Basic operators + first end-to-end**: `SeqScan`, `Selection`, `Projection`, `Sink`, `BNLJ`, `run_query` CLI (prints plan, rows, time, reads, writes, checksum), `ref_executor`, `scripts/oracle_check.py` | Rimpa/both | **QA via BNLJ matches SQLite** (count + checksum) on small and base-scale data; BNLJ I/O matches `M + ceil(M/(B-2))*N` within a few pages for 3 buffer sizes |
| 4 | **Sort and hash joins**: `ExternalSort` (0/1/2+ merge passes), `SMJ` (duplicates, fused final merge), `GHJ` (partitioning, recursion, BNLJ fallback on overflow) | Rimpa | each join equals the reference executor on random data with several B and with duplicate-heavy and skewed keys; **QA via SMJ and GHJ match SQLite**; measured I/O for SMJ and GHJ is close to `3(M+N)` when B is large enough |
| 5 | **B+ tree + INLJ + IndexScan**: paged B+ tree (bulk load or inserts, equality and range, duplicate keys, height in catalog), `IndexScan`, `INLJ` | Surya | B+ tree tests; **QA via INLJ matches SQLite**; INLJ I/O consistent with `M + |R|*c` for clustered vs. unclustered |
| 6 | **Minimal experiment runner + presentation numbers**: `scripts/run_experiments.py` (config -> gen -> run all plans -> oracle -> CSV), `configs/smoke.json`, `configs/qa_methods.json`; a script that prints the four-row table (Plan, Time, Reads, Writes) and one buffer-size sweep graph | both | the table has real numbers for the 4 joins on QA; sweep graph over at least 5 buffer sizes saved to `figures/`; oracle_ok = true on every row |

## After the presentation (needed for the report on Nov 29 and artifacts on Dec 4)

| # | Phase | Owner | Done when |
|---|---|---|---|
| 7 | **Three-table plans**: `PlanNode`, `plan_builder` driven by config (pushdown on/off, join order, join method per join, access path), temp-file materialization between joins | Rimpa | QB matches SQLite under at least 12 distinct plans, including all four join methods at least once |
| 8 | **Group-by, order-by, cost estimator**: `GroupBy` (QC), sorted output / interesting order (QD), `cost_estimator` (reduction factors, formulas from SPEC section 8) | Rimpa | QC and QD match SQLite; estimator prints predicted cardinalities, I/O, rank per plan; `run_query --explain` shows them |
| 9 | **Full pipeline and all six experiments**: `configs/e1..e6.json`, `run_all.sh`, `make_plots.py` (graph and table per experiment, estimated-vs-measured ratio, estimator-best vs. measured-best), 5 timed runs with median | both | `./scripts/run_all.sh` runs everything from scratch and exits non-zero on any oracle mismatch; all six figures and tables produced; total runtime documented |
| 10 | **Artifacts**: `README.md` with exact reproduction steps and expected runtime, `scripts/install_deps.sh` (if needed), final `DECISIONS.md` and `AI_USAGE.md`, a clean-clone test (clone to a fresh folder, follow the README, get the same I/O numbers) | both | clean-clone run reproduces the CSVs (I/O identical, times similar); zip contains code, scripts, configs, generator, README |

## What the report needs from the results (for the students; do not write report text)

By Nov 27 aim to have, for each of E1..E6: the CSV, the graph, a short results table, and the measured/estimated ratio. The students also need: hardware description (CPU, RAM, OS, compiler and flags), the dataset generation parameters, the buffer sizes used, and a list of every deviation from the proposal (these go in the report's optional announcement section, together with the acknowledgement of vibe coding).

## If we fall behind (cut in this order, record each cut in DECISIONS.md)

1. Reduce the number of sweep points and repeats in E1, E2, E6.
2. Drop clustered vs. unclustered as separate variants (keep sorted vs. random input).
3. Drop QD (ORDER BY) experiments; keep the code if it works.
4. Drop the recursive partitioning in GHJ (use the BNLJ fallback and report it as a finding under skew).
5. Never cut: oracle correctness, buffer-pool counters, QA and QB under all four joins, E3 (buffer size), E4 (skew).
