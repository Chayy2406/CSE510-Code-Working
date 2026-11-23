#include "storage/disk_manager.h"

#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>
#include <system_error>

namespace qe {

DiskManager::~DiskManager() {
  for (auto& entry : files_) {
    if (entry.fd >= 0) close(entry.fd);
  }
}

FileId DiskManager::OpenFile(const std::string& path, FileKind kind) {
  int fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(),
                             "DiskManager::OpenFile: " + path);
  }
  // page_count must reflect pages already on disk (a page_count of 0 for
  // an existing file would make every reader of it think the file is
  // empty), not just pages allocated through this DiskManager instance.
  off_t size = lseek(fd, 0, SEEK_END);
  if (size < 0) {
    throw std::system_error(errno, std::generic_category(),
                             "DiskManager::OpenFile: lseek " + path);
  }
  FileEntry entry;
  entry.fd = fd;
  entry.kind = kind;
  entry.page_count = static_cast<PageNo>(size / static_cast<off_t>(kPageSize));
  files_.push_back(entry);
  return static_cast<FileId>(files_.size() - 1);
}

DiskManager::FileEntry& DiskManager::Entry(FileId file) {
  if (file < 0 || static_cast<std::size_t>(file) >= files_.size()) {
    throw std::out_of_range("DiskManager: invalid FileId");
  }
  return files_[file];
}

const DiskManager::FileEntry& DiskManager::Entry(FileId file) const {
  if (file < 0 || static_cast<std::size_t>(file) >= files_.size()) {
    throw std::out_of_range("DiskManager: invalid FileId");
  }
  return files_[file];
}

PageNo DiskManager::AllocatePage(FileId file) {
  FileEntry& entry = Entry(file);
  PageNo page_no = entry.page_count;
  ++entry.page_count;
  return page_no;
}

PageNo DiskManager::PageCount(FileId file) const { return Entry(file).page_count; }

void DiskManager::ReadPage(FileId file, PageNo page_no, char* buf) {
  const FileEntry& entry = Entry(file);
  off_t offset = static_cast<off_t>(page_no) * static_cast<off_t>(kPageSize);
  ssize_t n = pread(entry.fd, buf, kPageSize, offset);
  if (n < 0) {
    throw std::system_error(errno, std::generic_category(),
                             "DiskManager::ReadPage");
  }
  // A page that was allocated but never written (a hole) reads as zeros;
  // pread may return fewer than kPageSize bytes past EOF.
  if (static_cast<std::size_t>(n) < kPageSize) {
    std::fill(buf + n, buf + kPageSize, 0);
  }

  ++stats_->page_reads;
  switch (entry.kind) {
    case FileKind::kBase:
      ++stats_->reads_base;
      break;
    case FileKind::kIndex:
      ++stats_->reads_index;
      break;
    case FileKind::kTemp:
      ++stats_->reads_temp;
      break;
  }
}

void DiskManager::WritePage(FileId file, PageNo page_no, const char* buf) {
  const FileEntry& entry = Entry(file);
  off_t offset = static_cast<off_t>(page_no) * static_cast<off_t>(kPageSize);
  ssize_t n = pwrite(entry.fd, buf, kPageSize, offset);
  if (n < 0 || static_cast<std::size_t>(n) != kPageSize) {
    throw std::system_error(errno, std::generic_category(),
                             "DiskManager::WritePage");
  }

  ++stats_->page_writes;
  if (entry.kind == FileKind::kTemp) ++stats_->writes_temp;
}

void DiskManager::CloseFile(FileId file) {
  FileEntry& entry = Entry(file);
  if (entry.fd >= 0) {
    close(entry.fd);
    entry.fd = -1;
  }
}

}  // namespace qe
