#include "catalog/catalog.h"

#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace qe {

void Catalog::ComputeTableStats(const std::string& table, const HeapFile& heap,
                                 const Schema& schema) {
  TableStats stats;
  stats.columns.resize(schema.NumColumns());
  std::vector<std::unordered_set<std::int32_t>> distinct(schema.NumColumns());
  for (std::size_t c = 0; c < schema.NumColumns(); ++c) {
    stats.columns[c].min_value = std::numeric_limits<std::int32_t>::max();
    stats.columns[c].max_value = std::numeric_limits<std::int32_t>::min();
  }

  heap.Scan([&](Rid, const Tuple& tuple) {
    ++stats.tuple_count;
    for (std::size_t c = 0; c < tuple.size(); ++c) {
      std::int32_t v = tuple[c];
      ColumnStats& cs = stats.columns[c];
      if (v < cs.min_value) cs.min_value = v;
      if (v > cs.max_value) cs.max_value = v;
      distinct[c].insert(v);
    }
  });

  for (std::size_t c = 0; c < schema.NumColumns(); ++c) {
    stats.columns[c].distinct_count = distinct[c].size();
  }
  if (stats.tuple_count == 0) {
    for (auto& cs : stats.columns) {
      cs.min_value = 0;
      cs.max_value = 0;
    }
  }

  stats.page_count = heap.NumPages();
  tables_[table] = std::move(stats);
}

bool Catalog::HasTable(const std::string& table) const {
  return tables_.find(table) != tables_.end();
}

const TableStats& Catalog::GetTableStats(const std::string& table) const {
  auto it = tables_.find(table);
  if (it == tables_.end()) {
    throw std::out_of_range("Catalog: unknown table '" + table + "'");
  }
  return it->second;
}

}  // namespace qe
