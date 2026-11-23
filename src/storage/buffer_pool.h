#pragma once

#include <cstdint>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "common/stats.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

namespace qe {

// Thrown when every frame is pinned and a new page (via Fetch or NewPage)
// is requested.
class BufferFullError : public std::runtime_error {
 public:
  explicit BufferFullError(const std::string& msg) : std::runtime_error(msg) {}
};

// Fixed-size pool of B frames with LRU replacement over unpinned frames.
// A miss (page not resident) costs one DiskManager::ReadPage, counted as
// one page_read. A hit is free (no I/O) but counted in buffer_hits.
// Evicting a dirty frame costs one DiskManager::WritePage. Pinned frames
// are never eviction candidates (SPEC.md section 3).
class BufferPool {
 public:
  // dm and stats must outlive this BufferPool.
  BufferPool(DiskManager* dm, std::size_t num_frames, IoStats* stats);

  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;

  // Pins (file, page_no) and returns its data, reading it in on a miss.
  Page* Fetch(FileId file, PageNo page_no);

  // Allocates a new page in `file`, pins it (zeroed, marked dirty), and
  // reports its page number via *page_no_out.
  Page* NewPage(FileId file, PageNo* page_no_out);

  // Decrements the pin count for (file, page_no). `dirty` is sticky: once
  // set it stays set until the frame is flushed or evicted.
  void Unpin(FileId file, PageNo page_no, bool dirty);

  // Writes every resident dirty frame back to disk.
  void FlushAll();

  // Flushes dirty frames, then empties the pool. Used to start a
  // measured run with a cold buffer pool.
  void Reset();

  std::size_t NumFrames() const { return frames_.size(); }

 private:
  struct Frame {
    Page page;
    FileId file = kInvalidFileId;
    PageNo page_no = kInvalidPageNo;
    int pin_count = 0;
    bool dirty = false;
    bool valid = false;
  };

  static std::int64_t Key(FileId file, PageNo page_no) {
    return (static_cast<std::int64_t>(file) << 32) |
           static_cast<std::uint32_t>(page_no);
  }

  // Returns a frame index ready to hold a new page: a free frame if one
  // exists, otherwise the LRU-evicted frame (flushing it first if dirty).
  std::size_t AcquireFrame();

  DiskManager* dm_;
  IoStats* stats_;
  std::vector<Frame> frames_;
  std::unordered_map<std::int64_t, std::size_t> page_table_;
  std::list<std::size_t> free_frames_;

  // Eviction candidates: unpinned, valid frames. Front = most recently
  // used, back = least recently used (next to evict).
  std::list<std::size_t> lru_;
  std::unordered_map<std::size_t, std::list<std::size_t>::iterator> lru_pos_;
};

}  // namespace qe
