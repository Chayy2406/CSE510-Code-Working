#include "plan/cost_estimator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "common/types.h"

namespace qe {

void CostEstimator::SetTable(const std::string& table, double tuple_count, double page_count) {
  tables_[table] = TableInfo{tuple_count, page_count};
}

void CostEstimator::SetColumnStats(const std::string& qualified_column, std::int32_t min_value,
                                   std::int32_t max_value, std::size_t distinct_count) {
  columns_[qualified_column] = ColumnStats{min_value, max_value, std::max<std::size_t>(1, distinct_count)};
}

double CostEstimator::TupleCount(const std::string& table) const {
  return tables_.at(table).tuple_count;
}

double CostEstimator::PageCount(const std::string& table) const {
  return tables_.at(table).page_count;
}

std::size_t CostEstimator::DistinctCount(const std::string& qualified_column) const {
  return columns_.at(qualified_column).distinct_count;
}

double CostEstimator::ReductionFactor(const NamedPredicate& pred) const {
  const ColumnStats& cs = columns_.at(pred.column);
  double range = static_cast<double>(cs.max_value) - static_cast<double>(cs.min_value);
  if (range <= 0) range = 1;

  switch (pred.op) {
    case CompareOp::kEq:
      return 1.0 / static_cast<double>(cs.distinct_count);
    case CompareOp::kGt:
    case CompareOp::kGe: {
      double v = std::clamp<double>(pred.value, cs.min_value, cs.max_value);
      return (cs.max_value - v) / range;
    }
    case CompareOp::kLt:
    case CompareOp::kLe: {
      double v = std::clamp<double>(pred.value, cs.min_value, cs.max_value);
      return (v - cs.min_value) / range;
    }
  }
  return 1.0;
}

double CostEstimator::CombinedReduction(const std::vector<NamedPredicate>& preds) const {
  double factor = 1.0;
  for (const NamedPredicate& p : preds) factor *= ReductionFactor(p);
  return factor;
}

double CostEstimator::EstimateScanCardinality(const std::string& table,
                                              const std::vector<NamedPredicate>& preds) const {
  return TupleCount(table) * CombinedReduction(preds);
}

double CostEstimator::EstimateJoinCardinality(double left_card, double right_card,
                                              std::size_t left_key_distinct,
                                              std::size_t right_key_distinct) {
  std::size_t denom = std::max<std::size_t>(1, std::max(left_key_distinct, right_key_distinct));
  return (left_card * right_card) / static_cast<double>(denom);
}

double CostEstimator::PagesForCardinality(double cardinality, std::size_t num_columns) {
  double tuple_bytes = static_cast<double>(num_columns) * sizeof(std::int32_t);
  double tuples_per_page = std::floor((static_cast<double>(kPageSize) - 4.0) / tuple_bytes);
  if (tuples_per_page < 1) tuples_per_page = 1;
  return std::ceil(cardinality / tuples_per_page);
}

double CostEstimator::BnljIO(double M, double N, std::size_t B) {
  double block = (B > 2) ? static_cast<double>(B - 2) : 1.0;
  return M + std::ceil(M / block) * N;
}

double CostEstimator::InljIO(double M, double outer_tuples, double c) {
  return M + outer_tuples * c;
}

double CostEstimator::SmjIO(double M, double N, std::size_t B) {
  double fan_in = (B > 2) ? static_cast<double>(B - 1) : 2.0;
  auto side_cost = [fan_in](double P) {
    double runs = std::ceil(P / std::max(1.0, fan_in));
    double passes = 0;
    while (runs > fan_in) {
      runs = std::ceil(runs / fan_in);
      ++passes;
    }

    return 2 * P + passes * (2 * P) + P;
  };
  return side_cost(M) + side_cost(N);
}

double CostEstimator::GhjIO(double M, double N, std::size_t B) {
  constexpr double kFudgeFactor = 1.2;
  double partitions_fit = static_cast<double>(B) > std::sqrt(kFudgeFactor * std::min(M, N));
  if (partitions_fit) return 3 * (M + N);

  return 3 * (M + N) + 2 * (M + N);
}

double CostEstimator::IOForMethod(const std::string& method, double M, double N, std::size_t B,
                                  double outer_tuples, double c) {
  if (method == "bnlj") return BnljIO(M, N, B);
  if (method == "inlj") return InljIO(M, outer_tuples, c);
  if (method == "smj") return SmjIO(M, N, B);
  if (method == "ghj") return GhjIO(M, N, B);
  throw std::invalid_argument("CostEstimator: unknown method '" + method + "'");
}

}
