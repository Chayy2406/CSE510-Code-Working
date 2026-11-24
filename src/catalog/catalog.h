#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "heap/heap_file.h"

namespace qe {

struct ColumnStats {
  std::int32_t min_value = 0;
  std::int32_t max_value = 0;
  std::size_t distinct_count = 0;
};

struct TableStats {
  std::size_t tuple_count = 0;
  std::size_t page_count = 0;
  std::vector<ColumnStats> columns;
  int index_height = 0;
};

class Catalog {
 public:

  void ComputeTableStats(const std::string& table, const HeapFile& heap,
                          const Schema& schema);

  bool HasTable(const std::string& table) const;
  const TableStats& GetTableStats(const std::string& table) const;

 private:
  std::unordered_map<std::string, TableStats> tables_;
};

}
