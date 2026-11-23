# Paged C++ query engine: comparing query plans (CSE 510, Fall 2026)

Surya Chaitanya Buse and Rimpa Nandi.

> Skeleton. Claude Code fills in the exact commands and expected runtimes in Phase 10 (artifacts). The instructor and TA must be able to reproduce every result in the technical report from this file alone.

## Requirements
- (to fill) C++17 compiler, CMake, Python 3, `matplotlib`

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Reproduce all results
(to fill) `./scripts/run_all.sh`: generates data, runs all plans, checks results against SQLite, writes `results/*.csv`, produces `figures/*.png`.

## Experiments
(to fill) E1..E6: what each varies, which config file, which figure it produces.

## Hardware and settings used for the reported numbers
(to fill)

## Repository layout
See `docs/SPEC.md` section 2.
