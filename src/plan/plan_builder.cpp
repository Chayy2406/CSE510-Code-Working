#include "plan/plan_builder.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "exec/bnlj.h"
#include "exec/ghj.h"
#include "exec/inlj.h"
#include "exec/selection.h"
#include "exec/seq_scan.h"
#include "exec/smj.h"

namespace qe {

PlanBuilder::PlanBuilder(BufferPool* bp, DiskManager* dm, std::size_t buffer_pages,
                          std::string temp_prefix, IoStats* stats)
    : bp_(bp),
      dm_(dm),
      buffer_pages_(buffer_pages),
      temp_prefix_(std::move(temp_prefix)),
      stats_(stats) {}

void PlanBuilder::BindTable(const std::string& table, HeapFile* heap,
                            std::vector<std::string> columns) {
  tables_[table] = TableBinding{heap, std::move(columns)};
}

void PlanBuilder::BindIndex(const std::string& table, const std::string& column,
                            BPlusTree* index) {
  indexes_[table] = {column, index};
}

std::size_t PlanBuilder::ResolveColumn(const std::vector<std::string>& schema,
                                        const std::string& name) const {
  auto it = std::find(schema.begin(), schema.end(), name);
  if (it == schema.end()) {
    throw std::invalid_argument("PlanBuilder: unknown column '" + name + "'");
  }
  return static_cast<std::size_t>(it - schema.begin());
}

HeapFile* PlanBuilder::Materialize(Operator* op) {
  const std::vector<std::string>& names = op->OutputSchema();
  auto schema = std::make_unique<Schema>(names, names.size() * sizeof(std::int32_t));
  Schema* schema_ptr = schema.get();
  owned_schemas_.push_back(std::move(schema));

  std::string path = temp_prefix_ + "_materialize_" + std::to_string(temp_counter_++);
  std::filesystem::remove(path);  // temp files must always start empty
  FileId file = dm_->OpenFile(path, FileKind::kTemp);
  auto heap = std::make_unique<HeapFile>(bp_, dm_, file, schema_ptr);
  HeapFile* heap_ptr = heap.get();
  owned_heaps_.push_back(std::move(heap));

  op->Open();
  Tuple t;
  while (op->Next(&t)) heap_ptr->Append(t);
  op->Close();
  return heap_ptr;
}

PlanBuilder::HeapInput PlanBuilder::ResolveHeapInput(const PlanNode& node) {
  if (node.kind == PlanNodeKind::kScan) {
    const TableBinding& tb = tables_.at(node.table);
    return HeapInput{tb.heap, {}, tb.columns};
  }
  if (node.kind == PlanNodeKind::kSelection && node.left->kind == PlanNodeKind::kScan) {
    const TableBinding& tb = tables_.at(node.left->table);
    std::vector<Predicate> preds;
    preds.reserve(node.predicates.size());
    for (const NamedPredicate& np : node.predicates) {
      preds.push_back(Predicate{ResolveColumn(tb.columns, np.column), np.op, np.value});
    }
    return HeapInput{tb.heap, std::move(preds), tb.columns};
  }
  // A join subtree, or a selection over something other than a plain
  // scan: build generically and materialize so BNLJ (which needs direct
  // heap-file access for its blocked I/O) can still consume it.
  Operator* op = BuildRec(node, /*is_outermost=*/false);
  HeapFile* heap = Materialize(op);
  return HeapInput{heap, {}, op->OutputSchema()};
}

PlanBuilder::IndexInput PlanBuilder::ResolveIndexInput(const PlanNode& node) {
  const PlanNode* scan_node = &node;
  std::vector<NamedPredicate> post_predicates;
  if (node.kind == PlanNodeKind::kSelection) {
    post_predicates = node.predicates;
    scan_node = node.left.get();
  }
  if (scan_node->kind != PlanNodeKind::kScan) {
    throw std::invalid_argument(
        "PlanBuilder: INLJ's inner side must be a (possibly filtered) base table scan");
  }
  const TableBinding& tb = tables_.at(scan_node->table);
  auto it = indexes_.find(scan_node->table);
  if (it == indexes_.end()) {
    throw std::invalid_argument("PlanBuilder: no index bound for table '" + scan_node->table +
                                 "'");
  }
  return IndexInput{tb.heap, it->second.second, tb.columns, std::move(post_predicates)};
}

Operator* PlanBuilder::BuildRec(const PlanNode& node, bool is_outermost) {
  if (node.kind == PlanNodeKind::kScan) {
    const TableBinding& tb = tables_.at(node.table);
    auto scan = std::make_unique<SeqScan>(tb.heap, tb.columns);
    Operator* ptr = scan.get();
    owned_operators_.push_back(std::move(scan));
    return ptr;
  }

  if (node.kind == PlanNodeKind::kSelection) {
    Operator* child = BuildRec(*node.left, is_outermost);
    std::vector<Predicate> preds;
    preds.reserve(node.predicates.size());
    for (const NamedPredicate& np : node.predicates) {
      preds.push_back(Predicate{ResolveColumn(child->OutputSchema(), np.column), np.op, np.value});
    }
    auto sel = std::make_unique<Selection>(child, std::move(preds));
    Operator* ptr = sel.get();
    owned_operators_.push_back(std::move(sel));
    return ptr;
  }

  // kJoin
  Operator* join_op = nullptr;

  if (node.method == "bnlj") {
    HeapInput left_in = ResolveHeapInput(*node.left);
    HeapInput right_in = ResolveHeapInput(*node.right);
    std::size_t left_col = ResolveColumn(left_in.columns, node.left_key);
    std::size_t right_col = ResolveColumn(right_in.columns, node.right_key);
    auto bnlj = std::make_unique<BNLJ>(left_in.heap, left_in.columns, left_in.predicates,
                                        left_col, right_in.heap, right_in.columns,
                                        right_in.predicates, right_col, buffer_pages_);
    join_op = bnlj.get();
    owned_operators_.push_back(std::move(bnlj));
  } else if (node.method == "inlj") {
    Operator* outer_op = BuildRec(*node.left, false);
    IndexInput inner = ResolveIndexInput(*node.right);
    std::size_t outer_col = ResolveColumn(outer_op->OutputSchema(), node.left_key);
    auto inlj = std::make_unique<INLJ>(outer_op, outer_col, outer_op->OutputSchema(), inner.index,
                                        inner.heap, inner.columns);
    join_op = inlj.get();
    owned_operators_.push_back(std::move(inlj));
    if (!inner.post_predicates.empty()) {
      std::vector<Predicate> preds;
      preds.reserve(inner.post_predicates.size());
      for (const NamedPredicate& np : inner.post_predicates) {
        preds.push_back(
            Predicate{ResolveColumn(join_op->OutputSchema(), np.column), np.op, np.value});
      }
      auto sel = std::make_unique<Selection>(join_op, std::move(preds));
      join_op = sel.get();
      owned_operators_.push_back(std::move(sel));
    }
  } else if (node.method == "smj" || node.method == "ghj") {
    Operator* left_op = BuildRec(*node.left, false);
    Operator* right_op = BuildRec(*node.right, false);
    std::size_t left_col = ResolveColumn(left_op->OutputSchema(), node.left_key);
    std::size_t right_col = ResolveColumn(right_op->OutputSchema(), node.right_key);

    auto left_schema = std::make_unique<Schema>(
        left_op->OutputSchema(), left_op->OutputSchema().size() * sizeof(std::int32_t));
    auto right_schema = std::make_unique<Schema>(
        right_op->OutputSchema(), right_op->OutputSchema().size() * sizeof(std::int32_t));
    Schema* left_schema_ptr = left_schema.get();
    Schema* right_schema_ptr = right_schema.get();
    owned_schemas_.push_back(std::move(left_schema));
    owned_schemas_.push_back(std::move(right_schema));

    std::string prefix = temp_prefix_ + "_" + node.method + "_" + std::to_string(temp_counter_++);
    if (node.method == "smj") {
      auto smj = std::make_unique<SMJ>(left_op, left_col, left_schema_ptr,
                                        left_op->OutputSchema(), right_op, right_col,
                                        right_schema_ptr, right_op->OutputSchema(),
                                        buffer_pages_, bp_, dm_, prefix);
      join_op = smj.get();
      owned_operators_.push_back(std::move(smj));
    } else {
      auto ghj = std::make_unique<GHJ>(left_op, left_col, left_schema_ptr,
                                        left_op->OutputSchema(), right_op, right_col,
                                        right_schema_ptr, right_op->OutputSchema(),
                                        buffer_pages_, bp_, dm_, prefix, stats_);
      join_op = ghj.get();
      owned_operators_.push_back(std::move(ghj));
    }
  } else {
    throw std::invalid_argument("PlanBuilder: unknown join method '" + node.method + "'");
  }

  if (!is_outermost) {
    HeapFile* materialized = Materialize(join_op);
    auto scan = std::make_unique<SeqScan>(materialized, join_op->OutputSchema());
    Operator* ptr = scan.get();
    owned_operators_.push_back(std::move(scan));
    return ptr;
  }
  return join_op;
}

std::string PlanBuilder::Describe(const PlanNode& node) const {
  switch (node.kind) {
    case PlanNodeKind::kScan:
      return "Scan(" + node.table + ")";
    case PlanNodeKind::kSelection:
      return "Select(" + Describe(*node.left) + ")";
    case PlanNodeKind::kJoin:
      return node.method + "(" + Describe(*node.left) + ", " + Describe(*node.right) + ")";
  }
  return "";
}

}  // namespace qe
