#pragma once

#include <cstring>

#include "common/types.h"

namespace qe {

class Page {
 public:
  static constexpr std::size_t kSize = kPageSize;

  char* Data() { return data_; }
  const char* Data() const { return data_; }

  void Zero() { std::memset(data_, 0, kSize); }

 private:
  char data_[kSize]{};
};

}
