# Paged C++ query engine: comparing query plans (CSE 510, Fall 2026)

Surya Chaitanya Buse and Rimpa Nandi.

A small paged query engine (C++17, no external DBMS) implementing four
join algorithms (block nested loop, index nested loop, sort-merge,
Grace hash), a buffer pool with exact page-I/O counting, a paged B+
tree, group-by/order-by, and a System-R-style cost estimator — built to
compare measured query time and page I/O against analytical cost
formulas across equivalent plans.

This README is everything needed to reproduce every number in the
report from a fresh clone: no other steps, no absolute paths, fixed
seeds throughout.

## Requirements

- C++17 compiler (Apple clang / g++), CMake >= 3.16
- Python 3 with `matplotlib` (stdlib `sqlite3`, `csv`, `argparse` — no
  other third-party packages)
- Run `./scripts/install_deps.sh` to check for all of the above and
  install `matplotlib` if it's missing.

Tested with: cmake 4.3.0, Apple clang 17.0.0, Python 3.12.5,
matplotlib 3.9.2, on macOS (Apple M2, 8 GB RAM) — see "Hardware and
settings" below.

## Build and test

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

16 unit test executables, all plain-assert (no GoogleTest). Expected:
`100% tests passed, 0 tests failed out of 16`, well under 5 seconds.

## Reproduce everything with one command

```
./scripts/run_all.sh
```

This builds (Release), runs the unit tests, then runs every config in
`configs/` in order (`smoke`, `qa_methods`, `e1`..`e6`): generates data
deterministically, builds indices, runs every plan, checks every
result against a SQLite oracle, writes `results/*.csv`, and produces
`figures/*.png`. It exits non-zero if any oracle check fails.

**Expected runtime: ~25-35 seconds** from a completely clean
`build/`/`data/`/`results/`/`figures/` state (measured on the hardware
below; CPU-bound, not disk-bound, so this should not vary much across
reasonably modern laptops).

To check just the pipeline itself quickly (well under a minute), run
`python3 scripts/run_experiments.py configs/smoke.json` directly, or
point `run_all.sh` at a directory containing only `smoke.json` (e.g.
`mkdir /tmp/smoke_only && cp configs/smoke.json /tmp/smoke_only/ &&
./scripts/run_all.sh /tmp/smoke_only`).

## Reproduce one experiment

```
python3 scripts/run_experiments.py configs/e3.json
python3 scripts/make_plots.py results/e3.csv --x-label "Buffer pages (B)"
```

`run_experiments.py` writes `results/<experiment>.csv` and calls
`make_plots.py` itself, so the second command above is usually
redundant — it's shown because `make_plots.py` also works standalone
on any already-produced CSV (e.g. to regenerate a figure with a
different title).

## Using the tools directly

```
# Generate data (deterministic: same seed + params => byte-identical files)
build/src/gen_data --out-dir data/demo --scale 0.1 --seed 7

# QA (2-table join): --method bnlj|smj|ghj|inlj
build/src/run_query --query qa --data-dir data/demo --method smj \
    --buffer-pages 15 --rmin 4 --dmin 100 --explain on

# QB (3-table join): --join-order order1|order2, --method1/--method2,
# --pushdown on|off
build/src/run_query --query qb --data-dir data/demo \
    --join-order order1 --method1 bnlj --method2 smj --pushdown on \
    --buffer-pages 15 --rmin 4 --amax 60 --color 1 --d1 50 --d2 300

# QC (QB + GROUP BY rating HAVING count > h) and QD (QB + ORDER BY sid)
# take the same flags as QB, plus --h for QC.
build/src/run_query --query qc --data-dir data/demo --h 5 \
    --join-order order1 --method1 bnlj --method2 bnlj --pushdown on \
    --buffer-pages 15 --rmin 4 --amax 60 --color 1 --d1 50 --d2 300

# Compare one run against the SQLite oracle directly:
python3 scripts/oracle_check.py --data-dir data/demo --method smj \
    --buffer-pages 15 --rmin 4 --dmin 100
python3 scripts/check_qb_plans.py --data-dir data/demo   # sweeps 64 QB plans
python3 scripts/check_qcd_plans.py --data-dir data/demo  # sweeps 64 QC + 64 QD plans
```

