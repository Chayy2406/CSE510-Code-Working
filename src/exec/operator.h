#pragma once

#include <string>
#include <vector>

#include "heap/schema.h"

namespace qe {

class Operator {
 public:
  virtual ~Operator() = default;
  virtual void Open() = 0;
  virtual bool Next(Tuple* out) = 0;
  virtual void Close() = 0;
  virtual const std::vector<std::string>& OutputSchema() const = 0;
};

}
