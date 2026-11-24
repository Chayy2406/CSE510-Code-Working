#include <algorithm>
#include <filesystem>
#include <random>
#include <unistd.h>

#include "common/stats.h"
#include "exec/external_sort.h"
#include "exec/seq_scan.h"
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

  constexpr std::size_t kTupleBytes = 1024;
  constexpr int kNumTuples = 60;

  std::mt19937 rng(42);
  std::uniform_int_distribution<std::int32_t> key_dist(0, 1000);
  std::vector<Tuple> input;
  for (int i = 0; i < kNumTuples; ++i) input.push_back({key_dist(rng), i});

  std::string path = TempPath("sort_input.bin");
  std::filesystem::remove(path);
  IoStats stats;
  DiskManager dm(&stats);
  BufferPool bp(&dm, 64, &stats);
  Schema schema({"key", "seq"}, kTupleBytes);
  FileId file = dm.OpenFile(path, FileKind::kBase);
  HeapFile heap(&bp, &dm, file, &schema);
  for (const Tuple& t : input) heap.Append(t);

  std::vector<Tuple> expected = input;
  std::stable_sort(expected.begin(), expected.end(),
                    [](const Tuple& a, const Tuple& b) { return a[0] < b[0]; });

  {
    SeqScan scan(&heap, {"key", "seq"});
    ExternalSort sorter(&scan, 0, &schema, {"key", "seq"},  20, &bp,
                         &dm, TempPath("sort_zero_pass"));
    SortedRunSet runs = sorter.Sort();
    CHECK_EQ(runs.runs.size(), std::size_t{1});
    FusedMerge merge(runs, {"key", "seq"});
    merge.Open();
    std::vector<Tuple> actual;
    Tuple t;
    while (merge.Next(&t)) actual.push_back(t);
    CHECK(actual == expected);
  }

  {
    SeqScan scan(&heap, {"key", "seq"});
    ExternalSort sorter(&scan, 0, &schema, {"key", "seq"},  2, &bp, &dm,
                         TempPath("sort_many_pass"));
    SortedRunSet runs = sorter.Sort();
    CHECK_EQ(runs.runs.size(), std::size_t{1});
    FusedMerge merge(runs, {"key", "seq"});
    merge.Open();
    std::vector<Tuple> actual;
    Tuple t;
    while (merge.Next(&t)) actual.push_back(t);
    CHECK(actual == expected);
  }

  {
    std::vector<Tuple> dup_input;
    for (int i = 0; i < 30; ++i) dup_input.push_back({i % 5, i});

    std::string dup_path = TempPath("sort_dup.bin");
    std::filesystem::remove(dup_path);
    IoStats dup_stats;
    DiskManager dup_dm(&dup_stats);
    BufferPool dup_bp(&dup_dm, 64, &dup_stats);
    FileId dup_file = dup_dm.OpenFile(dup_path, FileKind::kBase);
    HeapFile dup_heap(&dup_bp, &dup_dm, dup_file, &schema);
    for (const Tuple& t : dup_input) dup_heap.Append(t);

    std::vector<Tuple> dup_expected = dup_input;
    std::stable_sort(dup_expected.begin(), dup_expected.end(),
                      [](const Tuple& a, const Tuple& b) { return a[0] < b[0]; });

    SeqScan dup_scan(&dup_heap, {"key", "seq"});
    ExternalSort dup_sorter(&dup_scan, 0, &schema, {"key", "seq"},  2,
                             &dup_bp, &dup_dm, TempPath("sort_dup_work"));
    SortedRunSet dup_runs = dup_sorter.Sort();
    FusedMerge dup_merge(dup_runs, {"key", "seq"});
    dup_merge.Open();
    std::vector<Tuple> dup_actual;
    Tuple t;
    while (dup_merge.Next(&t)) dup_actual.push_back(t);
    CHECK(dup_actual == dup_expected);
    std::filesystem::remove(dup_path);
  }

  {
    std::string cost_path = TempPath("sort_cost.bin");
    std::filesystem::remove(cost_path);
    IoStats cost_stats;
    DiskManager cost_dm(&cost_stats);
    constexpr std::size_t kBufferPages = 20;
    BufferPool cost_bp(&cost_dm, kBufferPages, &cost_stats);
    FileId cost_file = cost_dm.OpenFile(cost_path, FileKind::kBase);
    HeapFile cost_heap(&cost_bp, &cost_dm, cost_file, &schema);
    for (const Tuple& t : input) cost_heap.Append(t);
    std::size_t P = cost_heap.NumPages();
    CHECK(P >= 10);

    cost_bp.Reset();
    cost_stats.Reset();

    SeqScan cost_scan(&cost_heap, {"key", "seq"});
    ExternalSort cost_sorter(&cost_scan, 0, &schema, {"key", "seq"}, kBufferPages,
                              &cost_bp, &cost_dm, TempPath("sort_cost_work"));
    SortedRunSet runs = cost_sorter.PrepareSortedRuns(kBufferPages - 1);
    CHECK_EQ(runs.runs.size(), std::size_t{1});

    cost_bp.Reset();

    FusedMerge merge(runs, {"key", "seq"});
    merge.Open();
    Tuple t;
    while (merge.Next(&t)) {   }

    long expected_io = static_cast<long>(3 * P);
    long actual_io = cost_stats.page_reads + cost_stats.page_writes;
    long diff = actual_io - expected_io;
    CHECK(diff >= -4 && diff <= 4);
    std::filesystem::remove(cost_path);
  }

  std::filesystem::remove(path);
TEST_MAIN_END
