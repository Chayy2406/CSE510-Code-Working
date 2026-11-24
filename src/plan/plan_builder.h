#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include "common/stats.h"
#include "exec/operator.h"
#include "heap/heap_file.h"
#include "index/bplus_tree.h"
#include "plan/plan_node.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

// Turns a PlanNode tree into an executable operator tree (SPEC.md
// section 8). Every join except the outermost one materializes its
// output to a temp heap file; the outermost pipelines directly to
// whatever the caller does with the returned Operator* (SPEC.md section
// 6). Owns every operator, heap file, and schema it creates to build the
// tree — all stay alive for as long as this PlanBuilder does.
class PlanBuilder {
 public:
  // bp, dm, stats must outlive this PlanBuilder.
  PlanBuilder(BufferPool* bp, DiskManager* dm, std::size_t buffer_pages,
              std::string temp_prefix, IoStats* stats);

  // Registers a base table. `columns` are qualified names in heap's
  // storage order (e.g. {"S.sid", "S.rating", "S.age"}).
  void BindTable(const std::string& table, HeapFile* heap, std::vector<std::string> columns);

  // Registers a B+ tree index on `table.column`, enabling that table to
  // be used as an INLJ inner (SPEC.md: "INLJ only where the inner has an
  // index").
  void BindIndex(const std::string& table, const std::string& column, BPlusTree* index);

  Operator* Build(const PlanNode& node) { return BuildRec(node, /*is_outermost=*/true); }

  std::string Describe(const PlanNode& node) const;

 private:
  struct TableBinding {
    HeapFile* heap;
    std::vector<std::string> columns;
  };
  struct HeapInput {
    HeapFile* heap;
    std::vector<Predicate> predicates;
    std::vector<std::string> columns;
  };
  struct IndexInput {
    HeapFile* heap;
    BPlusTree* index;
    std::vector<std::string> columns;
    std::vector<NamedPredicate> post_predicates;
  };

  Operator* BuildRec(const PlanNode& node, bool is_outermost);
  HeapInput ResolveHeapInput(const PlanNode& node);
  IndexInput ResolveIndexInput(const PlanNode& node);
  HeapFile* Materialize(Operator* op);
  std::size_t ResolveColumn(const std::vector<std::string>& schema, const std::string& name) const;

  BufferPool* bp_;
  DiskManager* dm_;
  std::size_t buffer_pages_;
  std::string temp_prefix_;
  IoStats* stats_;
  int temp_counter_ = 0;

  std::unordered_map<std::string, TableBinding> tables_;
  std::unordered_map<std::string, std::pair<std::string, BPlusTree*>> indexes_;

  std::vector<std::unique_ptr<Operator>> owned_operators_;
  std::vector<std::unique_ptr<HeapFile>> owned_heaps_;
  std::vector<std::unique_ptr<Schema>> owned_schemas_;
};

}  // namespace qe
