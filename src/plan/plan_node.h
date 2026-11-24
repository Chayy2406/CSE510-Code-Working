#pragma once

#include <memory>
#include <string>
#include <vector>

#include "exec/predicate.h"

namespace qe {

enum class PlanNodeKind { kScan, kSelection, kJoin };

// A predicate referencing a column by its qualified name (e.g. "S.rating")
// rather than a position, since the position depends on where in the
// tree the predicate ends up applied (pushed to a scan vs. applied after
// a join).
struct NamedPredicate {
  std::string column;
  CompareOp op;
  std::int32_t value;
};

// A tree of plan nodes that PlanBuilder turns into an executable operator
// tree (SPEC.md section 8). Built with the static factory methods below
// rather than constructed field-by-field.
struct PlanNode {
  PlanNodeKind kind;

  // kScan
  std::string table;  // "S" | "R" | "B"

  // kSelection
  std::vector<NamedPredicate> predicates;

  // kJoin
  std::string method;     // "bnlj" | "smj" | "ghj" | "inlj"
  std::string left_key;   // qualified column on the left subtree's output
  std::string right_key;  // qualified column on the right subtree's output

  std::unique_ptr<PlanNode> left;   // kSelection's child, or kJoin's left subtree
  std::unique_ptr<PlanNode> right;  // kJoin's right subtree only

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

}  // namespace qe
