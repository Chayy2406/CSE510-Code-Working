#pragma once

#include <functional>

#include "common/types.h"
#include "heap/schema.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

// A heap file of fixed-width tuples, packed back-to-back after a small
// per-page header (one int32 tuple count). All page access goes through
// the BufferPool, so every real read/write is counted there (SPEC.md
// section 4). Appends are sequential; there is no delete, matching the
// read-mostly, generate-once workload of this project.
class HeapFile {
 public:
  // bp and dm must outlive this HeapFile. `file` must already be open in
  // dm (via DiskManager::OpenFile), either freshly created or an existing
  // heap file written by a previous HeapFile on the same layout.
  HeapFile(BufferPool* bp, DiskManager* dm, FileId file, const Schema* schema);

  // Appends one tuple, allocating a new page if the current last page is
  // full. tuple.size() must equal schema->NumColumns().
  Rid Append(const Tuple& tuple);

  // Reads back the tuple at rid.
  Tuple Fetch(Rid rid) const;

  // Calls fn(rid, tuple) for every tuple currently in the file, in
  // (page_no, slot) order.
  void Scan(const std::function<void(Rid, const Tuple&)>& fn) const;

  std::size_t NumPages() const { return page_count_; }
  std::size_t TuplesPerPage() const { return tuples_per_page_; }

 private:
  static constexpr std::size_t kHeaderBytes = sizeof(std::int32_t);

  BufferPool* bp_;
  DiskManager* dm_;
  FileId file_;
  const Schema* schema_;
  std::size_t tuple_bytes_;
  std::size_t tuples_per_page_;

  std::size_t page_count_;       // total pages ever allocated for this file
  PageNo last_page_no_;          // kInvalidPageNo if the file is empty
  std::int32_t last_page_count_; // tuples currently in last_page_no_
};

}  // namespace qe
