#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <unistd.h>

#include "common/stats.h"
#include "exec/bnlj.h"
#include "exec/projection.h"
#include "exec/sink.h"
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

std::vector<Tuple> RunBnljQA(HeapFile* s_heap, HeapFile* r_heap, std::int32_t rmin,
                              std::int32_t dmin, std::size_t buffer_pages) {
  BNLJ bnlj(s_heap, {"S.sid", "S.rating", "S.age"},
            {Predicate{1, CompareOp::kGt, rmin}}, 0, r_heap,
            {"R.sid", "R.bid", "R.day"}, {Predicate{2, CompareOp::kGe, dmin}}, 0,
            buffer_pages);
  Projection proj(&bnlj, {"S.sid", "S.rating", "R.bid", "R.day"});
  proj.Open();
  std::vector<Tuple> out;
  Tuple t;
  while (proj.Next(&t)) out.push_back(t);
  proj.Close();
  std::sort(out.begin(), out.end());
  return out;
}

}

TEST_MAIN_BEGIN

  constexpr std::size_t kTupleBytes = 1024;
  constexpr std::int32_t kNumSailors = 10;
  constexpr std::int32_t kNumReserves = 15;
  constexpr std::int32_t kRmin = 3;
  constexpr std::int32_t kDmin = 50;

  std::mt19937 rng(99);
  std::uniform_int_distribution<std::int32_t> rating_dist(1, 10);
  std::uniform_int_distribution<std::int32_t> day_dist(1, 365);
  std::uniform_int_distribution<std::int32_t> sid_dist(0, kNumSailors - 1);

  std::vector<Tuple> sailors, reserves;
  for (std::int32_t sid = 0; sid < kNumSailors; ++sid) {
    sailors.push_back({sid, rating_dist(rng), 20});
  }
  for (std::int32_t i = 0; i < kNumReserves; ++i) {

    reserves.push_back({sid_dist(rng), i, day_dist(rng)});
  }

  std::string s_path = TempPath("bnlj_s.bin");
  std::string r_path = TempPath("bnlj_r.bin");
  std::filesystem::remove(s_path);
  std::filesystem::remove(r_path);

  IoStats stats;
  DiskManager dm(&stats);
  BufferPool bp(&dm, 16, &stats);
  Schema s_schema({"sid", "rating", "age"}, kTupleBytes);
  Schema r_schema({"sid", "bid", "day"}, kTupleBytes);
  FileId s_file = dm.OpenFile(s_path, FileKind::kBase);
  FileId r_file = dm.OpenFile(r_path, FileKind::kBase);
  HeapFile s_heap(&bp, &dm, s_file, &s_schema);
  HeapFile r_heap(&bp, &dm, r_file, &r_schema);
  for (const Tuple& t : sailors) s_heap.Append(t);
  for (const Tuple& t : reserves) r_heap.Append(t);
  bp.FlushAll();

  std::vector<Tuple> expected = ReferenceJoinQA(sailors, reserves, kRmin, kDmin);
  std::sort(expected.begin(), expected.end());
  CHECK(!expected.empty());

  std::size_t M = s_heap.NumPages();
  std::size_t N = r_heap.NumPages();
  CHECK(M >= 3);
  CHECK(N >= 3);

  for (std::size_t buffer_pages : {std::size_t{3}, std::size_t{5}, std::size_t{100}}) {
    std::vector<Tuple> actual = RunBnljQA(&s_heap, &r_heap, kRmin, kDmin, buffer_pages);
    CHECK(actual == expected);
  }

  for (std::size_t buffer_pages : {std::size_t{3}, std::size_t{4}, std::size_t{6}}) {
    IoStats join_stats;
    DiskManager join_dm(&join_stats);
    BufferPool join_bp(&join_dm, buffer_pages, &join_stats);
    FileId s_file2 = join_dm.OpenFile(s_path, FileKind::kBase);
    FileId r_file2 = join_dm.OpenFile(r_path, FileKind::kBase);
    HeapFile s_heap2(&join_bp, &join_dm, s_file2, &s_schema);
    HeapFile r_heap2(&join_bp, &join_dm, r_file2, &r_schema);

    std::int64_t reads_before = join_stats.page_reads;

    BNLJ bnlj(&s_heap2, {"S.sid", "S.rating", "S.age"},
              {Predicate{1, CompareOp::kGt, kRmin}}, 0, &r_heap2,
              {"R.sid", "R.bid", "R.day"}, {Predicate{2, CompareOp::kGe, kDmin}}, 0,
              buffer_pages);
    Sink sink(&bnlj);
    sink.Run();

    std::size_t block_pages = buffer_pages > 2 ? buffer_pages - 2 : 1;
    std::size_t expected_reads = M + static_cast<std::size_t>(std::ceil(
                                          static_cast<double>(M) / block_pages)) * N;
    std::size_t actual_reads =
        static_cast<std::size_t>(join_stats.page_reads - reads_before);

    long diff = static_cast<long>(actual_reads) - static_cast<long>(expected_reads);
    CHECK(diff >= -2 && diff <= 2);
  }

  std::filesystem::remove(s_path);
  std::filesystem::remove(r_path);
TEST_MAIN_END
