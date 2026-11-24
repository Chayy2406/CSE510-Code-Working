#pragma once

#include "exec/operator.h"
#include "heap/heap_file.h"
#include "index/bplus_tree.h"

namespace qe {

class IndexScan : public Operator {
 public:
  IndexScan(BPlusTree* index, HeapFile* heap, std::vector<std::string> schema,
            std::int32_t lo, std::int32_t hi)
      : index_(index), heap_(heap), schema_(std::move(schema)), lo_(lo), hi_(hi) {}

  void Open() override {
    rids_.clear();
    pos_ = 0;
    index_->RangeScan(lo_, hi_, [this](std::int32_t, Rid r) { rids_.push_back(r); });
  }

  bool Next(Tuple* out) override {
    if (pos_ >= rids_.size()) return false;
    *out = heap_->Fetch(rids_[pos_++]);
    return true;
  }

  void Close() override {}

  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  BPlusTree* index_;
  HeapFile* heap_;
  std::vector<std::string> schema_;
  std::int32_t lo_;
  std::int32_t hi_;
  std::vector<Rid> rids_;
  std::size_t pos_ = 0;
};

}
