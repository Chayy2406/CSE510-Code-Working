#pragma once

#include <cstdint>

#include "exec/checksum.h"
#include "exec/operator.h"

namespace qe {

struct SinkResult {
  std::size_t row_count = 0;
  std::uint64_t checksum = 0;
};

// Terminal consumer: pulls every tuple from `child`, counting rows and
// folding them into an order-insensitive checksum. Writes no pages, so
// output cost is excluded from the join I/O formulas (SPEC.md section 6).
class Sink {
 public:
  explicit Sink(Operator* child) : child_(child) {}

  SinkResult Run() {
    SinkResult result;
    child_->Open();
    Tuple t;
    while (child_->Next(&t)) {
      ++result.row_count;
      result.checksum = AccumulateChecksum(result.checksum, t);
    }
    child_->Close();
    return result;
  }

 private:
  Operator* child_;
};

}  // namespace qe
