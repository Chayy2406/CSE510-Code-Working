#pragma once

#include <cstdint>

#include "heap/schema.h"

namespace qe {

inline std::uint64_t TupleHash(const Tuple& t) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (std::int32_t v : t) {
    h ^= static_cast<std::uint32_t>(v);
    h *= 0x100000001b3ULL;
  }
  return h;
}

inline std::uint64_t AccumulateChecksum(std::uint64_t checksum, const Tuple& t) {
  return checksum + TupleHash(t);
}

}
