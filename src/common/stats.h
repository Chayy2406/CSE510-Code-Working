#pragma once

#include <cstdint>

namespace qe {

// Every page access in the engine flows through DiskManager (reads/writes)
// or BufferPool (hits), both of which increment this struct. One instance
// is shared per run so a test or experiment can zero it and get exact,
// reproducible I/O counts (SPEC.md section 3 and 10).
struct IoStats {
  std::int64_t page_reads = 0;
  std::int64_t page_writes = 0;

  std::int64_t reads_base = 0;
  std::int64_t reads_index = 0;
  std::int64_t reads_temp = 0;
  std::int64_t writes_temp = 0;

  std::int64_t buffer_hits = 0;
  std::int64_t tuples_output = 0;
  std::int64_t ghj_overflow_partitions = 0;  // Grace Hash Join BNLJ-fallback count

  void Reset() { *this = IoStats{}; }
};

}  // namespace qe
