#include "exec/smj.h"

namespace qe {

SMJ::SMJ(Operator* left, std::size_t left_col, const Schema* left_temp_schema,
         std::vector<std::string> left_names, Operator* right, std::size_t right_col,
         const Schema* right_temp_schema, std::vector<std::string> right_names,
         std::size_t buffer_pages, BufferPool* bp, DiskManager* dm,
         std::string temp_path_prefix)
    : left_(left),
      left_col_(left_col),
      left_temp_schema_(left_temp_schema),
      left_names_(std::move(left_names)),
      right_(right),
      right_col_(right_col),
      right_temp_schema_(right_temp_schema),
      right_names_(std::move(right_names)),
      buffer_pages_(buffer_pages),
      bp_(bp),
      dm_(dm),
      temp_path_prefix_(std::move(temp_path_prefix)) {
  schema_ = left_names_;
  schema_.insert(schema_.end(), right_names_.begin(), right_names_.end());
}

void SMJ::Open() {
  left_sorter_ = std::make_unique<ExternalSort>(
      left_, left_col_, left_temp_schema_, left_names_, buffer_pages_, bp_, dm_,
      temp_path_prefix_ + "_smj_left");
  right_sorter_ = std::make_unique<ExternalSort>(
      right_, right_col_, right_temp_schema_, right_names_, buffer_pages_, bp_, dm_,
      temp_path_prefix_ + "_smj_right");

  std::size_t fan_in = buffer_pages_ > 1 ? buffer_pages_ - 1 : 1;
  SortedRunSet left_runs = left_sorter_->PrepareSortedRuns(fan_in);
  SortedRunSet right_runs = right_sorter_->PrepareSortedRuns(fan_in);

  left_merge_ = std::make_unique<FusedMerge>(left_runs, left_names_);
  right_merge_ = std::make_unique<FusedMerge>(right_runs, right_names_);
  left_merge_->Open();
  right_merge_->Open();

  left_valid_ = left_merge_->Next(&left_cur_);
  right_valid_ = right_merge_->Next(&right_cur_);

  output_queue_.clear();
  output_pos_ = 0;
  spill_heap_.reset();
  spill_counter_ = 0;
}

void SMJ::AdvanceLeft() { left_valid_ = left_merge_->Next(&left_cur_); }
void SMJ::AdvanceRight() { right_valid_ = right_merge_->Next(&right_cur_); }

bool SMJ::AdvanceState() {
  while (left_valid_ && right_valid_) {
    std::int32_t lk = left_cur_[left_col_];
    std::int32_t rk = right_cur_[right_col_];
    if (lk < rk) {
      AdvanceLeft();
      continue;
    }
    if (lk > rk) {
      AdvanceRight();
      continue;
    }
    ProcessEqualBlock(lk);
    output_pos_ = 0;
    return true;
  }
  return false;
}

void SMJ::ProcessEqualBlock(std::int32_t key) {
  output_queue_.clear();
  left_group_mem_.clear();
  spill_heap_.reset();
  bool spilled = false;

  std::size_t tuples_per_page =
      (kPageSize - sizeof(std::int32_t)) / left_temp_schema_->TupleBytes();
  std::size_t group_capacity = buffer_pages_ * tuples_per_page;
  if (group_capacity == 0) group_capacity = 1;

  while (left_valid_ && left_cur_[left_col_] == key) {
    if (spilled) {
      spill_heap_->Append(left_cur_);
    } else {
      left_group_mem_.push_back(left_cur_);
      if (left_group_mem_.size() > group_capacity) {
        spilled = true;
        std::string path =
            temp_path_prefix_ + "_smj_dupgroup_" + std::to_string(spill_counter_++);
        FileId f = dm_->OpenFile(path, FileKind::kTemp);
        spill_heap_ = std::make_unique<HeapFile>(bp_, dm_, f, left_temp_schema_);
        for (const Tuple& t : left_group_mem_) spill_heap_->Append(t);
        left_group_mem_.clear();
      }
    }
    AdvanceLeft();
  }

  while (right_valid_ && right_cur_[right_col_] == key) {
    if (!spilled) {
      for (const Tuple& lt : left_group_mem_) {
        output_queue_.push_back(Combine(lt, right_cur_));
      }
    } else {
      spill_heap_->Scan(
          [&](Rid, const Tuple& lt) { output_queue_.push_back(Combine(lt, right_cur_)); });
    }
    AdvanceRight();
  }
}

Tuple SMJ::Combine(const Tuple& l, const Tuple& r) const {
  Tuple out;
  out.reserve(l.size() + r.size());
  out.insert(out.end(), l.begin(), l.end());
  out.insert(out.end(), r.begin(), r.end());
  return out;
}

bool SMJ::Next(Tuple* out) {
  while (output_pos_ >= output_queue_.size()) {
    if (!AdvanceState()) return false;
  }
  *out = output_queue_[output_pos_++];
  return true;
}

void SMJ::Close() {
  left_merge_.reset();
  right_merge_.reset();
  left_sorter_.reset();
  right_sorter_.reset();
  spill_heap_.reset();
}

}
