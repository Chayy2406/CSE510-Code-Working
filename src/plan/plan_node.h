#pragma once

#include <memory>
#include <string>
#include <vector>

#include "exec/predicate.h"

namespace qe {

enum class PlanNodeKind { kScan, kSelection, kJoin };

struct NamedPredicate {
  std::string column;
  CompareOp op;
  std::int32_t value;
};

struct PlanNode {
  PlanNodeKind kind;

  std::string table;

  std::vector<NamedPredicate> predicates;

  std::string method;
  std::string left_key;
  std::string right_key;

  std::unique_ptr<PlanNode> left;
  std::unique_ptr<PlanNode> right;

  static std::unique_ptr<PlanNode> Scan(std::string table) {
    auto n = std::make_unique<PlanNode>();
    n->kind = PlanNodeKind::kScan;
    n->table = std::move(table);
    return n;
  }

  static std::unique_ptr<PlanNode> Select(std::unique_ptr<PlanNode> child,
                                          std::vector<NamedPredicate> preds) {
    auto n = std::make_unique<PlanNode>();
    n->kind = PlanNodeKind::kSelection;
    n->predicates = std::move(preds);
    n->left = std::move(child);
    return n;
  }

  static std::unique_ptr<PlanNode> Join(std::string method, std::unique_ptr<PlanNode> left,
                                        std::string left_key, std::unique_ptr<PlanNode> right,
                                        std::string right_key) {
    auto n = std::make_unique<PlanNode>();
    n->kind = PlanNodeKind::kJoin;
    n->method = std::move(method);
    n->left = std::move(left);
    n->left_key = std::move(left_key);
    n->right = std::move(right);
    n->right_key = std::move(right_key);
    return n;
  }
};

}
