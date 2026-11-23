#include <algorithm>
#include <filesystem>
#include <random>
#include <unistd.h>

#include "common/stats.h"
#include "exec/ghj.h"
#include "exec/projection.h"
#include "exec/selection.h"
#include "exec/seq_scan.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "test_util.h"
#include "tools/ref_executor.h"

using namespace qe;

namespace {

std::string TempPath(const char* name) {
  return (std::filesystem::temp_directory_path() /
          (std::string("qe_test_") + std::to_string(getpid()) + "_" + name))
      .string();
}

std::vector<Tuple> RunGhjQA(HeapFile* s_heap, HeapFile* r_heap, std::int32_t rmin,
                             std::int32_t dmin, std::size_t buffer_pages, BufferPool* bp,
                             DiskManager* dm, const Schema* s_temp_schema,
                             const Schema* r_temp_schema, IoStats* stats) {
  SeqScan s_scan(s_heap, {"S.sid", "S.rating", "S.age"});
  Selection s_sel(&s_scan, {Predicate{1, CompareOp::kGt, rmin}});
  SeqScan r_scan(r_heap, {"R.sid", "R.bid", "R.day"});
  Selection r_sel(&r_scan, {Predicate{2, CompareOp::kGe, dmin}});

  GHJ ghj(&s_sel, 0, s_temp_schema, {"S.sid", "S.rating", "S.age"}, &r_sel, 0,
          r_temp_schema, {"R.sid", "R.bid", "R.day"}, buffer_pages, bp, dm,
          TempPath("ghj_qa"), stats);
  Projection proj(&ghj, {"S.sid", "S.rating", "R.bid", "R.day"});
  proj.Open();
  std::vector<Tuple> out;
  Tuple t;
  while (proj.Next(&t)) out.push_back(t);
  proj.Close();
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

TEST_MAIN_BEGIN
  constexpr std::size_t kTupleBytes = 1024;
  constexpr std::int32_t kNumSailors = 12;
  constexpr std::int32_t kNumReserves = 40;
  constexpr std::int32_t kRmin = 2;
  constexpr std::int32_t kDmin = 10;

  std::mt19937 rng(321);
  std::uniform_int_distribution<std::int32_t> rating_dist(1, 10);
  std::uniform_int_distribution<std::int32_t> day_dist(1, 365);
  std::discrete_distribution<std::int32_t> skewed_sid(
      {40, 30, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2});

  std::vector<Tuple> sailors, reserves;
  for (std::int32_t sid = 0; sid < kNumSailors; ++sid) {
    sailors.push_back({sid, rating_dist(rng), 20});
  }
  for (std::int32_t i = 0; i < kNumReserves; ++i) {
    reserves.push_back({skewed_sid(rng), i, day_dist(rng)});
  }

  std::string s_path = TempPath("ghj_s.bin");
  std::string r_path = TempPath("ghj_r.bin");
  std::filesystem::remove(s_path);
  std::filesystem::remove(r_path);

  IoStats stats;
  DiskManager dm(&stats);
  BufferPool bp(&dm, 64, &stats);
  Schema s_schema({"sid", "rating", "age"}, kTupleBytes);
  Schema r_schema({"sid", "bid", "day"}, kTupleBytes);
  FileId s_file = dm.OpenFile(s_path, FileKind::kBase);
  FileId r_file = dm.OpenFile(r_path, FileKind::kBase);
  HeapFile s_heap(&bp, &dm, s_file, &s_schema);
  HeapFile r_heap(&bp, &dm, r_file, &r_schema);
  for (const Tuple& t : sailors) s_heap.Append(t);
  for (const Tuple& t : reserves) r_heap.Append(t);
  bp.FlushAll();  // the loop below reopens these files via a fresh DiskManager

  std::vector<Tuple> expected = ReferenceJoinQA(sailors, reserves, kRmin, kDmin);
  std::sort(expected.begin(), expected.end());
  CHECK(!expected.empty());

  std::size_t M = s_heap.NumPages();
  std::size_t N = r_heap.NumPages();
  CHECK(M >= 4);
  CHECK(N >= 4);

  // Correctness against the reference executor at several buffer sizes,
  // including small ones that force recursive re-partitioning on the
  // skewed sid.
  for (std::size_t buffer_pages : {std::size_t{3}, std::size_t{5}, std::size_t{50}}) {
    IoStats run_stats;
    DiskManager run_dm(&run_stats);
    BufferPool run_bp(&run_dm, 64, &run_stats);
    FileId s_f = run_dm.OpenFile(s_path, FileKind::kBase);
    FileId r_f = run_dm.OpenFile(r_path, FileKind::kBase);
    HeapFile s_h(&run_bp, &run_dm, s_f, &s_schema);
    HeapFile r_h(&run_bp, &run_dm, r_f, &r_schema);
    std::vector<Tuple> actual = RunGhjQA(&s_h, &r_h, kRmin, kDmin, buffer_pages, &run_bp,
                                          &run_dm, &s_schema, &r_schema, &run_stats);
    CHECK(actual == expected);
  }

  // I/O cost "close to" 3(M+N) when partitions fit without recursion.
  {
    IoStats cost_stats;
    DiskManager cost_dm(&cost_stats);
    constexpr std::size_t kBufferPages = 30;
    BufferPool cost_bp(&cost_dm, kBufferPages, &cost_stats);
    FileId s_file2 = cost_dm.OpenFile(s_path, FileKind::kBase);
    FileId r_file2 = cost_dm.OpenFile(r_path, FileKind::kBase);
    HeapFile s_heap2(&cost_bp, &cost_dm, s_file2, &s_schema);
    HeapFile r_heap2(&cost_bp, &cost_dm, r_file2, &r_schema);

    RunGhjQA(&s_heap2, &r_heap2, kRmin, kDmin, kBufferPages, &cost_bp, &cost_dm,
             &s_schema, &r_schema, &cost_stats);

    long expected_io = 3 * static_cast<long>(M + N);
    long actual_io = cost_stats.page_reads + cost_stats.page_writes;
    long diff = actual_io - expected_io;
    CHECK(diff >= -8 && diff <= 8);
    CHECK_EQ(cost_stats.ghj_overflow_partitions, std::int64_t{0});
  }

  std::filesystem::remove(s_path);
  std::filesystem::remove(r_path);
TEST_MAIN_END
