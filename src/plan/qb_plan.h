#pragma once

#include "plan/plan_node.h"

namespace qe {

inline std::unique_ptr<PlanNode> BuildQBPlan(const std::string& join_order,
                                             const std::string& method1,
                                             const std::string& method2, bool pushdown,
                                             std::int32_t rmin, std::int32_t amax,
                                             std::int32_t color, std::int32_t d1,
                                             std::int32_t d2) {
  std::vector<NamedPredicate> s_preds = {{"S.rating", CompareOp::kGt, rmin},
                                          {"S.age", CompareOp::kLt, amax}};
  std::vector<NamedPredicate> r_preds = {{"R.day", CompareOp::kGe, d1},
                                          {"R.day", CompareOp::kLe, d2}};
  std::vector<NamedPredicate> b_preds = {{"B.color", CompareOp::kEq, color}};

  auto make_leaf = [&](const std::string& table, std::vector<NamedPredicate> preds) {
    auto scan = PlanNode::Scan(table);
    if (pushdown) return PlanNode::Select(std::move(scan), std::move(preds));
    return scan;
  };

  auto s_leaf = make_leaf("S", s_preds);
  auto r_leaf = make_leaf("R", r_preds);
  auto b_leaf = make_leaf("B", b_preds);

  std::unique_ptr<PlanNode> top;
  if (join_order == "order1") {
    auto join1 = PlanNode::Join(method1, std::move(r_leaf), "R.sid", std::move(s_leaf), "S.sid");
    top = PlanNode::Join(method2, std::move(join1), "R.bid", std::move(b_leaf), "B.bid");
  } else {
    auto join1 = PlanNode::Join(method1, std::move(r_leaf), "R.bid", std::move(b_leaf), "B.bid");
    top = PlanNode::Join(method2, std::move(join1), "R.sid", std::move(s_leaf), "S.sid");
  }

  if (!pushdown) {
    std::vector<NamedPredicate> all_preds = s_preds;
    all_preds.insert(all_preds.end(), r_preds.begin(), r_preds.end());
    all_preds.insert(all_preds.end(), b_preds.begin(), b_preds.end());
    top = PlanNode::Select(std::move(top), std::move(all_preds));
  }
  return top;
}

}
