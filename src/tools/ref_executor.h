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

// QB: Sailors join Reserves on sid, join Boats on bid, filtered by
// rating > rmin, age < amax, color = color, day BETWEEN d1 AND d2,
// projected to (S.sid, R.day, B.color).
std::vector<Tuple> ReferenceJoinQB(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    const std::vector<Tuple>& boats, std::int32_t rmin,
                                    std::int32_t amax, std::int32_t color, std::int32_t d1,
                                    std::int32_t d2);

// QC: QB's join and predicates, GROUP BY S.rating, HAVING COUNT(*) > h.
// Output is (rating, count) pairs.
std::vector<Tuple> ReferenceJoinQC(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    const std::vector<Tuple>& boats, std::int32_t rmin,
                                    std::int32_t amax, std::int32_t color, std::int32_t d1,
                                    std::int32_t d2, std::int32_t h);

}  // namespace qe
