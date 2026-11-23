# AI usage log

Course policy: vibe coding tools are allowed for the system implementation and must be acknowledged in the technical report's announcement section. AI must not be used to write the proposal or the technical report text.
Claude Code appends a row whenever it creates or substantially changes a component. Students: add your own review notes.

| Date | Component / file(s) | Tool | What it did | Reviewed by |
|---|---|---|---|---|
| 2026-11-23 | Project guide and specs (`CLAUDE.md`, `docs/SPEC.md`, `docs/PLAN.md`, `START_HERE.md`) | Claude (chat) | Turned the proposal, slides and policy into an implementation spec and build plan | |
| 2026-11-23 | Phase 0 scaffold: `CMakeLists.txt`, `tests/test_util.h`, `tests/test_smoke.cpp`, `tests/CMakeLists.txt` | Claude Code | CMake project (C++17, Release default), CTest wiring, plain-assert test helper | |
| 2026-11-23 | Phase 1 storage: `src/common/types.h`, `src/common/stats.h`, `src/storage/page.h`, `src/storage/disk_manager.{h,cpp}`, `src/storage/buffer_pool.{h,cpp}`, `tests/test_disk_manager.cpp`, `tests/test_buffer_pool.cpp` | Claude Code | DiskManager (pread/pwrite, per-kind counters) and BufferPool (LRU, pin counts, sticky dirty bit, BufferFullError); unit tests for hit/miss, dirty vs clean eviction, pin protection, BufferFullError, and a hand-computed counter trace | |
