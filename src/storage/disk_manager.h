#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/stats.h"
#include "common/types.h"
#include "storage/page.h"

namespace qe {

class DiskManager {
 public:

  explicit DiskManager(IoStats* stats) : stats_(stats) {}
  ~DiskManager();

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  FileId OpenFile(const std::string& path, FileKind kind);

  PageNo AllocatePage(FileId file);

  PageNo PageCount(FileId file) const;

  void ReadPage(FileId file, PageNo page_no, char* buf);

  void WritePage(FileId file, PageNo page_no, const char* buf);

  void CloseFile(FileId file);

 private:
  struct FileEntry {
    int fd = -1;
    FileKind kind = FileKind::kBase;
    PageNo page_count = 0;
  };

  FileEntry& Entry(FileId file);
  const FileEntry& Entry(FileId file) const;

  IoStats* stats_;
  std::vector<FileEntry> files_;
};

}
