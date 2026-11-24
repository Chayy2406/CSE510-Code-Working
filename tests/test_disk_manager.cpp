#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unistd.h>

#include "common/stats.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "test_util.h"

using qe::DiskManager;
using qe::FileKind;
using qe::IoStats;
using qe::kPageSize;

namespace {

std::string TempPath(const char* name) {
  return (std::filesystem::temp_directory_path() /
          (std::string("qe_test_") + std::to_string(getpid()) + "_" + name))
.string();
}

}

TEST_MAIN_BEGIN
  std::string path = TempPath("disk_manager.bin");
  std::filesystem::remove(path);

  {
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);

    qe::PageNo p0 = dm.AllocatePage(f);
    CHECK_EQ(p0, 0);
    char buf[kPageSize];
    dm.ReadPage(f, p0, buf);
    CHECK_EQ(stats.page_reads, 1);
    CHECK_EQ(stats.reads_base, 1);
    bool all_zero = true;
    for (char c : buf) {
      if (c != 0) all_zero = false;
    }
    CHECK(all_zero);

    char out[kPageSize];
    std::memset(out, 0, kPageSize);
    std::memcpy(out, "hello page", 10);
    dm.WritePage(f, p0, out);
    CHECK_EQ(stats.page_writes, 1);

    char in[kPageSize];
    dm.ReadPage(f, p0, in);
    CHECK_EQ(stats.page_reads, 2);
    CHECK_EQ(std::memcmp(in, out, kPageSize), 0);

    qe::PageNo p1 = dm.AllocatePage(f);
    CHECK_EQ(p1, 1);
    CHECK_EQ(dm.PageCount(f), 2);

    qe::FileId idx = dm.OpenFile(TempPath("disk_manager_idx.bin"), FileKind::kIndex);
    dm.AllocatePage(idx);
    dm.ReadPage(idx, 0, buf);
    CHECK_EQ(stats.reads_index, 1);
    CHECK_EQ(stats.reads_base, 2);

    qe::FileId tmp = dm.OpenFile(TempPath("disk_manager_tmp.bin"), FileKind::kTemp);
    dm.AllocatePage(tmp);
    dm.WritePage(tmp, 0, out);
    dm.ReadPage(tmp, 0, buf);
    CHECK_EQ(stats.writes_temp, 1);
    CHECK_EQ(stats.reads_temp, 1);

    std::filesystem::remove(TempPath("disk_manager_idx.bin"));
    std::filesystem::remove(TempPath("disk_manager_tmp.bin"));
  }

  std::filesystem::remove(path);
TEST_MAIN_END
