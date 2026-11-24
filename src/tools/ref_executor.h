#pragma once

#include <cstdint>
#include <vector>

#include "heap/schema.h"

namespace qe {

std::vector<Tuple> ReferenceJoinQA(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    std::int32_t rmin, std::int32_t dmin);

std::vector<Tuple> ReferenceJoinQB(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    const std::vector<Tuple>& boats, std::int32_t rmin,
                                    std::int32_t amax, std::int32_t color, std::int32_t d1,
                                    std::int32_t d2);

std::vector<Tuple> ReferenceJoinQC(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    const std::vector<Tuple>& boats, std::int32_t rmin,
                                    std::int32_t amax, std::int32_t color, std::int32_t d1,
                                    std::int32_t d2, std::int32_t h);

}
