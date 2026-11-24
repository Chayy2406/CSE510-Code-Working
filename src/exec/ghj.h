#pragma once

#include <deque>
#include <memory>
#include <string>

#include "common/stats.h"
#include "exec/operator.h"
#include "heap/heap_file.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

class GHJ : public Operator {
 public:
  GHJ(Operator* left, std::size_t left_col, const Schema* left_temp_schema,
      std::vector<std::string> left_names, Operator* right, std::size_t right_col,
      const Schema* right_temp_schema, std::vector<std::string> right_names,
      std::size_t buffer_pages, BufferPool* bp, DiskManager* dm,
      std::string temp_path_prefix, IoStats* stats);

  void Open() override;
  bool Next(Tuple* out) override;
  void Close() override;
  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  struct PartitionPair {
    HeapFile* left_heap;
    PageNo left_start, left_end;
    HeapFile* right_heap;
    PageNo right_start, right_end;
    int depth;
  };

  static std::size_t Hash(std::int32_t key, int seed, std::size_t num_partitions);
  Tuple Combine(const Tuple& l, const Tuple& r) const;
  HeapFile* NewTempHeap(const Schema* schema, const std::string& suffix);

  std::vector<PartitionPair> PartitionFromOperator(int seed);
  std::vector<PartitionPair> PartitionFromHeap(const PartitionPair& p, int seed);
  void ProcessNextPending();
  void RunHashJoin(const PartitionPair& p, bool build_left);
  void RunFallbackJoin(const PartitionPair& p);

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
  IoStats* stats_;

  std::vector<std::string> schema_;
  std::vector<std::unique_ptr<HeapFile>> heap_files_;
  int temp_counter_ = 0;

  static constexpr int kMaxDepth = 4;

  std::deque<PartitionPair> pending_;
  std::vector<Tuple> output_queue_;
  std::size_t output_pos_ = 0;
};

}
