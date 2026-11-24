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

class BufferFullError : public std::runtime_error {
 public:
  explicit BufferFullError(const std::string& msg) : std::runtime_error(msg) {}
};

class BufferPool {
 public:

  BufferPool(DiskManager* dm, std::size_t num_frames, IoStats* stats);

  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;

  Page* Fetch(FileId file, PageNo page_no);

  Page* NewPage(FileId file, PageNo* page_no_out);

  void Unpin(FileId file, PageNo page_no, bool dirty);

  void FlushAll();

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

  std::size_t AcquireFrame();

  DiskManager* dm_;
  IoStats* stats_;
  std::vector<Frame> frames_;
  std::unordered_map<std::int64_t, std::size_t> page_table_;
  std::list<std::size_t> free_frames_;

  std::list<std::size_t> lru_;
  std::unordered_map<std::size_t, std::list<std::size_t>::iterator> lru_pos_;
};

}
