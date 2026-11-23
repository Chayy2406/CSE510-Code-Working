# CSE 510 project: paged C++ query engine, four join algorithms

Two-person group (Surya Chaitanya Buse, Rimpa Nandi). Course: CSE 510, Dr. Jin Wang, ASU, Fall 2026.
Read these before writing any code, in this order:

1. `docs/SPEC.md`: what we are building and every design decision already made.
2. `docs/PLAN.md`: the build order, with a checkpoint after each phase.
3. `docs/reference/`: the revised proposal, the final presentation, and the course project policy (PDFs).

## Deadlines that drive everything (all times MST)

- **Tue Nov 24**: final presentation (10 min). We need real measured numbers for it. See "Presentation slice" in `docs/PLAN.md`.
- **Sun Nov 29, 11:59 PM**: technical report (2 to 4 pages, IEEE template). It needs results from the experiments.
- **Dec 1**: possible in-class demo (only if the instructor selects us).
- **Fri Dec 4, 11:59 PM**: artifacts zip. No extensions. Every result in the report must be reproducible from it.

Time is very short. Build the smallest correct vertical slice first, then widen. Do not polish early.

## Hard rules

1. **Correctness before speed.** A join is only "done" when it matches the oracle (see SPEC section 9) on the test data.
2. **Every page access goes through the buffer pool.** No operator may keep table data in a `vector`/array/map beyond its allotted frame budget. The I/O counters are only valid if this holds. Review any new code for hidden in-memory copies.
3. **Work in checkpoints.** After each phase in `docs/PLAN.md`: build, run the tests, commit, and show me the test output and a 3-line summary. Then stop and wait for me to say go. Exception: if I say "continue through phase N", keep going until then.
4. **Ask before deviating from `docs/SPEC.md`.** If the spec is wrong or something is impractical, say so, propose the change, and record the decision in `docs/DECISIONS.md` (date, what, why).
5. **No report or proposal prose.** Course policy forbids AI-written report and proposal text. You may produce code, scripts, tables, CSVs and graphs, and you may point out what results the report needs. Do not draft report paragraphs.
6. **Log AI use.** Whenever you create or substantially change a component, add a row to `docs/AI_USAGE.md`. The report's announcement section must acknowledge vibe coding.
7. **Reproducibility.** Fixed seeds, no absolute paths, no manual steps. Everything runs from the repo root.

## Tech and commands

- C++17, CMake, `-O2` Release for all measurements (Debug only for debugging). No external C++ libraries in the engine; only the standard library. POSIX file I/O (`pread`/`pwrite`) is fine.
- Tests: plain-assert test executables registered with CTest, using a tiny header `tests/test_util.h` (no GoogleTest, so nothing to download).
- Python 3 for scripts only: standard library plus `matplotlib` (`scripts/requirements.txt`). Use stdlib `sqlite3` for the oracle.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
- Test: `ctest --test-dir build --output-on-failure`
- Layout is in `docs/SPEC.md` section 2. Keep it.

## Style

- Small files, one class per module, header plus source. Comment the *why* for the buffer pool, external sort, and Grace hash join; those are the places where silent bugs ruin results.
- Prefer clear code over clever code. A reviewer (the student) must be able to explain it in the presentation.
- No dead code, no commented-out experiments. Use `git` with one commit per checkpoint at minimum.

## When something is ambiguous

Pick the simplest option that satisfies the spec, write it down in `docs/DECISIONS.md`, and mention it in your checkpoint summary. Only stop and ask if the choice changes experiment results or costs more than about an hour to undo.
