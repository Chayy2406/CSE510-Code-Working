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

class PlanBuilder {
 public:

  PlanBuilder(BufferPool* bp, DiskManager* dm, std::size_t buffer_pages,
              std::string temp_prefix, IoStats* stats);

  void BindTable(const std::string& table, HeapFile* heap, std::vector<std::string> columns);

  void BindIndex(const std::string& table, const std::string& column, BPlusTree* index);

  Operator* Build(const PlanNode& node) { return BuildRec(node,  true); }

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

}
