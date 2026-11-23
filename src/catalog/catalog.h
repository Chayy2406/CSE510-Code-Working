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
  std::vector<ColumnStats> columns;  // parallel to the table's Schema
  int index_height = 0;              // filled in once a B+ tree exists (Phase 5)
};

// Per-process table statistics, recomputed by scanning each heap file once
// at load time (SPEC.md section 5). There is no catalog file in the
// repository layout: every tool that opens a table recomputes its stats,
// which is cheap at this project's scale and keeps the estimator honest
// about reading only the catalog, not the data, afterwards.
class Catalog {
 public:
  // Scans `heap` once and records its statistics under `table`.
  void ComputeTableStats(const std::string& table, const HeapFile& heap,
                          const Schema& schema);

  bool HasTable(const std::string& table) const;
  const TableStats& GetTableStats(const std::string& table) const;

 private:
  std::unordered_map<std::string, TableStats> tables_;
};

}  // namespace qe
