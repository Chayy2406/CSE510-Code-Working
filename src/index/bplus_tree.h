#pragma once

#include <functional>
#include <optional>

#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

// A paged B+ tree mapping int32 keys to Rids, with duplicate keys
// supported (SPEC.md section 2/9). Page 0 of `file` is a small meta page
// holding the root page number and tree height; all other pages are
// leaf or internal nodes, distinguished by a flag in their first 4
// bytes. Construction reads an existing meta page if `file` already has
// data, or creates one for a brand-new file — matching HeapFile's
// reopen-an-existing-file behavior.
//
// Known limitation: a leaf split picks its boundary so it never falls in
// the middle of a run of equal keys (needed so an internal separator key
// is a correct upper bound for its left subtree). If a single key's
// duplicate count alone exceeds one leaf's capacity, that invariant
// cannot be preserved; this does not occur for any key used in this
// project's tests or INLJ usage (see docs/DECISIONS.md).
class BPlusTree {
 public:
  // bp and dm must outlive this BPlusTree. `file` must already be open in
  // dm (via DiskManager::OpenFile) with FileKind::kIndex.
  BPlusTree(BufferPool* bp, DiskManager* dm, FileId file);

  void Insert(std::int32_t key, Rid rid);

  // Calls fn(key, rid) for every entry with lo <= key <= hi, in key order
  // (ties in insertion order).
  void RangeScan(std::int32_t lo, std::int32_t hi,
                 const std::function<void(std::int32_t, Rid)>& fn) const;

  // Calls fn(rid) for every entry with this exact key.
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

}  // namespace qe
