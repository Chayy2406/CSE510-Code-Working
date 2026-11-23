#pragma once

#include <cstdint>
#include <vector>

#include "heap/schema.h"

namespace qe {

// Trivial, all-in-memory nested loop join, used only to check the real
// (paged, buffer-pooled) join operators against a dead-simple reference
// on small data in unit tests (SPEC.md section 9). No I/O counting, no
// buffer budget, no predicate pushdown distinctions — just the correct
// answer for QA: Sailors join Reserves on sid, filtered by
// rating > rmin and day >= dmin, projected to (sid, rating, bid, day).
std::vector<Tuple> ReferenceJoinQA(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    std::int32_t rmin, std::int32_t dmin);

}  // namespace qe
