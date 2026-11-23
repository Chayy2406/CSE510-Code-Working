#pragma once

#include <cstdint>
#include <vector>

#include "heap/schema.h"

namespace qe {

enum class CompareOp { kEq, kLt, kLe, kGt, kGe };

// tuple[col] `op` value. Column indices are positions in whatever schema
// the predicate is being evaluated against (a scan's table schema, or a
// join's combined output schema for post-join filters).
struct Predicate {
  std::size_t col;
  CompareOp op;
  std::int32_t value;
};

inline bool Evaluate(const Predicate& p, const Tuple& t) {
  std::int32_t v = t[p.col];
  switch (p.op) {
    case CompareOp::kEq: return v == p.value;
    case CompareOp::kLt: return v < p.value;
    case CompareOp::kLe: return v <= p.value;
    case CompareOp::kGt: return v > p.value;
    case CompareOp::kGe: return v >= p.value;
  }
  return false;
}

// AND of every predicate in the list (an empty list always passes).
inline bool EvaluateAll(const std::vector<Predicate>& preds, const Tuple& t) {
  for (const Predicate& p : preds) {
    if (!Evaluate(p, t)) return false;
  }
  return true;
}

}  // namespace qe
