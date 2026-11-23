#include "exec/external_sort.h"

#include <filesystem>

namespace qe {

// ---------------------------------------------------------------------
// FusedMerge
// ---------------------------------------------------------------------

FusedMerge::FusedMerge(SortedRunSet runs, std::vector<std::string> schema)
    : runs_(std::move(runs)), schema_(std::move(schema)) {}

void FusedMerge::RunCursor::LoadPage() {
  page_tuples.clear();
  idx = 0;
  valid = false;
  while (page < end) {
    heap->ScanPage(page, [&](Rid, const Tuple& t) { page_tuples.push_back(t); });
    if (!page_tuples.empty()) break;
    ++page;
  }
  if (page < end && !page_tuples.empty()) {
    valid = true;
    current = page_tuples[0];
  }
}

void FusedMerge::RunCursor::Advance() {
  ++idx;
  if (idx < page_tuples.size()) {
    current = page_tuples[idx];
    return;
  }
  ++page;
  LoadPage();
}

void FusedMerge::Open() {
  cursors_.clear();
  cursors_.reserve(runs_.runs.size());
  for (const auto& r : runs_.runs) {
    RunCursor c;
    c.heap = runs_.heap;
    c.page = r.first;
    c.end = r.second;
    c.LoadPage();
    cursors_.push_back(std::move(c));
  }
}

bool FusedMerge::Next(Tuple* out) {
  int best = -1;
  for (std::size_t i = 0; i < cursors_.size(); ++i) {
    if (!cursors_[i].valid) continue;
    if (best == -1 ||
        cursors_[i].current[runs_.sort_col] < cursors_[best].current[runs_.sort_col]) {
      best = static_cast<int>(i);
    }
  }
  if (best == -1) return false;
  *out = cursors_[best].current;
  cursors_[best].Advance();
  return true;
}

// ---------------------------------------------------------------------
// ExternalSort
// ---------------------------------------------------------------------

ExternalSort::ExternalSort(Operator* child, std::size_t sort_col,
                            const Schema* temp_schema,
                            std::vector<std::string> output_names,
                            std::size_t buffer_pages, BufferPool* bp,
                            DiskManager* dm, std::string temp_path_prefix)
    : child_(child),
      sort_col_(sort_col),
      temp_schema_(temp_schema),
      output_names_(std::move(output_names)),
      buffer_pages_(buffer_pages),
      bp_(bp),
      dm_(dm),
      temp_path_prefix_(std::move(temp_path_prefix)) {}

HeapFile* ExternalSort::NewTempHeap(const std::string& suffix) {
  std::string path =
      temp_path_prefix_ + "_" + suffix + "_" + std::to_string(temp_counter_++);
  // Temp files are always meant to start empty. Without this, reusing the
  // same temp_path_prefix across two separate ExternalSort invocations
  // (e.g. the same query run twice, or SMJ/GHJ invoked more than once in
  // a process) would make the new run see the previous run's leftover
  // pages as already-allocated and append on top of them instead of
  // starting fresh, corrupting the sort.
  std::filesystem::remove(path);
  FileId file = dm_->OpenFile(path, FileKind::kTemp);
  heap_files_.push_back(std::make_unique<HeapFile>(bp_, dm_, file, temp_schema_));
  return heap_files_.back().get();
}

SortedRunSet ExternalSort::FormRuns() {
  HeapFile* heap = NewTempHeap("runs");
  std::size_t tuples_per_page = heap->TuplesPerPage();
  std::size_t run_capacity = buffer_pages_ * tuples_per_page;
  if (run_capacity == 0) run_capacity = 1;

  SortedRunSet result;
  result.heap = heap;
  result.sort_col = sort_col_;

  child_->Open();
  Tuple t;
  bool more = child_->Next(&t);
  std::vector<Tuple> batch;
  batch.reserve(run_capacity);
  while (more) {
    batch.clear();
    while (batch.size() < run_capacity && more) {
      batch.push_back(t);
      more = child_->Next(&t);
    }
    std::stable_sort(batch.begin(), batch.end(),
                      [this](const Tuple& a, const Tuple& b) {
                        return a[sort_col_] < b[sort_col_];
                      });
    PageNo start = static_cast<PageNo>(heap->NumPages());
    for (const Tuple& bt : batch) heap->Append(bt);
    PageNo end = static_cast<PageNo>(heap->NumPages());
    result.runs.push_back({start, end});
  }
  child_->Close();
  return result;
}

SortedRunSet ExternalSort::MergePass(const SortedRunSet& input, std::size_t fan_in) {
  HeapFile* out_heap = NewTempHeap("merge");
  SortedRunSet result;
  result.heap = out_heap;
  result.sort_col = input.sort_col;

  for (std::size_t i = 0; i < input.runs.size(); i += fan_in) {
    std::size_t group_end = std::min(i + fan_in, input.runs.size());
    SortedRunSet group;
    group.heap = input.heap;
    group.sort_col = input.sort_col;
    group.runs.assign(input.runs.begin() + i, input.runs.begin() + group_end);

    FusedMerge merger(group, output_names_);
    merger.Open();
    PageNo start = static_cast<PageNo>(out_heap->NumPages());
    Tuple t;
    while (merger.Next(&t)) out_heap->Append(t);
    merger.Close();
    PageNo end = static_cast<PageNo>(out_heap->NumPages());
    result.runs.push_back({start, end});
  }
  return result;
}

SortedRunSet ExternalSort::PrepareSortedRuns(std::size_t max_runs) {
  SortedRunSet current = FormRuns();
  // Flush run formation's output now rather than leaving it to whatever
  // eviction happens to occur next: run formation's "P writes" must be
  // attributed to run formation itself, not folded into (or hidden by)
  // unrelated eviction pressure from whatever else shares this pool.
  bp_->FlushAll();

  // fan_in must be at least 2: merging groups of 1 run leaves the run
  // count unchanged, so the loop below would never terminate.
  std::size_t fan_in = buffer_pages_ > 2 ? buffer_pages_ - 1 : 2;
  while (current.runs.size() > max_runs) {
    HeapFile* superseded = current.heap;
    current = MergePass(current, fan_in);
    // Bounds the number of simultaneously open temp files across a long
    // chain of merge passes (otherwise a tiny buffer_pages forces enough
    // passes to exhaust the process's fd limit). Flush first: a page of
    // `superseded` may still be dirty and resident if it was never
    // evicted before this pass finished reading it.
    bp_->FlushAll();
    dm_->CloseFile(superseded->Id());
  }
  return current;
}

}  // namespace qe
