#pragma once

#include <cstdint>

#include "heap/schema.h"

namespace qe {

// Order-insensitive result checksum (SPEC.md section 9: "row count plus an
// order-insensitive checksum"). Per tuple, fold its columns (in output
// order) into a 64-bit FNV-1a hash, then sum the per-tuple hashes modulo
// 2^64. Summation makes the total independent of row order; using a
// strong per-row hash as the addend (instead of the raw column values)
// makes accidental collisions between the engine's result and a wrong
// result very unlikely.
//
// scripts/oracle_check.py reimplements this exact algorithm in Python to
// compare against the engine's output without piping actual rows between
// processes. Any change here must be mirrored there.
inline std::uint64_t TupleHash(const Tuple& t) {
  std::uint64_t h = 0xcbf29ce484222325ULL;  // FNV offset basis
  for (std::int32_t v : t) {
    h ^= static_cast<std::uint32_t>(v);
    h *= 0x100000001b3ULL;  // FNV prime
  }
  return h;
}

inline std::uint64_t AccumulateChecksum(std::uint64_t checksum, const Tuple& t) {
  return checksum + TupleHash(t);  // unsigned wraparound is intentional
}

}  // namespace qe
