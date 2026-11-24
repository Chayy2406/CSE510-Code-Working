#pragma once

#include <cstdint>

namespace qe {

struct IoStats {
  std::int64_t page_reads = 0;
  std::int64_t page_writes = 0;

  std::int64_t reads_base = 0;
  std::int64_t reads_index = 0;
  std::int64_t reads_temp = 0;
  std::int64_t writes_temp = 0;

  std::int64_t buffer_hits = 0;
  std::int64_t tuples_output = 0;
  std::int64_t ghj_overflow_partitions = 0;

  void Reset() { *this = IoStats{}; }
};

}
