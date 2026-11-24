#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "exec/operator.h"
#include "heap/heap_file.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

struct SortedRunSet {
  HeapFile* heap = nullptr;
  std::vector<std::pair<PageNo, PageNo>> runs;
  std::size_t sort_col = 0;
};

class FusedMerge : public Operator {
 public:
  FusedMerge(SortedRunSet runs, std::vector<std::string> schema);

  void Open() override;
  bool Next(Tuple* out) override;
  void Close() override {}
  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  struct RunCursor {
    HeapFile* heap;
    PageNo page;
    PageNo end;
    std::vector<Tuple> page_tuples;
    std::size_t idx = 0;
    bool valid = false;
    Tuple current;

    void LoadPage();
    void Advance();
  };

  SortedRunSet runs_;
  std::vector<std::string> schema_;
  std::vector<RunCursor> cursors_;
};

class ExternalSort {
 public:

  ExternalSort(Operator* child, std::size_t sort_col, const Schema* temp_schema,
               std::vector<std::string> output_names, std::size_t buffer_pages,
               BufferPool* bp, DiskManager* dm, std::string temp_path_prefix);

  SortedRunSet PrepareSortedRuns(std::size_t max_runs);
  SortedRunSet Sort() { return PrepareSortedRuns(1); }

  const std::vector<std::string>& OutputSchema() const { return output_names_; }

 private:
  SortedRunSet FormRuns();
  SortedRunSet MergePass(const SortedRunSet& input, std::size_t fan_in);
  HeapFile* NewTempHeap(const std::string& suffix);

  Operator* child_;
  std::size_t sort_col_;
  const Schema* temp_schema_;
  std::vector<std::string> output_names_;
  std::size_t buffer_pages_;
  BufferPool* bp_;
  DiskManager* dm_;
  std::string temp_path_prefix_;
  int temp_counter_ = 0;
  std::vector<std::unique_ptr<HeapFile>> heap_files_;
};

}
