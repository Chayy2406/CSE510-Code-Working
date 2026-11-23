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

// A sorted run: a contiguous page range [start, end) of `heap`, already
// sorted on sort_col. A SortedRunSet is one or more such runs, in their
// original relative order (needed for stable-sort tie-breaking: on equal
// keys, the earliest run wins).
struct SortedRunSet {
  HeapFile* heap = nullptr;
  std::vector<std::pair<PageNo, PageNo>> runs;
  std::size_t sort_col = 0;
};

// Pull-based k-way merge over a SortedRunSet's remaining runs: at each
// step, emits the smallest key across all active run cursors, breaking
// ties by the lowest run index (preserving stability through repeated
// merges). This is the "final merge" that SMJ fuses directly into its
// join loop instead of materializing to disk.
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

// Run formation (SPEC.md section 6): reads `child` fully, forming sorted
// runs of buffer_pages pages each, then shrinks the run count via
// (buffer_pages - 1)-way merge-and-rewrite passes until at most
// `max_runs` remain (PrepareSortedRuns), or down to exactly one run
// (Sort(), the fully materialized result). Ties are stable throughout:
// run formation uses std::stable_sort, and merges always prefer the
// lowest-indexed run on equal keys.
class ExternalSort {
 public:
  // child, temp_schema, bp, dm must outlive this ExternalSort and any
  // SortedRunSet/FusedMerge built from it: the HeapFiles it creates for
  // runs and merge passes are owned here.
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

}  // namespace qe
