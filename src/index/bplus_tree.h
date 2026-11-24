#pragma once

#include <functional>
#include <optional>

#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

class BPlusTree {
 public:

  BPlusTree(BufferPool* bp, DiskManager* dm, FileId file);

  void Insert(std::int32_t key, Rid rid);

  void RangeScan(std::int32_t lo, std::int32_t hi,
                 const std::function<void(std::int32_t, Rid)>& fn) const;

  void EqualityScan(std::int32_t key, const std::function<void(Rid)>& fn) const;

  int Height() const { return height_; }
  bool Empty() const { return root_page_ == kInvalidPageNo; }

 private:
  struct SplitResult {
    std::int32_t promoted_key;
    PageNo new_page;
  };

  std::optional<SplitResult> InsertRec(PageNo node_page, std::int32_t key, Rid rid);
  std::optional<SplitResult> InsertIntoLeaf(PageNo page_no, std::int32_t key, Rid rid);
  std::optional<SplitResult> InsertIntoInternal(PageNo page_no, std::int32_t promoted_key,
                                                 PageNo right_child);
  PageNo FindLeafForKey(std::int32_t key) const;
  void WriteMeta();

  BufferPool* bp_;
  DiskManager* dm_;
  FileId file_;
  PageNo root_page_;
  int height_;
};

}
