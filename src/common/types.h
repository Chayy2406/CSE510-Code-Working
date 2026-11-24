#pragma once

#include <cstdint>

namespace qe {

constexpr std::size_t kPageSize = 4096;

using PageNo = std::int32_t;
constexpr PageNo kInvalidPageNo = -1;

using FileId = std::int32_t;
constexpr FileId kInvalidFileId = -1;

enum class FileKind { kBase, kIndex, kTemp };

struct Rid {
  PageNo page_no = kInvalidPageNo;
  std::int32_t slot = -1;

  bool operator==(const Rid& other) const {
    return page_no == other.page_no && slot == other.slot;
  }
};

}
