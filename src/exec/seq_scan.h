#pragma once

#include "exec/operator.h"
#include "exec/predicate.h"
#include "heap/heap_file.h"

namespace qe {

class SeqScan : public Operator {
 public:
  SeqScan(HeapFile* heap, std::vector<std::string> schema,
          std::vector<Predicate> predicates = {})
      : heap_(heap), schema_(std::move(schema)), predicates_(std::move(predicates)) {}

  void Open() override {
    page_ = 0;
    slot_ = 0;
  }

  bool Next(Tuple* out) override {
    while (heap_->NextTuple(&page_, &slot_, out)) {
      if (EvaluateAll(predicates_, *out)) return true;
    }
    return false;
  }

  void Close() override {}

  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  HeapFile* heap_;
  std::vector<std::string> schema_;
  std::vector<Predicate> predicates_;
  PageNo page_ = 0;
  std::int32_t slot_ = 0;
};

}
