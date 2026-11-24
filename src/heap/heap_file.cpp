#include "heap/heap_file.h"

#include <cstring>
#include <stdexcept>

namespace qe {

namespace {

std::int32_t ReadTupleCount(const Page* page) {
  std::int32_t count;
  std::memcpy(&count, page->Data(), sizeof(count));
  return count;
}

void WriteTupleCount(Page* page, std::int32_t count) {
  std::memcpy(page->Data(), &count, sizeof(count));
}

}

HeapFile::HeapFile(BufferPool* bp, DiskManager* dm, FileId file,
                    const Schema* schema)
    : bp_(bp),
      dm_(dm),
      file_(file),
      schema_(schema),
      tuple_bytes_(schema->TupleBytes()),
      tuples_per_page_((kPageSize - kHeaderBytes) / tuple_bytes_),
      page_count_(dm_->PageCount(file)),
      last_page_no_(kInvalidPageNo),
      last_page_count_(0) {
  if (tuples_per_page_ == 0) {
    throw std::invalid_argument("HeapFile: tuple_bytes too large for a page");
  }
  if (page_count_ > 0) {
    last_page_no_ = static_cast<PageNo>(page_count_ - 1);
    Page* page = bp_->Fetch(file_, last_page_no_);
    last_page_count_ = ReadTupleCount(page);
    bp_->Unpin(file_, last_page_no_,  false);
  }
}

Rid HeapFile::Append(const Tuple& tuple) {
  if (tuple.size() != schema_->NumColumns()) {
    throw std::invalid_argument("HeapFile::Append: wrong column count");
  }

  bool need_new_page =
      last_page_no_ == kInvalidPageNo ||
      static_cast<std::size_t>(last_page_count_) >= tuples_per_page_;

  Page* page;
  if (need_new_page) {
    PageNo page_no;
    page = bp_->NewPage(file_, &page_no);
    last_page_no_ = page_no;
    last_page_count_ = 0;
    ++page_count_;
    WriteTupleCount(page, 0);
  } else {
    page = bp_->Fetch(file_, last_page_no_);
  }

  std::size_t offset = kHeaderBytes +
                        static_cast<std::size_t>(last_page_count_) * tuple_bytes_;
  std::memset(page->Data() + offset, 0, tuple_bytes_);
  std::memcpy(page->Data() + offset, tuple.data(),
              tuple.size() * sizeof(std::int32_t));

  Rid rid{last_page_no_, last_page_count_};
  ++last_page_count_;
  WriteTupleCount(page, last_page_count_);

  bp_->Unpin(file_, last_page_no_,  true);
  return rid;
}

Tuple HeapFile::Fetch(Rid rid) const {
  Page* page = bp_->Fetch(file_, rid.page_no);
  std::int32_t count = ReadTupleCount(page);
  if (rid.slot < 0 || rid.slot >= count) {
    bp_->Unpin(file_, rid.page_no, false);
    throw std::out_of_range("HeapFile::Fetch: invalid slot");
  }
  std::size_t offset =
      kHeaderBytes + static_cast<std::size_t>(rid.slot) * tuple_bytes_;
  Tuple tuple(schema_->NumColumns());
  std::memcpy(tuple.data(), page->Data() + offset,
              tuple.size() * sizeof(std::int32_t));
  bp_->Unpin(file_, rid.page_no, false);
  return tuple;
}

void HeapFile::Scan(const std::function<void(Rid, const Tuple&)>& fn) const {
  for (std::size_t p = 0; p < page_count_; ++p) {
    ScanPage(static_cast<PageNo>(p), fn);
  }
}

void HeapFile::ScanPage(PageNo page_no,
                         const std::function<void(Rid, const Tuple&)>& fn) const {
  Page* page = bp_->Fetch(file_, page_no);
  std::int32_t count = ReadTupleCount(page);
  for (std::int32_t slot = 0; slot < count; ++slot) {
    std::size_t offset =
        kHeaderBytes + static_cast<std::size_t>(slot) * tuple_bytes_;
    Tuple tuple(schema_->NumColumns());
    std::memcpy(tuple.data(), page->Data() + offset,
                tuple.size() * sizeof(std::int32_t));
    fn(Rid{page_no, slot}, tuple);
  }
  bp_->Unpin(file_, page_no, false);
}

bool HeapFile::NextTuple(PageNo* page_no, std::int32_t* slot, Tuple* out) const {
  while (static_cast<std::size_t>(*page_no) < page_count_) {
    Page* page = bp_->Fetch(file_, *page_no);
    std::int32_t count = ReadTupleCount(page);
    if (*slot >= count) {
      bp_->Unpin(file_, *page_no, false);
      ++(*page_no);
      *slot = 0;
      continue;
    }
    std::size_t offset =
        kHeaderBytes + static_cast<std::size_t>(*slot) * tuple_bytes_;
    out->resize(schema_->NumColumns());
    std::memcpy(out->data(), page->Data() + offset,
                out->size() * sizeof(std::int32_t));
    bp_->Unpin(file_, *page_no, false);
    ++(*slot);
    return true;
  }
  return false;
}

}
