#pragma once

#include "exec/operator.h"
#include "exec/predicate.h"

namespace qe {

// Pipelined filter over any child operator. Used when predicates are not
// pushed into the scan (pushdown off) or when they apply after a join.
class Selection : public Operator {
 public:
  Selection(Operator* child, std::vector<Predicate> predicates)
      : child_(child), predicates_(std::move(predicates)) {}

  void Open() override { child_->Open(); }

  bool Next(Tuple* out) override {
    while (child_->Next(out)) {
      if (EvaluateAll(predicates_, *out)) return true;
    }
    return false;
  }

  void Close() override { child_->Close(); }

  const std::vector<std::string>& OutputSchema() const override {
    return child_->OutputSchema();
  }

 private:
  Operator* child_;
  std::vector<Predicate> predicates_;
};

}  // namespace qe
