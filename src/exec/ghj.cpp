#include "exec/ghj.h"

#include <filesystem>
#include <unordered_map>

namespace qe {

GHJ::GHJ(Operator* left, std::size_t left_col, const Schema* left_temp_schema,
         std::vector<std::string> left_names, Operator* right, std::size_t right_col,
         const Schema* right_temp_schema, std::vector<std::string> right_names,
         std::size_t buffer_pages, BufferPool* bp, DiskManager* dm,
         std::string temp_path_prefix, IoStats* stats)
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
      temp_path_prefix_(std::move(temp_path_prefix)),
      stats_(stats) {
  schema_ = left_names_;
  schema_.insert(schema_.end(), right_names_.begin(), right_names_.end());
}

std::size_t GHJ::Hash(std::int32_t key, int seed, std::size_t num_partitions) {
  std::uint64_t h = static_cast<std::uint32_t>(key);
  h ^= 0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(seed) * 0xbf58476d1ce4e5b9ULL;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;
  return static_cast<std::size_t>(h % num_partitions);
}

Tuple GHJ::Combine(const Tuple& l, const Tuple& r) const {
  Tuple out;
  out.reserve(l.size() + r.size());
  out.insert(out.end(), l.begin(), l.end());
  out.insert(out.end(), r.begin(), r.end());
  return out;
}

HeapFile* GHJ::NewTempHeap(const Schema* schema, const std::string& suffix) {
  std::string path = temp_path_prefix_ + "_ghj_" + suffix + "_" +
                      std::to_string(temp_counter_++);

  std::filesystem::remove(path);
  FileId file = dm_->OpenFile(path, FileKind::kTemp);
  heap_files_.push_back(std::make_unique<HeapFile>(bp_, dm_, file, schema));
  return heap_files_.back().get();
}

std::vector<GHJ::PartitionPair> GHJ::PartitionFromOperator(int seed) {
  std::size_t num_partitions = buffer_pages_ > 1 ? buffer_pages_ - 1 : 1;
  std::vector<HeapFile*> left_parts(num_partitions);
  std::vector<HeapFile*> right_parts(num_partitions);
  for (std::size_t i = 0; i < num_partitions; ++i) {
    left_parts[i] = NewTempHeap(left_temp_schema_, "L_d0_" + std::to_string(i));
    right_parts[i] = NewTempHeap(right_temp_schema_, "R_d0_" + std::to_string(i));
  }

  left_->Open();
  Tuple t;
  while (left_->Next(&t)) {
    std::size_t b = Hash(t[left_col_], seed, num_partitions);
    left_parts[b]->Append(t);
  }
  left_->Close();

  right_->Open();
  while (right_->Next(&t)) {
    std::size_t b = Hash(t[right_col_], seed, num_partitions);
    right_parts[b]->Append(t);
  }
  right_->Close();

  std::vector<PartitionPair> result;
  result.reserve(num_partitions);
  for (std::size_t i = 0; i < num_partitions; ++i) {
    result.push_back({left_parts[i], 0, static_cast<PageNo>(left_parts[i]->NumPages()),
                       right_parts[i], 0,
                       static_cast<PageNo>(right_parts[i]->NumPages()), 1});
  }
  return result;
}

std::vector<GHJ::PartitionPair> GHJ::PartitionFromHeap(const PartitionPair& p, int seed) {
  std::size_t num_partitions = buffer_pages_ > 1 ? buffer_pages_ - 1 : 1;
  std::string tag = "d" + std::to_string(p.depth) + "_s" + std::to_string(seed);
  std::vector<HeapFile*> left_parts(num_partitions);
  std::vector<HeapFile*> right_parts(num_partitions);
  for (std::size_t i = 0; i < num_partitions; ++i) {
    left_parts[i] = NewTempHeap(left_temp_schema_, "L_" + tag + "_" + std::to_string(i));
    right_parts[i] = NewTempHeap(right_temp_schema_, "R_" + tag + "_" + std::to_string(i));
  }

  for (PageNo pg = p.left_start; pg < p.left_end; ++pg) {
    p.left_heap->ScanPage(pg, [&](Rid, const Tuple& t) {
      std::size_t b = Hash(t[left_col_], seed, num_partitions);
      left_parts[b]->Append(t);
    });
  }
  for (PageNo pg = p.right_start; pg < p.right_end; ++pg) {
    p.right_heap->ScanPage(pg, [&](Rid, const Tuple& t) {
      std::size_t b = Hash(t[right_col_], seed, num_partitions);
      right_parts[b]->Append(t);
    });
  }

  std::vector<PartitionPair> result;
  result.reserve(num_partitions);
  for (std::size_t i = 0; i < num_partitions; ++i) {
    result.push_back({left_parts[i], 0, static_cast<PageNo>(left_parts[i]->NumPages()),
                       right_parts[i], 0,
                       static_cast<PageNo>(right_parts[i]->NumPages()), p.depth + 1});
  }
  return result;
}

void GHJ::RunHashJoin(const PartitionPair& p, bool build_left) {
  output_queue_.clear();
  output_pos_ = 0;

  HeapFile* build_heap = build_left ? p.left_heap : p.right_heap;
  PageNo build_start = build_left ? p.left_start : p.right_start;
  PageNo build_end = build_left ? p.left_end : p.right_end;
  std::size_t build_col = build_left ? left_col_ : right_col_;

  HeapFile* probe_heap = build_left ? p.right_heap : p.left_heap;
  PageNo probe_start = build_left ? p.right_start : p.left_start;
  PageNo probe_end = build_left ? p.right_end : p.left_end;
  std::size_t probe_col = build_left ? right_col_ : left_col_;

  std::unordered_multimap<std::int32_t, Tuple> table;
  for (PageNo pg = build_start; pg < build_end; ++pg) {
    build_heap->ScanPage(pg,
                          [&](Rid, const Tuple& t) { table.emplace(t[build_col], t); });
  }
  for (PageNo pg = probe_start; pg < probe_end; ++pg) {
    probe_heap->ScanPage(pg, [&](Rid, const Tuple& probe_t) {
      auto range = table.equal_range(probe_t[probe_col]);
      for (auto it = range.first; it != range.second; ++it) {
        if (build_left) {
          output_queue_.push_back(Combine(it->second, probe_t));
        } else {
          output_queue_.push_back(Combine(probe_t, it->second));
        }
      }
    });
  }
}

void GHJ::RunFallbackJoin(const PartitionPair& p) {
  output_queue_.clear();
  output_pos_ = 0;

  std::size_t left_pages = static_cast<std::size_t>(p.left_end - p.left_start);
  std::size_t right_pages = static_cast<std::size_t>(p.right_end - p.right_start);
  bool buffer_left = left_pages <= right_pages;

  HeapFile* buffer_heap = buffer_left ? p.left_heap : p.right_heap;
  PageNo buffer_start = buffer_left ? p.left_start : p.right_start;
  PageNo buffer_end = buffer_left ? p.left_end : p.right_end;
  std::size_t buffer_col = buffer_left ? left_col_ : right_col_;

  HeapFile* scan_heap = buffer_left ? p.right_heap : p.left_heap;
  PageNo scan_start = buffer_left ? p.right_start : p.left_start;
  PageNo scan_end = buffer_left ? p.right_end : p.left_end;
  std::size_t scan_col = buffer_left ? right_col_ : left_col_;

  std::vector<Tuple> buffered;
  for (PageNo pg = buffer_start; pg < buffer_end; ++pg) {
    buffer_heap->ScanPage(pg, [&](Rid, const Tuple& t) { buffered.push_back(t); });
  }
  for (PageNo pg = scan_start; pg < scan_end; ++pg) {
    scan_heap->ScanPage(pg, [&](Rid, const Tuple& scan_t) {
      for (const Tuple& bt : buffered) {
        if (bt[buffer_col] == scan_t[scan_col]) {
          if (buffer_left) {
            output_queue_.push_back(Combine(bt, scan_t));
          } else {
            output_queue_.push_back(Combine(scan_t, bt));
          }
        }
      }
    });
  }
}

void GHJ::ProcessNextPending() {
  PartitionPair p = pending_.front();
  pending_.pop_front();

  std::size_t left_pages = static_cast<std::size_t>(p.left_end - p.left_start);
  std::size_t right_pages = static_cast<std::size_t>(p.right_end - p.right_start);
  bool build_left = left_pages <= right_pages;
  std::size_t build_pages = build_left ? left_pages : right_pages;

  std::size_t budget = buffer_pages_ > 2 ? buffer_pages_ - 2 : 1;
  if (build_pages <= budget) {
    RunHashJoin(p, build_left);
  } else if (p.depth >= kMaxDepth) {
    ++stats_->ghj_overflow_partitions;
    RunFallbackJoin(p);
  } else {
    std::vector<PartitionPair> sub = PartitionFromHeap(p, p.depth + 1);
    for (auto& sp : sub) pending_.push_back(sp);
  }

  bp_->FlushAll();
  dm_->CloseFile(p.left_heap->Id());
  dm_->CloseFile(p.right_heap->Id());
}

void GHJ::Open() {
  pending_.clear();
  output_queue_.clear();
  output_pos_ = 0;
  heap_files_.clear();
  temp_counter_ = 0;

  std::vector<PartitionPair> top = PartitionFromOperator(0);
  for (auto& p : top) pending_.push_back(p);
}

bool GHJ::Next(Tuple* out) {
  while (output_pos_ >= output_queue_.size()) {
    if (pending_.empty()) return false;
    ProcessNextPending();
    output_pos_ = 0;
  }
  *out = output_queue_[output_pos_++];
  return true;
}

void GHJ::Close() {
  pending_.clear();
  heap_files_.clear();
}

}
