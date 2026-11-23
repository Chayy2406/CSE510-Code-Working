#pragma once

#include <memory>
#include <string>

#include "exec/external_sort.h"
#include "exec/operator.h"

namespace qe {

// Sort-merge join (SPEC.md section 6). Both inputs are sorted via
// ExternalSort down to at most (buffer_pages - 1) runs each, then joined
// by a fused final merge (no extra materialize/read pair). Many-to-many
// duplicate keys are handled by buffering the left side's duplicate
// group in memory; if a group exceeds one run's worth of tuples, it
// spills to a temp heap file and is rescanned per matching right tuple
// instead of held unbounded in memory.
//
// Deviation from SPEC.md: "skipping a sort if the input is already
// ordered" is not implemented — both sides are always sorted. Deciding
// when an input is already ordered is the plan builder's job (Phase 7),
// which doesn't exist yet; see docs/DECISIONS.md.
class SMJ : public Operator {
 public:
  SMJ(Operator* left, std::size_t left_col, const Schema* left_temp_schema,
      std::vector<std::string> left_names, Operator* right, std::size_t right_col,
      const Schema* right_temp_schema, std::vector<std::string> right_names,
      std::size_t buffer_pages, BufferPool* bp, DiskManager* dm,
      std::string temp_path_prefix);

  void Open() override;
  bool Next(Tuple* out) override;
  void Close() override;
  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  void AdvanceLeft();
  void AdvanceRight();
  bool AdvanceState();
  void ProcessEqualBlock(std::int32_t key);
  Tuple Combine(const Tuple& l, const Tuple& r) const;

  Operator* left_;
  std::size_t left_col_;
  const Schema* left_temp_schema_;
  std::vector<std::string> left_names_;

  Operator* right_;
  std::size_t right_col_;
  const Schema* right_temp_schema_;
  std::vector<std::string> right_names_;

  std::size_t buffer_pages_;
  BufferPool* bp_;
  DiskManager* dm_;
  std::string temp_path_prefix_;

  std::vector<std::string> schema_;

  std::unique_ptr<ExternalSort> left_sorter_;
  std::unique_ptr<ExternalSort> right_sorter_;
  std::unique_ptr<FusedMerge> left_merge_;
  std::unique_ptr<FusedMerge> right_merge_;

  bool left_valid_ = false;
  bool right_valid_ = false;
  Tuple left_cur_;
  Tuple right_cur_;

  std::vector<Tuple> left_group_mem_;
  std::unique_ptr<HeapFile> spill_heap_;
  int spill_counter_ = 0;

  std::vector<Tuple> output_queue_;
  std::size_t output_pos_ = 0;
};

}  // namespace qe
