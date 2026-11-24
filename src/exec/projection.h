#pragma once

#include <algorithm>
#include <stdexcept>

#include "exec/operator.h"

namespace qe {

class Projection : public Operator {
 public:
  Projection(Operator* child, std::vector<std::string> keep_columns)
      : child_(child), schema_(std::move(keep_columns)) {
    const std::vector<std::string>& child_schema = child_->OutputSchema();
    indices_.reserve(schema_.size());
    for (const std::string& name : schema_) {
      auto it = std::find(child_schema.begin(), child_schema.end(), name);
      if (it == child_schema.end()) {
        throw std::invalid_argument("Projection: unknown column '" + name + "'");
      }
      indices_.push_back(static_cast<std::size_t>(it - child_schema.begin()));
    }
  }

  void Open() override { child_->Open(); }

  bool Next(Tuple* out) override {
    Tuple in;
    if (!child_->Next(&in)) return false;
    out->resize(indices_.size());
    for (std::size_t i = 0; i < indices_.size(); ++i) {
      (*out)[i] = in[indices_[i]];
    }
    return true;
  }

  void Close() override { child_->Close(); }

  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  Operator* child_;
  std::vector<std::string> schema_;
  std::vector<std::size_t> indices_;
};

}
