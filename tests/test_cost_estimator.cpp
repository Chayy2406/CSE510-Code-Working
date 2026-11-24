#include <cmath>

#include "plan/cost_estimator.h"
#include "test_util.h"

using namespace qe;

namespace {
bool Close(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }
}

TEST_MAIN_BEGIN
  CostEstimator est;
  est.SetTable("S",  1000,  20);
  est.SetColumnStats("S.sid", 0, 999, 1000);
  est.SetColumnStats("S.rating", 1, 10, 10);
  est.SetColumnStats("S.age", 16, 70, 55);

  CHECK(Close(est.ReductionFactor(NamedPredicate{"S.rating", CompareOp::kEq, 5}), 1.0 / 10));

  CHECK(Close(est.ReductionFactor(NamedPredicate{"S.rating", CompareOp::kGt, 5}), 5.0 / 9));

  CHECK(Close(est.ReductionFactor(NamedPredicate{"S.age", CompareOp::kLt, 40}), 24.0 / 54));

  double combined = est.CombinedReduction(
      {NamedPredicate{"S.rating", CompareOp::kGt, 5}, NamedPredicate{"S.age", CompareOp::kLt, 40}});
  CHECK(Close(combined, (5.0 / 9) * (24.0 / 54)));
  CHECK(Close(est.EstimateScanCardinality(
                  "S", {NamedPredicate{"S.rating", CompareOp::kGt, 5},
                        NamedPredicate{"S.age", CompareOp::kLt, 40}}),
              1000 * combined));

  CHECK(Close(CostEstimator::EstimateJoinCardinality(100, 200, 50, 80), (100.0 * 200) / 80));

  CHECK_EQ(static_cast<long>(CostEstimator::PagesForCardinality(1022, 2)), 2L);
  CHECK_EQ(static_cast<long>(CostEstimator::PagesForCardinality(1, 2)), 1L);

  CHECK(Close(CostEstimator::BnljIO(100, 50, 12), 100 + std::ceil(100.0 / 10) * 50));

  CHECK(Close(CostEstimator::InljIO(40, 1000, 3), 40 + 1000 * 3));

  CHECK(Close(CostEstimator::SmjIO(10, 10, 20), 3 * (10 + 10)));

  CHECK(CostEstimator::SmjIO(1000, 1000, 3) > 3.0 * (1000 + 1000));

  CHECK(Close(CostEstimator::GhjIO(10, 10, 20), 3 * (10 + 10)));

  CHECK(CostEstimator::GhjIO(1000, 1000, 3) > 3.0 * (1000 + 1000));

  CHECK(Close(CostEstimator::IOForMethod("bnlj", 100, 50, 12, 0, 0),
              CostEstimator::BnljIO(100, 50, 12)));
  CHECK(Close(CostEstimator::IOForMethod("inlj", 40, 0, 0, 1000, 3),
              CostEstimator::InljIO(40, 1000, 3)));
TEST_MAIN_END
