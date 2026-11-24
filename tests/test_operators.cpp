#include <filesystem>
#include <unistd.h>

#include "common/stats.h"
#include "exec/projection.h"
#include "exec/seq_scan.h"
#include "exec/selection.h"
#include "exec/sink.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "test_util.h"

using namespace qe;

namespace {

std::string TempPath(const char* name) {
  return (std::filesystem::temp_directory_path() /
          (std::string("qe_test_") + std::to_string(getpid()) + "_" + name))
.string();
}

}

TEST_MAIN_BEGIN
  std::string path = TempPath("operators.bin");
  std::filesystem::remove(path);
  IoStats stats;
  DiskManager dm(&stats);
  BufferPool bp(&dm, 4, &stats);
  FileId file = dm.OpenFile(path, FileKind::kBase);
  Schema schema({"sid", "rating", "age"}, 12);
  HeapFile heap(&bp, &dm, file, &schema);

  std::vector<Tuple> rows = {{0, 3, 20}, {1, 7, 30}, {2, 9, 40}, {3, 2, 50}};
  for (const Tuple& t : rows) heap.Append(t);

  {
    SeqScan scan(&heap, {"sid", "rating", "age"});
    scan.Open();
    Tuple t;
    std::size_t i = 0;
    while (scan.Next(&t)) {
      CHECK(t == rows[i]);
      ++i;
    }
    scan.Close();
    CHECK_EQ(i, rows.size());
  }

  {
    SeqScan scan(&heap, {"sid", "rating", "age"},
                 {Predicate{1, CompareOp::kGt, 5}});
    scan.Open();
    Tuple t;
    std::size_t count = 0;
    while (scan.Next(&t)) {
      CHECK(t[1] > 5);
      ++count;
    }
    scan.Close();
    CHECK_EQ(count, std::size_t{2});
  }

  {
    SeqScan scan(&heap, {"sid", "rating", "age"});
    Selection sel(&scan, {Predicate{1, CompareOp::kGt, 5}});
    Sink sink(&sel);
    SinkResult result = sink.Run();
    CHECK_EQ(result.row_count, std::size_t{2});
  }

  {
    SeqScan scan(&heap, {"sid", "rating", "age"});
    Projection proj(&scan, {"age", "sid"});
    proj.Open();
    Tuple t;
    CHECK(proj.Next(&t));
    Tuple expected{20, 0};
    CHECK(t == expected);
    proj.Close();
  }

  {
    SeqScan scan(&heap, {"sid", "rating", "age"});
    bool threw = false;
    try {
      Projection proj(&scan, {"nope"});
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    CHECK(threw);
  }

  {
    SinkResult a;
    for (const Tuple& t : rows) a.checksum = AccumulateChecksum(a.checksum, t);
    SinkResult b;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      b.checksum = AccumulateChecksum(b.checksum, *it);
    }
    CHECK_EQ(a.checksum, b.checksum);
  }

  std::filesystem::remove(path);
TEST_MAIN_END
