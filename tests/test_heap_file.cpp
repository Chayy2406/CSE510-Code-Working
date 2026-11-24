#include <filesystem>
#include <unistd.h>

#include "common/stats.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "test_util.h"

using qe::BufferPool;
using qe::DiskManager;
using qe::FileKind;
using qe::HeapFile;
using qe::IoStats;
using qe::Rid;
using qe::Schema;
using qe::Tuple;

namespace {
std::string TempPath(const char* name) {
  return (std::filesystem::temp_directory_path() /
          (std::string("qe_test_") + std::to_string(getpid()) + "_" + name))
.string();
}
}

TEST_MAIN_BEGIN

  {
    std::string path = TempPath("heap_round_trip.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    BufferPool bp(&dm, 4, &stats);
    qe::FileId file = dm.OpenFile(path, FileKind::kBase);
    Schema schema({"sid", "rating", "age"},  12);

    std::vector<Rid> rids;
    std::vector<Tuple> expected;
    {
      HeapFile heap(&bp, &dm, file, &schema);

      for (int i = 0; i < 1000; ++i) {
        Tuple t{i, (i % 10) + 1, 16 + (i % 55)};
        rids.push_back(heap.Append(t));
        expected.push_back(t);
      }
      CHECK(heap.NumPages() >= 3);

      for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(heap.Fetch(rids[i]) == expected[i]);
      }

      std::size_t scanned = 0;
      heap.Scan([&](Rid, const Tuple& t) {
        CHECK(t == expected[scanned]);
        ++scanned;
      });
      CHECK_EQ(scanned, expected.size());
    }

    {
      HeapFile heap(&bp, &dm, file, &schema);
      CHECK_EQ(heap.NumPages(), static_cast<std::size_t>(rids.back().page_no) + 1);
      Tuple extra{9999, 5, 30};
      Rid extra_rid = heap.Append(extra);
      CHECK(heap.Fetch(extra_rid) == extra);

      std::size_t scanned = 0;
      heap.Scan([&](Rid, const Tuple&) { ++scanned; });
      CHECK_EQ(scanned, expected.size() + 1);
    }
    std::filesystem::remove(path);
  }

  {
    std::string path = TempPath("heap_padding.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    BufferPool bp(&dm, 4, &stats);
    qe::FileId file = dm.OpenFile(path, FileKind::kBase);
    Schema schema({"bid", "color"},  50);
    HeapFile heap(&bp, &dm, file, &schema);

    Tuple t{7, 3};
    Rid rid = heap.Append(t);
    CHECK(heap.Fetch(rid) == t);
    std::filesystem::remove(path);
  }
TEST_MAIN_END
