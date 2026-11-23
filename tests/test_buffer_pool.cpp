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

}  // namespace

TEST_MAIN_BEGIN
  // Scenario 1: a hit is free (no extra read), a miss costs exactly one
  // read. A single-frame pool forces eviction between accesses so we get
  // a genuine miss followed by a genuine hit.
  {
    std::string path = TempPath("bp_hit_miss.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm, /*num_frames=*/1, &stats);

    PageNo p0, p1;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, false);
    bp.NewPage(f, &p1);  // evicts p0 (dirty since creation) -> 1 write
    bp.Unpin(f, p1, false);
    CHECK_EQ(stats.page_writes, 1);
    CHECK_EQ(stats.page_reads, 0);

    bp.Fetch(f, p0);  // p0 is no longer resident: a genuine miss
    bp.Unpin(f, p0, false);
    CHECK_EQ(stats.page_reads, 1);
    CHECK_EQ(stats.buffer_hits, 0);

    bp.Fetch(f, p0);  // still resident (only frame holds p0): a hit
    bp.Unpin(f, p0, false);
    CHECK_EQ(stats.page_reads, 1);
    CHECK_EQ(stats.buffer_hits, 1);
    std::filesystem::remove(path);
  }

  // Scenario 2: evicting a dirty frame costs exactly one write; a clean
  // eviction costs none. A page is only clean once it has been flushed.
  {
    std::string path = TempPath("bp_dirty_evict.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm, /*num_frames=*/2, &stats);

    PageNo p0, p1, p2, p3;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, false);
    bp.FlushAll();  // writes p0 once; it is now clean
    CHECK_EQ(stats.page_writes, 1);

    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);  // p1 is dirty (new, never written)

    bp.NewPage(f, &p2);  // pool full -> evicts LRU = clean p0: no write
    bp.Unpin(f, p2, false);
    CHECK_EQ(stats.page_writes, 1);

    bp.NewPage(f, &p3);  // evicts LRU = dirty p1: 1 write
    bp.Unpin(f, p3, false);
    CHECK_EQ(stats.page_writes, 2);
    std::filesystem::remove(path);
  }

  // Scenario 3: a pinned page is never chosen for eviction, even though
  // it is the least recently used.
  {
    std::string path = TempPath("bp_pin_protect.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm, /*num_frames=*/2, &stats);

    PageNo p0, p1, p2;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, false);  // p0 is the oldest unpinned page
    bp.Fetch(f, p0);         // re-pin p0 and keep it pinned (no Unpin)
    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);

    // Pool is full (p0 pinned, p1 unpinned). A third page must evict p1,
    // not p0.
    bp.NewPage(f, &p2);
    bp.Unpin(f, p2, false);

    // p0 must still be resident: fetching it again is a hit, not a miss.
    std::int64_t reads_before = stats.page_reads;
    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);
    CHECK_EQ(stats.page_reads, reads_before);
    std::filesystem::remove(path);
  }

  // Scenario 4: BufferFullError when every frame is pinned.
  {
    std::string path = TempPath("bp_full.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm, /*num_frames=*/1, &stats);

    PageNo p0, p1;
    bp.NewPage(f, &p0);  // pins the only frame, never unpinned
    bool threw = false;
    try {
      bp.NewPage(f, &p1);
    } catch (const BufferFullError&) {
      threw = true;
    }
    CHECK(threw);
    std::filesystem::remove(path);
  }

  // Scenario 5: counters match a hand-computed access trace.
  {
    std::string path = TempPath("bp_trace.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    qe::FileId f = dm.OpenFile(path, FileKind::kBase);
    BufferPool bp(&dm, /*num_frames=*/2, &stats);

    PageNo p0, p1;
    bp.NewPage(f, &p0);
    bp.Unpin(f, p0, true);
    bp.NewPage(f, &p1);
    bp.Unpin(f, p1, false);
    // Trace: fetch p0 (hit), fetch p1 (hit), fetch p0 (hit).
    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);
    bp.Fetch(f, p1);
    bp.Unpin(f, p1, false);
    bp.Fetch(f, p0);
    bp.Unpin(f, p0, false);

    CHECK_EQ(stats.page_reads, 0);   // both pages created via NewPage, never evicted
    CHECK_EQ(stats.buffer_hits, 3);  // the three Fetch calls above
    CHECK_EQ(stats.page_writes, 0);  // nothing evicted yet

    bp.Reset();  // flushes p0 and p1 (both still dirty since creation)
    CHECK_EQ(stats.page_writes, 2);
    std::filesystem::remove(path);
  }
TEST_MAIN_END
