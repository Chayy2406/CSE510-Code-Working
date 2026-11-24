#include <algorithm>
#include <filesystem>
#include <random>
#include <unistd.h>

#include "common/stats.h"
#include "exec/inlj.h"
#include "exec/projection.h"
#include "exec/selection.h"
#include "exec/seq_scan.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "index/bplus_tree.h"
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

}

TEST_MAIN_BEGIN

  {
    constexpr std::size_t kTupleBytes = 200;
    constexpr std::int32_t kNumSailors = 15;
    constexpr std::int32_t kNumReserves = 50;
    constexpr std::int32_t kRmin = 2;
    constexpr std::int32_t kDmin = 10;

    std::mt19937 rng(55);
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

    std::string s_path = TempPath("inlj_s.bin");
    std::string r_path = TempPath("inlj_r.bin");
    std::string idx_path = TempPath("inlj_idx.bin");
    std::filesystem::remove(s_path);
    std::filesystem::remove(r_path);
    std::filesystem::remove(idx_path);

    IoStats stats;
    DiskManager dm(&stats);
    BufferPool bp(&dm, 32, &stats);
    Schema s_schema({"sid", "rating", "age"}, kTupleBytes);
    Schema r_schema({"sid", "bid", "day"}, kTupleBytes);
    FileId s_file = dm.OpenFile(s_path, FileKind::kBase);
    FileId r_file = dm.OpenFile(r_path, FileKind::kBase);
    HeapFile s_heap(&bp, &dm, s_file, &s_schema);
    HeapFile r_heap(&bp, &dm, r_file, &r_schema);
    for (const Tuple& t : sailors) s_heap.Append(t);
    for (const Tuple& t : reserves) r_heap.Append(t);

    FileId idx_file = dm.OpenFile(idx_path, FileKind::kIndex);
    BPlusTree s_index(&bp, &dm, idx_file);
    s_heap.Scan([&](Rid rid, const Tuple& t) { s_index.Insert(t[0], rid); });

    SeqScan r_scan(&r_heap, {"R.sid", "R.bid", "R.day"});
    Selection r_sel(&r_scan, {Predicate{2, CompareOp::kGe, kDmin}});
    INLJ inlj(&r_sel, 0, {"R.sid", "R.bid", "R.day"}, &s_index, &s_heap,
              {"S.sid", "S.rating", "S.age"});
    Selection post_filter(&inlj, {Predicate{4, CompareOp::kGt, kRmin}});
    Projection proj(&post_filter, {"S.sid", "S.rating", "R.bid", "R.day"});

    proj.Open();
    std::vector<Tuple> actual;
    Tuple t;
    while (proj.Next(&t)) actual.push_back(t);
    proj.Close();
    std::sort(actual.begin(), actual.end());

    std::vector<Tuple> expected = ReferenceJoinQA(sailors, reserves, kRmin, kDmin);
    std::sort(expected.begin(), expected.end());
    CHECK(!expected.empty());
    CHECK(actual == expected);

    std::filesystem::remove(s_path);
    std::filesystem::remove(r_path);
    std::filesystem::remove(idx_path);
  }

  {
    constexpr std::size_t kTupleBytes = 200;
    constexpr std::int32_t kNumSailors = 200;
    constexpr std::int32_t kNumReserves = 400;

    auto measure = [&](bool clustered) {
      std::vector<Tuple> sailors;
      for (std::int32_t sid = 0; sid < kNumSailors; ++sid) {
        sailors.push_back({sid, 5, 20});
      }
      if (!clustered) {
        std::mt19937 shuffle_rng(3);
        std::shuffle(sailors.begin(), sailors.end(), shuffle_rng);
      }

      std::string s_path = TempPath(clustered ? "inlj_clus_s.bin" : "inlj_unclus_s.bin");
      std::string idx_path =
          TempPath(clustered ? "inlj_clus_idx.bin" : "inlj_unclus_idx.bin");
      std::filesystem::remove(s_path);
      std::filesystem::remove(idx_path);

      IoStats stats;
      DiskManager dm(&stats);
      constexpr std::size_t kPoolFrames = 4;
      BufferPool bp(&dm, kPoolFrames, &stats);
      Schema s_schema({"sid", "rating", "age"}, kTupleBytes);
      FileId s_file = dm.OpenFile(s_path, FileKind::kBase);
      HeapFile s_heap(&bp, &dm, s_file, &s_schema);
      for (const Tuple& t : sailors) s_heap.Append(t);

      FileId idx_file = dm.OpenFile(idx_path, FileKind::kIndex);
      BPlusTree s_index(&bp, &dm, idx_file);
      s_heap.Scan([&](Rid rid, const Tuple& t) { s_index.Insert(t[0], rid); });

      bp.Reset();
      IoStats before = stats;

      for (std::int32_t i = 0; i < kNumReserves; ++i) {
        std::int32_t sid = (i / 2) % kNumSailors;
        s_index.EqualityScan(sid, [&](Rid rid) { s_heap.Fetch(rid); });
      }

      std::filesystem::remove(s_path);
      std::filesystem::remove(idx_path);
      return stats.reads_base - before.reads_base;
    };

    std::int64_t clustered_reads = measure( true);
    std::int64_t unclustered_reads = measure( false);

    CHECK(clustered_reads < unclustered_reads);

    CHECK(clustered_reads * 2 < unclustered_reads);
  }
TEST_MAIN_END
