#pragma once

#include <string>
#include <vector>

#include "heap/schema.h"

namespace qe {

// Iterator-style execution operator (SPEC.md section 6): Open() once,
// Next() repeatedly until it returns false, then Close(). OutputSchema()
// names each column of the tuples Next() produces, typically qualified
// as "Table.column" so joins can disambiguate same-named columns.
class Operator {
 public:
  virtual ~Operator() = default;
  virtual void Open() = 0;
  virtual bool Next(Tuple* out) = 0;
  virtual void Close() = 0;
  virtual const std::vector<std::string>& OutputSchema() const = 0;
};

}  // namespace qe
