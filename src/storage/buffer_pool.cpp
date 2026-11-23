#include "storage/buffer_pool.h"

namespace qe {

BufferPool::BufferPool(DiskManager* dm, std::size_t num_frames, IoStats* stats)
    : dm_(dm), stats_(stats), frames_(num_frames) {
  for (std::size_t i = 0; i < num_frames; ++i) free_frames_.push_back(i);
}

std::size_t BufferPool::AcquireFrame() {
  if (!free_frames_.empty()) {
    std::size_t idx = free_frames_.front();
    free_frames_.pop_front();
    return idx;
  }
  if (lru_.empty()) {
    throw BufferFullError("BufferPool: all frames pinned");
  }
  std::size_t idx = lru_.back();
  lru_.pop_back();
  lru_pos_.erase(idx);

  Frame& victim = frames_[idx];
  if (victim.dirty) {
    dm_->WritePage(victim.file, victim.page_no, victim.page.Data());
  }
  page_table_.erase(Key(victim.file, victim.page_no));
  victim.valid = false;
  victim.dirty = false;
  return idx;
}

Page* BufferPool::Fetch(FileId file, PageNo page_no) {
  std::int64_t key = Key(file, page_no);
  auto it = page_table_.find(key);
  if (it != page_table_.end()) {
    Frame& f = frames_[it->second];
    if (f.pin_count == 0) {
      lru_.erase(lru_pos_.at(it->second));
      lru_pos_.erase(it->second);
    }
    ++f.pin_count;
    ++stats_->buffer_hits;
    return &f.page;
  }

  std::size_t idx = AcquireFrame();
  Frame& f = frames_[idx];
  dm_->ReadPage(file, page_no, f.page.Data());
  f.file = file;
  f.page_no = page_no;
  f.pin_count = 1;
  f.dirty = false;
  f.valid = true;
  page_table_[key] = idx;
  return &f.page;
}

Page* BufferPool::NewPage(FileId file, PageNo* page_no_out) {
  PageNo page_no = dm_->AllocatePage(file);
  std::size_t idx = AcquireFrame();
  Frame& f = frames_[idx];
  f.page.Zero();
  f.file = file;
  f.page_no = page_no;
  f.pin_count = 1;
  f.dirty = true;
  f.valid = true;
  page_table_[Key(file, page_no)] = idx;
  *page_no_out = page_no;
  return &f.page;
}

void BufferPool::Unpin(FileId file, PageNo page_no, bool dirty) {
  auto it = page_table_.find(Key(file, page_no));
  if (it == page_table_.end()) {
    throw std::logic_error("BufferPool::Unpin: page not resident");
  }
  Frame& f = frames_[it->second];
  if (dirty) f.dirty = true;
  if (f.pin_count <= 0) {
    throw std::logic_error("BufferPool::Unpin: page not pinned");
  }
  --f.pin_count;
  if (f.pin_count == 0) {
    lru_.push_front(it->second);
    lru_pos_[it->second] = lru_.begin();
  }
}

void BufferPool::FlushAll() {
  for (Frame& f : frames_) {
    if (f.valid && f.dirty) {
      dm_->WritePage(f.file, f.page_no, f.page.Data());
      f.dirty = false;
    }
  }
}

void BufferPool::Reset() {
  FlushAll();
  page_table_.clear();
  lru_.clear();
  lru_pos_.clear();
  free_frames_.clear();
  for (std::size_t i = 0; i < frames_.size(); ++i) {
    frames_[i] = Frame{};
    free_frames_.push_back(i);
  }
}

}  // namespace qe
