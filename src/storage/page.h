#pragma once

#include <cstring>

#include "common/types.h"

namespace qe {

// One fixed-size page of raw bytes. Layout of the bytes (heap header,
// B+ tree node, etc.) is interpreted by the layer above; the buffer pool
// only moves bytes.
class Page {
 public:
  static constexpr std::size_t kSize = kPageSize;

  char* Data() { return data_; }
  const char* Data() const { return data_; }

  void Zero() { std::memset(data_, 0, kSize); }

 private:
  char data_[kSize]{};
};

}  // namespace qe
