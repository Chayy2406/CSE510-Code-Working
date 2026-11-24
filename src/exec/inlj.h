#pragma once

#include "exec/operator.h"
#include "heap/heap_file.h"
#include "index/bplus_tree.h"

namespace qe {

// Index nested loop join (SPEC.md section 6): for every outer tuple,
// probe the inner's B+ tree index on the join key and fetch each
// matching inner tuple. The outer is whichever side lacks an index —
// for QA that is Reserves, since the index is built on Sailors.sid
// (SPEC.md's proposal: "for every tuple of R, probe an index on S").
// Output schema is outer columns followed by inner columns, matching
// BNLJ/SMJ/GHJ's convention (even though here "outer" is R, not S).
class INLJ : public Operator {
 public:
  INLJ(Operator* outer, std::size_t outer_join_col, std::vector<std::string> outer_names,
       BPlusTree* inner_index, HeapFile* inner_heap, std::vector<std::string> inner_names)
      : outer_(outer),
        outer_join_col_(outer_join_col),
        inner_index_(inner_index),
        inner_heap_(inner_heap) {
    schema_ = std::move(outer_names);
    inner_cols_ = inner_names.size();
    schema_.insert(schema_.end(), inner_names.begin(), inner_names.end());
  }

  void Open() override {
    outer_->Open();
    matches_.clear();
    match_pos_ = 0;
  }

  bool Next(Tuple* out) override {
    while (match_pos_ >= matches_.size()) {
      if (!outer_->Next(&outer_cur_)) return false;
      matches_.clear();
      match_pos_ = 0;
      std::int32_t key = outer_cur_[outer_join_col_];
      inner_index_->EqualityScan(
          key, [this](Rid rid) { matches_.push_back(inner_heap_->Fetch(rid)); });
    }
    Tuple combined = outer_cur_;
    combined.reserve(outer_cur_.size() + inner_cols_);
    const Tuple& inner_t = matches_[match_pos_++];
    combined.insert(combined.end(), inner_t.begin(), inner_t.end());
    *out = std::move(combined);
    return true;
  }

  void Close() override {}

  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  Operator* outer_;
  std::size_t outer_join_col_;
  BPlusTree* inner_index_;
  HeapFile* inner_heap_;

  std::vector<std::string> schema_;
  std::size_t inner_cols_ = 0;

  Tuple outer_cur_;
  std::vector<Tuple> matches_;
  std::size_t match_pos_ = 0;
};

}  // namespace qe
