#include "tools/ref_executor.h"

#include <unordered_map>

namespace qe {

std::vector<Tuple> ReferenceJoinQA(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    std::int32_t rmin, std::int32_t dmin) {
  std::vector<Tuple> out;
  for (const Tuple& s : sailors) {
    if (s[1] <= rmin) continue;
    for (const Tuple& r : reserves) {
      if (r[0] != s[0]) continue;
      if (r[2] < dmin) continue;
      out.push_back({s[0], s[1], r[1], r[2]});
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
  for (const Tuple& s : sailors) {
    if (s[1] <= rmin) continue;
    if (s[2] >= amax) continue;
    for (const Tuple& r : reserves) {
      if (r[0] != s[0]) continue;
      if (r[2] < d1 || r[2] > d2) continue;
      for (const Tuple& b : boats) {
        if (b[0] != r[1]) continue;
        if (b[1] != color) continue;
        out.push_back({s[0], r[2], b[1]});
      }
    }
  }
  return out;
}

std::vector<Tuple> ReferenceJoinQC(const std::vector<Tuple>& sailors,
                                    const std::vector<Tuple>& reserves,
                                    const std::vector<Tuple>& boats, std::int32_t rmin,
                                    std::int32_t amax, std::int32_t color, std::int32_t d1,
                                    std::int32_t d2, std::int32_t h) {
  std::unordered_map<std::int32_t, std::int64_t> counts;
  for (const Tuple& s : sailors) {
    if (s[1] <= rmin) continue;
    if (s[2] >= amax) continue;
    for (const Tuple& r : reserves) {
      if (r[0] != s[0]) continue;
      if (r[2] < d1 || r[2] > d2) continue;
      for (const Tuple& b : boats) {
        if (b[0] != r[1]) continue;
        if (b[1] != color) continue;
        ++counts[s[1]];
      }
    }
  }
  std::vector<Tuple> out;
  for (const auto& kv : counts) {
    if (kv.second > h) out.push_back({kv.first, static_cast<std::int32_t>(kv.second)});
  }
  return out;
}

}
