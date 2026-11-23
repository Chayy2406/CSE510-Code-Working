#pragma once

#include "exec/operator.h"
#include "exec/predicate.h"
#include "heap/heap_file.h"

namespace qe {

// Block nested loop join directly over two heap files (SPEC.md section 6).
// Reads the outer in blocks of `buffer_pages - 2` pages; for each block,
// rescans the entire inner table. Matches are joined on
// outer_tuple[outer_join_col] == inner_tuple[inner_join_col]; the output
// schema is outer columns followed by inner columns.
//
// Cost: every outer page is read exactly once across all blocks (M total);
// the inner is rescanned once per block (N * ceil(M/(buffer_pages-2))
// total), matching M + ceil(M/(B-2))*N.
//
// Implementation note: each block's matching outer tuples are decoded into
// a private in-memory vector sized to the block (buffer_pages - 2 pages),
// rather than holding that many pages physically pinned in the
// BufferPool — only one page is ever pinned at a time. Page I/O counts
// are identical to the textbook pinned-block version; only the peak pin
// count differs (documented in docs/DECISIONS.md).
class BNLJ : public Operator {
 public:
  BNLJ(HeapFile* outer, std::vector<std::string> outer_names,
       std::vector<Predicate> outer_predicates, std::size_t outer_join_col,
       HeapFile* inner, std::vector<std::string> inner_names,
       std::vector<Predicate> inner_predicates, std::size_t inner_join_col,
       std::size_t buffer_pages)
      : outer_(outer),
        inner_(inner),
        outer_predicates_(std::move(outer_predicates)),
        outer_join_col_(outer_join_col),
        inner_predicates_(std::move(inner_predicates)),
        inner_join_col_(inner_join_col),
        buffer_pages_(buffer_pages) {
    schema_ = outer_names;
    schema_.insert(schema_.end(), inner_names.begin(), inner_names.end());
    outer_cols_ = outer_names.size();
    inner_cols_ = inner_names.size();
  }

  void Open() override {
    next_outer_page_ = 0;
    output_queue_.clear();
    output_pos_ = 0;
  }

  bool Next(Tuple* out) override {
    while (output_pos_ >= output_queue_.size()) {
      if (next_outer_page_ >= outer_->NumPages()) return false;
      LoadNextBlock();
      output_pos_ = 0;
    }
    *out = output_queue_[output_pos_++];
    return true;
  }

  void Close() override {}

  const std::vector<std::string>& OutputSchema() const override { return schema_; }

 private:
  void LoadNextBlock() {
    output_queue_.clear();

    std::size_t block_pages = buffer_pages_ > 2 ? buffer_pages_ - 2 : 1;
    std::size_t start_page = next_outer_page_;
    std::size_t end_page = std::min(start_page + block_pages, outer_->NumPages());
    next_outer_page_ = end_page;

    std::vector<Tuple> block_tuples;
    for (std::size_t p = start_page; p < end_page; ++p) {
      outer_->ScanPage(static_cast<PageNo>(p), [&](Rid, const Tuple& t) {
        if (EvaluateAll(outer_predicates_, t)) block_tuples.push_back(t);
      });
    }

    // The inner is rescanned in full for this block regardless of how
    // many outer tuples survived pushdown: the cost model counts N page
    // reads per block independent of predicate selectivity.
    for (std::size_t p = 0; p < inner_->NumPages(); ++p) {
      inner_->ScanPage(static_cast<PageNo>(p), [&](Rid, const Tuple& inner_t) {
        if (!EvaluateAll(inner_predicates_, inner_t)) return;
        for (const Tuple& outer_t : block_tuples) {
          if (outer_t[outer_join_col_] == inner_t[inner_join_col_]) {
            Tuple combined;
            combined.reserve(outer_cols_ + inner_cols_);
            combined.insert(combined.end(), outer_t.begin(), outer_t.end());
            combined.insert(combined.end(), inner_t.begin(), inner_t.end());
            output_queue_.push_back(std::move(combined));
          }
        }
      });
    }
  }

  HeapFile* outer_;
  HeapFile* inner_;
  std::vector<Predicate> outer_predicates_;
  std::size_t outer_join_col_;
  std::vector<Predicate> inner_predicates_;
  std::size_t inner_join_col_;
  std::size_t buffer_pages_;

  std::vector<std::string> schema_;
  std::size_t outer_cols_ = 0;
  std::size_t inner_cols_ = 0;

  std::size_t next_outer_page_ = 0;
  std::vector<Tuple> output_queue_;
  std::size_t output_pos_ = 0;
};

}  // namespace qe
