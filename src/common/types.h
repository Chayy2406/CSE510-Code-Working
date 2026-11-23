#pragma once

#include <cstdint>

namespace qe {

// Page size is fixed at compile time (SPEC.md section 3, [ASSUMPTION]).
constexpr std::size_t kPageSize = 4096;

using PageNo = std::int32_t;
constexpr PageNo kInvalidPageNo = -1;

// Identifies which file a page belongs to. DiskManager owns one real file
// per FileId; BufferPool frames are keyed by (FileId, PageNo).
using FileId = std::int32_t;
constexpr FileId kInvalidFileId = -1;

enum class FileKind { kBase, kIndex, kTemp };

// A tuple's physical location: which page, which slot within that page.
struct Rid {
  PageNo page_no = kInvalidPageNo;
  std::int32_t slot = -1;

  bool operator==(const Rid& other) const {
    return page_no == other.page_no && slot == other.slot;
  }
};

}  // namespace qe
