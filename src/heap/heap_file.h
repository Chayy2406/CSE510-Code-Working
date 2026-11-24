#pragma once

#include <functional>

#include "common/types.h"
#include "heap/schema.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace qe {

class HeapFile {
 public:

  HeapFile(BufferPool* bp, DiskManager* dm, FileId file, const Schema* schema);

  Rid Append(const Tuple& tuple);

  Tuple Fetch(Rid rid) const;

  void Scan(const std::function<void(Rid, const Tuple&)>& fn) const;

  void ScanPage(PageNo page_no, const std::function<void(Rid, const Tuple&)>& fn) const;

  bool NextTuple(PageNo* page_no, std::int32_t* slot, Tuple* out) const;

  std::size_t NumPages() const { return page_count_; }
  std::size_t TuplesPerPage() const { return tuples_per_page_; }
  FileId Id() const { return file_; }

 private:
  static constexpr std::size_t kHeaderBytes = sizeof(std::int32_t);

  BufferPool* bp_;
  DiskManager* dm_;
  FileId file_;
  const Schema* schema_;
  std::size_t tuple_bytes_;
  std::size_t tuples_per_page_;

  std::size_t page_count_;
  PageNo last_page_no_;
  std::int32_t last_page_count_;
};

}
