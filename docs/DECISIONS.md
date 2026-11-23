# Decisions

Record every choice that is not fixed by `docs/SPEC.md`, and every deviation from it. Newest at the bottom.
Format: `date | decision | why | impact on experiments`.

2026-11-23 | Pages are file-backed (`pread`/`pwrite`), 4096 bytes, fixed-width int32 tuples with padding | Query time is the main metric; page I/O counted exactly at the DiskManager | Wall-clock time includes OS page cache effects; document in the report's setup text
2026-11-23 | Every join except the last materializes its output to a temp heap file | One join active at a time gets the full buffer budget B; formulas use exact M, N | Intermediate-result I/O is counted and included in the estimator
2026-11-23 | Final output goes to a Sink that counts rows and checksums; no page writes for the final result | The join formulas exclude output cost | Output cardinality reported separately
2026-11-23 | Oracle is SQLite via Python stdlib on CSVs exported by the generator | Required by the proposal | Result comparison is by row count plus an order-insensitive checksum
2026-11-23 | `BufferPool::NewPage` always marks the frame dirty, regardless of the dirty flag later passed to `Unpin` | A freshly allocated page has never been written to disk, so it must be flushed eventually or its contents (and anything the caller writes into it) are lost; `Unpin`'s dirty flag is sticky (OR-only) and cannot clear it | Eviction/flush write counts include one write per page ever allocated via `NewPage`, even if the caller's own writes were later undone; unit tests assert on this |
2026-11-23 | `BufferPool::Reset()` calls `FlushAll()` before emptying the pool (not just a bare clear) | Silently dropping dirty frames would lose data and make repeated "cold start" runs non-reproducible; SPEC.md only says Reset "empties the pool" but flushing first is the only safe reading | `Reset()` can itself produce page writes, which is included in `page_writes` right before a measured run begins — callers should snapshot counters after Reset, not assume Reset is a no-op on stats |
