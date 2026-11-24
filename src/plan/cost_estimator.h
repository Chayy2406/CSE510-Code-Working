#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "plan/plan_node.h"

namespace qe {

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

  double ReductionFactor(const NamedPredicate& pred) const;

  double CombinedReduction(const std::vector<NamedPredicate>& preds) const;

  double EstimateScanCardinality(const std::string& table,
                                 const std::vector<NamedPredicate>& preds) const;

  static double EstimateJoinCardinality(double left_card, double right_card,
                                        std::size_t left_key_distinct,
                                        std::size_t right_key_distinct);

  static double PagesForCardinality(double cardinality, std::size_t num_columns);

  static double BnljIO(double M, double N, std::size_t B);

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

}
