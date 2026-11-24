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

std::vector<Tuple> ReferenceJoinQB(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    const std::vector<Tuple>& boats, std::int32_t rmin,
                                    std::int32_t amax, std::int32_t color, std::int32_t d1,
                                    std::int32_t d2) {
  std::vector<Tuple> out;
  for (const Tuple& s : sailors) {       // s = (sid, rating, age)
    if (s[1] <= rmin) continue;
    if (s[2] >= amax) continue;
    for (const Tuple& r : reserves) {    // r = (sid, bid, day)
      if (r[0] != s[0]) continue;
      if (r[2] < d1 || r[2] > d2) continue;
      for (const Tuple& b : boats) {     // b = (bid, color)
        if (b[0] != r[1]) continue;
        if (b[1] != color) continue;
        out.push_back({s[0], r[2], b[1]});  // (S.sid, R.day, B.color)
      }
    }
  }
  return out;
}

}  // namespace qe
