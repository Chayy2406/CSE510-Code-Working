#include <filesystem>
#include <unistd.h>

#include "common/stats.h"
#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "test_util.h"

using qe::BufferFullError;
using qe::BufferPool;
using qe::DiskManager;
using qe::FileKind;
using qe::IoStats;
using qe::Page;
using qe::PageNo;

namespace {

std::string TempPath(const char* name) {
  return (std::filesystem::temp_directory_path() /
          (std::string("qe_test_") + std::to_string(getpid()) + "_" + name))
.string();
}

}

TEST_MAIN_BEGIN

  {
    std::string path = TempPath("bp_hit_miss.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm,  1, &stats);

    PageNo p0, p1;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, false);
    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);
    CHECK_EQ(stats.page_writes, 1);
    CHECK_EQ(stats.page_reads, 0);

    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);
    CHECK_EQ(stats.page_reads, 1);
    CHECK_EQ(stats.buffer_hits, 0);

    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);
    CHECK_EQ(stats.page_reads, 1);
    CHECK_EQ(stats.buffer_hits, 1);
    std::filesystem::remove(path);
  }

  {
    std::string path = TempPath("bp_dirty_evict.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm,  2, &stats);

    PageNo p0, p1, p2, p3;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, false);
    bp.FlushAll();
    CHECK_EQ(stats.page_writes, 1);

    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);

    bp.NewPage(f, &p2);
    bp.Unpin(f, p2, false);
    CHECK_EQ(stats.page_writes, 1);

    bp.NewPage(f, &p3);
    bp.Unpin(f, p3, false);
    CHECK_EQ(stats.page_writes, 2);
    std::filesystem::remove(path);
  }

  {
    std::string path = TempPath("bp_pin_protect.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm,  2, &stats);

    PageNo p0, p1, p2;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, false);
    bp.Fetch(f, p0);
    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);

    bp.NewPage(f, &p2);
    bp.Unpin(f, p2, false);

    std::int64_t reads_before = stats.page_reads;
    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);
    CHECK_EQ(stats.page_reads, reads_before);
    std::filesystem::remove(path);
  }

  {
    std::string path = TempPath("bp_full.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm,  1, &stats);

    PageNo p0, p1;
    bp.NewPage(f, &p0);
    bool threw = false;
    try {
      bp.NewPage(f, &p1);
    } catch (const BufferFullError&) {
      threw = true;
    }
    CHECK(threw);
    std::filesystem::remove(path);
  }

  {
    std::string path = TempPath("bp_trace.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm,  2, &stats);

    PageNo p0, p1;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, true);
    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);

    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);
    bp.Fetch(f, p1);
    bp.Unpin(f, p1, false);
    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);

    CHECK_EQ(stats.page_reads, 0);
    CHECK_EQ(stats.buffer_hits, 3);
    CHECK_EQ(stats.page_writes, 0);

    bp.Reset();
    CHECK_EQ(stats.page_writes, 2);
    std::filesystem::remove(path);
  }
TEST_MAIN_END
