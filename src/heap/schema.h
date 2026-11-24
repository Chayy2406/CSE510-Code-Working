#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qe {

using Tuple = std::vector<std::int32_t>;

class Schema {
 public:
  Schema(std::vector<std::string> columns, std::size_t tuple_bytes)
      : columns_(std::move(columns)), tuple_bytes_(tuple_bytes) {
    if (tuple_bytes_ < columns_.size() * sizeof(std::int32_t)) {
      throw std::invalid_argument(
          "Schema: tuple_bytes smaller than the columns require");
    }
  }

  std::size_t NumColumns() const { return columns_.size(); }
  std::size_t TupleBytes() const { return tuple_bytes_; }
  const std::string& ColumnName(std::size_t i) const { return columns_.at(i); }

  int ColumnIndex(const std::string& name) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i] == name) return static_cast<int>(i);
    }
    return -1;
  }

 private:
  std::vector<std::string> columns_;
  std::size_t tuple_bytes_;
};

inline Schema SailorsSchema(std::size_t tuple_bytes) {
  return Schema({"sid", "rating", "age"}, tuple_bytes);
}
inline Schema ReservesSchema(std::size_t tuple_bytes) {
  return Schema({"sid", "bid", "day"}, tuple_bytes);
}
inline Schema BoatsSchema(std::size_t tuple_bytes) {
  return Schema({"bid", "color"}, tuple_bytes);
}

}
