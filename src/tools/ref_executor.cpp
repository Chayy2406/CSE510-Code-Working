#include "tools/ref_executor.h"

namespace qe {

std::vector<Tuple> ReferenceJoinQA(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    std::int32_t rmin, std::int32_t dmin) {
  std::vector<Tuple> out;
  for (const Tuple& s : sailors) {           // s = (sid, rating, age)
    if (s[1] <= rmin) continue;
    for (const Tuple& r : reserves) {        // r = (sid, bid, day)
      if (r[0] != s[0]) continue;
      if (r[2] < dmin) continue;
      out.push_back({s[0], s[1], r[1], r[2]});  // (sid, rating, bid, day)
    }
  }
  return out;
}

}  // namespace qe
