#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/stats.h"
#include "common/types.h"
#include "storage/page.h"

namespace qe {

// One real file per table / index / temp file, accessed with pread/pwrite.
// This is the only class allowed to touch the files directly; everything
// else goes through BufferPool. Every ReadPage/WritePage call increments
// the shared IoStats (SPEC.md section 3).
class DiskManager {
 public:
  // stats must outlive this DiskManager.
  explicit DiskManager(IoStats* stats) : stats_(stats) {}
  ~DiskManager();

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  // Opens (creating if needed) a backing file and returns a FileId used by
  // all other calls. `path` must be unique per file.
  FileId OpenFile(const std::string& path, FileKind kind);

  // Allocates the next page number for `file`; does not touch disk until
  // the page is written.
  PageNo AllocatePage(FileId file);

  // Number of pages ever allocated for `file`.
  PageNo PageCount(FileId file) const;

  // Reads kPageSize bytes at page_no into buf. Counted as one page read.
  void ReadPage(FileId file, PageNo page_no, char* buf);

  // Writes kPageSize bytes from buf at page_no. Counted as one page write.
  void WritePage(FileId file, PageNo page_no, const char* buf);

  // Closes the underlying fd early. Only safe once nothing in the
  // BufferPool still has a dirty, resident page for this file (flush
  // first) and nothing will touch this FileId again — used to bound the
  // number of open temp files during multi-pass external sort / grace
  // hash join, which would otherwise exhaust the process's fd limit.
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

}  // namespace qe
