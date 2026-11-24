#include <cmath>

#include "plan/cost_estimator.h"
#include "test_util.h"

using namespace qe;

namespace {
bool Close(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }
}  // namespace

TEST_MAIN_BEGIN
  CostEstimator est;
  est.SetTable("S", /*tuple_count=*/1000, /*page_count=*/20);
  est.SetColumnStats("S.sid", 0, 999, 1000);
  est.SetColumnStats("S.rating", 1, 10, 10);
  est.SetColumnStats("S.age", 16, 70, 55);

  // Reduction factors.
  CHECK(Close(est.ReductionFactor(NamedPredicate{"S.rating", CompareOp::kEq, 5}), 1.0 / 10));
  // rating > 5 over [1,10]: (10-5)/(10-1) = 5/9.
  CHECK(Close(est.ReductionFactor(NamedPredicate{"S.rating", CompareOp::kGt, 5}), 5.0 / 9));
  // age < 40 over [16,70]: (40-16)/(70-16) = 24/54.
  CHECK(Close(est.ReductionFactor(NamedPredicate{"S.age", CompareOp::kLt, 40}), 24.0 / 54));

  // Combined (independence): product of individual factors.
  double combined = est.CombinedReduction(
      {NamedPredicate{"S.rating", CompareOp::kGt, 5}, NamedPredicate{"S.age", CompareOp::kLt, 40}});
  CHECK(Close(combined, (5.0 / 9) * (24.0 / 54)));
  CHECK(Close(est.EstimateScanCardinality(
                  "S", {NamedPredicate{"S.rating", CompareOp::kGt, 5},
                        NamedPredicate{"S.age", CompareOp::kLt, 40}}),
              1000 * combined));

  // Join cardinality: |R|*|S| / max(NKeys).
  CHECK(Close(CostEstimator::EstimateJoinCardinality(100, 200, 50, 80), (100.0 * 200) / 80));

  // Pages for cardinality: 2-column intermediate tuples (8 bytes each),
  // (4096-4)/8 = 511 tuples/page.
  CHECK_EQ(static_cast<long>(CostEstimator::PagesForCardinality(1022, 2)), 2L);
  CHECK_EQ(static_cast<long>(CostEstimator::PagesForCardinality(1, 2)), 1L);

  // BNLJ: M + ceil(M/(B-2))*N.
  CHECK(Close(CostEstimator::BnljIO(100, 50, 12), 100 + std::ceil(100.0 / 10) * 50));

  // INLJ: M + |R|*c.
  CHECK(Close(CostEstimator::InljIO(40, 1000, 3), 40 + 1000 * 3));

  // SMJ: reduces to 3(M+N) when a buffer large enough for one merge
  // pass is given (small M, N relative to B).
  CHECK(Close(CostEstimator::SmjIO(10, 10, 20), 3 * (10 + 10)));
  // SMJ: a tiny buffer needs extra passes, costing strictly more than 3(M+N).
  CHECK(CostEstimator::SmjIO(1000, 1000, 3) > 3.0 * (1000 + 1000));

  // GHJ: reduces to 3(M+N) when B is large enough for partitions to fit.
  CHECK(Close(CostEstimator::GhjIO(10, 10, 20), 3 * (10 + 10)));
  // GHJ: a buffer too small for partitions to fit costs strictly more.
  CHECK(CostEstimator::GhjIO(1000, 1000, 3) > 3.0 * (1000 + 1000));

  // IOForMethod dispatches correctly.
  CHECK(Close(CostEstimator::IOForMethod("bnlj", 100, 50, 12, 0, 0),
              CostEstimator::BnljIO(100, 50, 12)));
  CHECK(Close(CostEstimator::IOForMethod("inlj", 40, 0, 0, 1000, 3),
              CostEstimator::InljIO(40, 1000, 3)));
TEST_MAIN_END
