#pragma once

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include "exec/operator.h"

namespace qe {

// In-memory hash aggregate: COUNT(*) grouped by one column (SPEC.md
// section 6/7, QC). SPEC.md explicitly calls out the hash-aggregate path
// for a small group count (QC groups on S.rating: 10 groups) rather than
// the sort-based alternative, which this project doesn't need and
// doesn't implement (see docs/DECISIONS.md). HAVING is just a Selection
// applied to this operator's output (column 1 is the count).
class GroupBy : public Operator {
 public:
  GroupBy(Operator* child, const std::string& group_col, std::string count_alias = "count")
      : child_(child) {
    group_col_idx_ = ResolveColumn(child_->OutputSchema(), group_col);
    schema_ = {group_col, std::move(count_alias)};
  }

  void Open() override {
    counts_.clear();
    child_->Open();
    Tuple t;
    while (child_->Next(&t)) {
      ++counts_[t[group_col_idx_]];
    }
    child_->Close();
    it_ = counts_.begin();
  }

  bool Next(Tuple* out) override {
    if (it_ == counts_.end()) return false;
    *out = Tuple{it_->first, static_cast<std::int32_t>(it_->second)};
    ++it_;
    return true;
  }

  void Close() override {}

  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  static std::size_t ResolveColumn(const std::vector<std::string>& schema,
                                    const std::string& name) {
    auto it = std::find(schema.begin(), schema.end(), name);
    if (it == schema.end()) {
      throw std::invalid_argument("GroupBy: unknown column '" + name + "'");
    }
    return static_cast<std::size_t>(it - schema.begin());
  }

  Operator* child_;
  std::size_t group_col_idx_;
  std::vector<std::string> schema_;
  std::unordered_map<std::int32_t, std::int64_t> counts_;
  std::unordered_map<std::int32_t, std::int64_t>::iterator it_;
};

}  // namespace qe
