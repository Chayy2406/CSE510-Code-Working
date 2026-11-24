#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "plan/plan_node.h"

namespace qe {

// System R style cost estimator (SPEC.md section 8): reduction factors
// from catalog statistics (not the data itself), a join-size formula,
// and the four joins' I/O cost formulas. This estimates a *given* plan;
// it does not search the plan space (SPEC.md section 13 explicitly
// excludes a cost-based optimizer search).
class CostEstimator {
 public:
  struct ColumnStats {
    std::int32_t min_value = 0;
    std::int32_t max_value = 0;
    std::size_t distinct_count = 1;
  };

  void SetTable(const std::string& table, double tuple_count, double page_count);
  void SetColumnStats(const std::string& qualified_column, std::int32_t min_value,
                      std::int32_t max_value, std::size_t distinct_count);

  double TupleCount(const std::string& table) const;
  double PageCount(const std::string& table) const;
  std::size_t DistinctCount(const std::string& qualified_column) const;

  // col = v -> 1/NKeys(col); col > v / col >= v -> (high - v) / (high - low);
  // col < v / col <= v -> (v - low) / (high - low).
  double ReductionFactor(const NamedPredicate& pred) const;

  // Product of each predicate's reduction factor (independence assumption).
  double CombinedReduction(const std::vector<NamedPredicate>& preds) const;

  // Estimated row count for `table` after applying `preds`.
  double EstimateScanCardinality(const std::string& table,
                                 const std::vector<NamedPredicate>& preds) const;

  // |R| * |S| / max(NKeys(R.k), NKeys(S.k)).
  static double EstimateJoinCardinality(double left_card, double right_card,
                                        std::size_t left_key_distinct,
                                        std::size_t right_key_distinct);

  // Pages needed to store `cardinality` tuples of `num_columns` int32
  // columns each (matches PlanBuilder::Materialize's temp tuple width).
  static double PagesForCardinality(double cardinality, std::size_t num_columns);

  // I/O formulas, SPEC.md section 8. M, N in pages; B = buffer_pages.
  static double BnljIO(double M, double N, std::size_t B);
  // outer_tuples = |R| (number of outer tuples probing the index); c =
  // cost of one index lookup (index height + data page fetch(es)).
  static double InljIO(double M, double outer_tuples, double c);
  static double SmjIO(double M, double N, std::size_t B);
  static double GhjIO(double M, double N, std::size_t B);

  static double IOForMethod(const std::string& method, double M, double N, std::size_t B,
                            double outer_tuples, double c);

 private:
  struct TableInfo {
    double tuple_count = 0;
    double page_count = 0;
  };

  std::unordered_map<std::string, TableInfo> tables_;
  std::unordered_map<std::string, ColumnStats> columns_;
};

}  // namespace qe
