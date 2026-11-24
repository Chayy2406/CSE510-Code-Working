#include "index/bplus_tree.h"

#include <algorithm>
#include <cstring>

namespace qe {

namespace {

constexpr std::size_t kLeafHeaderBytes = 12;
constexpr std::size_t kLeafEntryBytes = 12;
constexpr std::size_t kMaxLeafEntries = (kPageSize - kLeafHeaderBytes) / kLeafEntryBytes;

constexpr std::size_t kInternalHeaderBytes = 8;

constexpr std::size_t kMaxInternalKeys =
    (kPageSize - kInternalHeaderBytes - sizeof(std::int32_t)) / (2 * sizeof(std::int32_t));
constexpr std::size_t kChildrenOffset = kInternalHeaderBytes;
constexpr std::size_t kKeysOffset =
    kInternalHeaderBytes + (kMaxInternalKeys + 1) * sizeof(std::int32_t);

static_assert(kKeysOffset + kMaxInternalKeys * sizeof(std::int32_t) <= kPageSize,
              "internal node layout must fit in one page");

struct LeafEntry {
  std::int32_t key;
  Rid rid;
};

bool IsLeaf(const Page* page) {
  std::int32_t flag;
  std::memcpy(&flag, page->Data(), sizeof(flag));
  return flag != 0;
}

void DecodeLeaf(const Page* page, std::vector<LeafEntry>* entries, PageNo* next_leaf) {
  std::int32_t count;
  std::memcpy(&count, page->Data() + 4, sizeof(count));
  std::memcpy(next_leaf, page->Data() + 8, sizeof(PageNo));
  entries->resize(static_cast<std::size_t>(count));
  const char* p = page->Data() + kLeafHeaderBytes;
  for (std::int32_t i = 0; i < count; ++i) {
    std::int32_t key, pg, slot;
    std::memcpy(&key, p, 4);
    p += 4;
    std::memcpy(&pg, p, 4);
    p += 4;
    std::memcpy(&slot, p, 4);
    p += 4;
    (*entries)[i] = LeafEntry{key, Rid{pg, slot}};
  }
}

void EncodeLeaf(Page* page, const std::vector<LeafEntry>& entries, PageNo next_leaf) {
  page->Zero();
  std::int32_t flag = 1;
  std::int32_t count = static_cast<std::int32_t>(entries.size());
  std::memcpy(page->Data(), &flag, 4);
  std::memcpy(page->Data() + 4, &count, 4);
  std::memcpy(page->Data() + 8, &next_leaf, 4);
  char* p = page->Data() + kLeafHeaderBytes;
  for (const LeafEntry& e : entries) {
    std::memcpy(p, &e.key, 4);
    p += 4;
    std::memcpy(p, &e.rid.page_no, 4);
    p += 4;
    std::memcpy(p, &e.rid.slot, 4);
    p += 4;
  }
}

void DecodeInternal(const Page* page, std::vector<std::int32_t>* keys,
                    std::vector<PageNo>* children) {
  std::int32_t count;
  std::memcpy(&count, page->Data() + 4, sizeof(count));
  children->resize(static_cast<std::size_t>(count) + 1);
  keys->resize(static_cast<std::size_t>(count));
  const char* cp = page->Data() + kChildrenOffset;
  for (std::int32_t i = 0; i <= count; ++i) {
    std::memcpy(&(*children)[i], cp, 4);
    cp += 4;
  }
  const char* kp = page->Data() + kKeysOffset;
  for (std::int32_t i = 0; i < count; ++i) {
    std::memcpy(&(*keys)[i], kp, 4);
    kp += 4;
  }
}

void EncodeInternal(Page* page, const std::vector<std::int32_t>& keys,
                     const std::vector<PageNo>& children) {
  page->Zero();
  std::int32_t flag = 0;
  std::int32_t count = static_cast<std::int32_t>(keys.size());
  std::memcpy(page->Data(), &flag, 4);
  std::memcpy(page->Data() + 4, &count, 4);
  char* cp = page->Data() + kChildrenOffset;
  for (PageNo c : children) {
    std::memcpy(cp, &c, 4);
    cp += 4;
  }
  char* kp = page->Data() + kKeysOffset;
  for (std::int32_t k : keys) {
    std::memcpy(kp, &k, 4);
    kp += 4;
  }
}

}

BPlusTree::BPlusTree(BufferPool* bp, DiskManager* dm, FileId file)
    : bp_(bp), dm_(dm), file_(file), root_page_(kInvalidPageNo), height_(0) {
  if (dm_->PageCount(file_) == 0) {
    PageNo meta_page;
    Page* meta = bp_->NewPage(file_, &meta_page);
    std::memcpy(meta->Data(), &root_page_, 4);
    std::memcpy(meta->Data() + 4, &height_, 4);
    bp_->Unpin(file_, meta_page, true);
  } else {
    Page* meta = bp_->Fetch(file_, 0);
    std::memcpy(&root_page_, meta->Data(), 4);
    std::memcpy(&height_, meta->Data() + 4, 4);
    bp_->Unpin(file_, 0, false);
  }
}

void BPlusTree::WriteMeta() {
  Page* meta = bp_->Fetch(file_, 0);
  std::memcpy(meta->Data(), &root_page_, 4);
  std::memcpy(meta->Data() + 4, &height_, 4);
  bp_->Unpin(file_, 0, true);
}

void BPlusTree::Insert(std::int32_t key, Rid rid) {
  if (root_page_ == kInvalidPageNo) {
    PageNo leaf_page;
    Page* leaf = bp_->NewPage(file_, &leaf_page);
    EncodeLeaf(leaf, {LeafEntry{key, rid}}, kInvalidPageNo);
    bp_->Unpin(file_, leaf_page, true);
    root_page_ = leaf_page;
    height_ = 1;
    WriteMeta();
    return;
  }

  std::optional<SplitResult> split = InsertRec(root_page_, key, rid);
  if (split) {
    PageNo new_root_page;
    Page* new_root = bp_->NewPage(file_, &new_root_page);
    EncodeInternal(new_root, {split->promoted_key}, {root_page_, split->new_page});
    bp_->Unpin(file_, new_root_page, true);
    root_page_ = new_root_page;
    ++height_;
    WriteMeta();
  }
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertRec(PageNo node_page,
                                                            std::int32_t key, Rid rid) {
  Page* page = bp_->Fetch(file_, node_page);
  bool leaf = IsLeaf(page);
  if (leaf) {
    bp_->Unpin(file_, node_page, false);
    return InsertIntoLeaf(node_page, key, rid);
  }

  std::vector<std::int32_t> keys;
  std::vector<PageNo> children;
  DecodeInternal(page, &keys, &children);
  bp_->Unpin(file_, node_page, false);

  std::size_t idx =
      static_cast<std::size_t>(std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
  PageNo child_page = children[idx];

  std::optional<SplitResult> child_split = InsertRec(child_page, key, rid);
  if (!child_split) return std::nullopt;
  return InsertIntoInternal(node_page, child_split->promoted_key, child_split->new_page);
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertIntoLeaf(PageNo page_no,
                                                                 std::int32_t key, Rid rid) {
  Page* page = bp_->Fetch(file_, page_no);
  std::vector<LeafEntry> entries;
  PageNo next_leaf;
  DecodeLeaf(page, &entries, &next_leaf);
  bp_->Unpin(file_, page_no, false);

  auto pos = std::upper_bound(
      entries.begin(), entries.end(), key,
      [](std::int32_t k, const LeafEntry& e) { return k < e.key; });
  entries.insert(pos, LeafEntry{key, rid});

  if (entries.size() <= kMaxLeafEntries) {
    Page* page2 = bp_->Fetch(file_, page_no);
    EncodeLeaf(page2, entries, next_leaf);
    bp_->Unpin(file_, page_no, true);
    return std::nullopt;
  }

  std::size_t mid = entries.size() / 2;
  while (mid < entries.size() && entries[mid].key == entries[mid - 1].key) ++mid;
  if (mid >= entries.size()) mid = entries.size() / 2;

  std::vector<LeafEntry> left(entries.begin(), entries.begin() + static_cast<long>(mid));
  std::vector<LeafEntry> right(entries.begin() + static_cast<long>(mid), entries.end());

  PageNo right_page;
  Page* right_page_ptr = bp_->NewPage(file_, &right_page);
  EncodeLeaf(right_page_ptr, right, next_leaf);
  bp_->Unpin(file_, right_page, true);

  Page* left_page_ptr = bp_->Fetch(file_, page_no);
  EncodeLeaf(left_page_ptr, left, right_page);
  bp_->Unpin(file_, page_no, true);

  return SplitResult{right.front().key, right_page};
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertIntoInternal(PageNo page_no,
                                                                     std::int32_t promoted_key,
                                                                     PageNo right_child) {
  Page* page = bp_->Fetch(file_, page_no);
  std::vector<std::int32_t> keys;
  std::vector<PageNo> children;
  DecodeInternal(page, &keys, &children);
  bp_->Unpin(file_, page_no, false);

  std::size_t idx = static_cast<std::size_t>(
      std::upper_bound(keys.begin(), keys.end(), promoted_key) - keys.begin());
  keys.insert(keys.begin() + static_cast<long>(idx), promoted_key);
  children.insert(children.begin() + static_cast<long>(idx) + 1, right_child);

  if (keys.size() <= kMaxInternalKeys) {
    Page* page2 = bp_->Fetch(file_, page_no);
    EncodeInternal(page2, keys, children);
    bp_->Unpin(file_, page_no, true);
    return std::nullopt;
  }

  std::size_t mid = keys.size() / 2;
  std::int32_t up_key = keys[mid];
  std::vector<std::int32_t> left_keys(keys.begin(), keys.begin() + static_cast<long>(mid));
  std::vector<std::int32_t> right_keys(keys.begin() + static_cast<long>(mid) + 1, keys.end());
  std::vector<PageNo> left_children(children.begin(),
                                     children.begin() + static_cast<long>(mid) + 1);
  std::vector<PageNo> right_children(children.begin() + static_cast<long>(mid) + 1,
                                      children.end());

  PageNo right_page;
  Page* right_page_ptr = bp_->NewPage(file_, &right_page);
  EncodeInternal(right_page_ptr, right_keys, right_children);
  bp_->Unpin(file_, right_page, true);

  Page* left_page_ptr = bp_->Fetch(file_, page_no);
  EncodeInternal(left_page_ptr, left_keys, left_children);
  bp_->Unpin(file_, page_no, true);

  return SplitResult{up_key, right_page};
}

PageNo BPlusTree::FindLeafForKey(std::int32_t key) const {
  PageNo node = root_page_;
  while (true) {
    Page* page = bp_->Fetch(file_, node);
    if (IsLeaf(page)) {
      bp_->Unpin(file_, node, false);
      return node;
    }
    std::vector<std::int32_t> keys;
    std::vector<PageNo> children;
    DecodeInternal(page, &keys, &children);
    bp_->Unpin(file_, node, false);
    std::size_t idx = static_cast<std::size_t>(
        std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
    node = children[idx];
  }
}

void BPlusTree::RangeScan(std::int32_t lo, std::int32_t hi,
                          const std::function<void(std::int32_t, Rid)>& fn) const {
  if (root_page_ == kInvalidPageNo) return;
  PageNo node = FindLeafForKey(lo);
  while (node != kInvalidPageNo) {
    Page* page = bp_->Fetch(file_, node);
    std::vector<LeafEntry> entries;
    PageNo next_leaf;
    DecodeLeaf(page, &entries, &next_leaf);
    bp_->Unpin(file_, node, false);

    bool done = false;
    for (const LeafEntry& e : entries) {
      if (e.key < lo) continue;
      if (e.key > hi) {
        done = true;
        break;
      }
      fn(e.key, e.rid);
    }
    if (done) break;
    node = next_leaf;
  }
}

void BPlusTree::EqualityScan(std::int32_t key,
                             const std::function<void(Rid)>& fn) const {
  RangeScan(key, key, [&fn](std::int32_t, Rid rid) { fn(rid); });
}

}