`gen_data --help`-style flags (there is no `--help`; these are all of
them): `--seed`, `--scale`, `--key-dist uniform|zipf`, `--zipf-theta`,
`--corr`, `--order random|sorted`, `--cluster-by sid|bid`,
`--sailors-tuple-bytes`, `--reserves-tuple-bytes`, `--boats-tuple-bytes`,
`--out-dir`.

## Experiments

| ID | Question | Varies | Fixed | Config | Figure |
|---|---|---|---|---|---|
| E1 | Cost spread across table sizes | `scale` (0.02-0.4) | QA, B=15 | `configs/e1.json` | `figures/e1.png` |
| E2 | When does pushdown help most? | `rmin` (selectivity) x pushdown on/off | QB, scale=0.1, B=15 | `configs/e2.json` | `figures/e2.png` |
| E3 | Join method vs. buffer size | `buffer_pages` (5-80) x method | QB, scale=0.1 | `configs/e3.json` | `figures/e3.png` |
| E4 | Does skew hurt hash more than sort-merge? | `zipf_theta` (0-2) | QA, smj vs. ghj | `configs/e4.json` | `figures/e4.png` |
| E5 | Is the cost model's best plan the measured best? | `corr` (0-1) | QB, all 4 methods | `configs/e5.json` | `figures/e5.png` |
| E6 | How much do indexes/clustering matter? | index vs. none, clustered vs. unclustered | QA | `configs/e6.json` | `figures/e6.png` |

Each run prints a `Measured/estimated I/O ratio` line and (where more
than one plan shares a sweep point) an `Estimator's top plan ==
measured fastest: N/M points` line — these are also in each CSV's
`est_io`/`est_out_rows` columns, computed by `run_query --explain`
(`src/plan/cost_estimator.{h,cpp}`).

Scope decisions for these six configs: E1 sweeps QA only, not all four
queries; E2/E3/E5 fix `method1 == method2` (the same join algorithm for
both of QB's joins) rather than all 16 method-pair combinations; E6's
varying "factor" is a categorical scenario (not a continuous sweep).
These keep each experiment's runtime small while still answering its
question; none of them are a correctness limitation —
`scripts/check_qb_plans.py` and `scripts/check_qcd_plans.py` separately
verify all 64 (order x method-pair x pushdown) QB plans and all 64
QC/QD plans against the SQLite oracle.

## Hardware and settings used for the reported numbers

- CPU: Apple M2 (arm64), RAM: 8 GB
- OS: macOS (Darwin 27.0.0)
- Compiler: Apple clang 17.0.0, `-O2`, CMake build type `Release`
- Python: 3.12.5, matplotlib 3.9.2
- Page size: 4096 bytes (compile-time constant, `src/common/types.h`)
- Base dataset scale (scale=1.0): Sailors 40,000 rows (~494 pages),
  Reserves 100,000 rows (~2,500 pages), Boats 100 rows (~2 pages).
  Experiments E1-E6 use smaller scales (0.02-0.4) to keep total runtime
  short; `configs/qa_methods.json` and ad hoc `run_query` runs were also
  verified to match the SQLite oracle at full base scale (scale=1.0).
- Timing caveat: wall-clock time reflects the OS page cache plus CPU
  cost, not raw disk latency — page I/O counts are exact and are the
  primary metric for comparing plans.

## Repository layout

- `src/` — the engine (storage, heap files, indexes, operators, joins,
  plan builder, cost estimator, CLI tools)
- `tests/` — one plain-assert test executable per module
- `scripts/` — the Python pipeline (data generation driver, oracle
  checks, experiment runner, plotting, `run_all.sh`)
- `configs/` — experiment JSON (`smoke`, `qa_methods`, `e1`..`e6`)
- `data/` — generated data (gitignored, regenerated on demand)
- `results/` — experiment CSVs (tracked)
- `figures/` — experiment graphs (tracked)

## AI usage

This project used Claude Code throughout implementation. AI was not
used to write report or proposal prose, per course policy.
