#include <algorithm>
#include <filesystem>
#include <random>
#include <unistd.h>

#include "common/stats.h"
#include "exec/group_by.h"
#include "exec/projection.h"
#include "exec/selection.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "index/bplus_tree.h"
#include "plan/plan_builder.h"
#include "plan/qb_plan.h"
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
  constexpr std::size_t kTupleBytes = 1024;
  constexpr std::int32_t kNumSailors = 15;
  constexpr std::int32_t kNumBoats = 8;
  constexpr std::int32_t kNumReserves = 60;
  constexpr std::int32_t kRmin = 2;
  constexpr std::int32_t kAmax = 60;
  constexpr std::int32_t kColor = 1;
  constexpr std::int32_t kD1 = 50;
  constexpr std::int32_t kD2 = 300;
  constexpr std::int32_t kH = 1;

  std::mt19937 rng(99);
  std::uniform_int_distribution<std::int32_t> rating_dist(1, 10);
  std::uniform_int_distribution<std::int32_t> age_dist(16, 70);
  std::uniform_int_distribution<std::int32_t> color_dist(0, 3);
  std::uniform_int_distribution<std::int32_t> day_dist(1, 365);
  std::uniform_int_distribution<std::int32_t> sid_dist(0, kNumSailors - 1);
  std::uniform_int_distribution<std::int32_t> bid_dist(0, kNumBoats - 1);

  std::vector<Tuple> sailors, boats, reserves;
  for (std::int32_t sid = 0; sid < kNumSailors; ++sid) {
    sailors.push_back({sid, rating_dist(rng), age_dist(rng)});
  }
  for (std::int32_t bid = 0; bid < kNumBoats; ++bid) boats.push_back({bid, color_dist(rng)});
  for (std::int32_t i = 0; i < kNumReserves; ++i) {
    reserves.push_back({sid_dist(rng), bid_dist(rng), day_dist(rng)});
  }

  std::vector<Tuple> expected =
      ReferenceJoinQC(sailors, reserves, boats, kRmin, kAmax, kColor, kD1, kD2, kH);
  std::sort(expected.begin(), expected.end());
  CHECK(!expected.empty());

  std::string s_path = TempPath("qc_s.bin");
  std::string r_path = TempPath("qc_r.bin");
  std::string b_path = TempPath("qc_b.bin");
  std::string s_idx_path = TempPath("qc_s_idx.bin");
  std::string b_idx_path = TempPath("qc_b_idx.bin");
  for (const std::string& p : {s_path, r_path, b_path, s_idx_path, b_idx_path}) {
    std::filesystem::remove(p);
  }

  IoStats stats;
  DiskManager dm(&stats);
  BufferPool bp(&dm, 64, &stats);
  Schema s_schema({"sid", "rating", "age"}, kTupleBytes);
  Schema r_schema({"sid", "bid", "day"}, kTupleBytes);
  Schema b_schema({"bid", "color"}, kTupleBytes);
  FileId s_file = dm.OpenFile(s_path, FileKind::kBase);
  FileId r_file = dm.OpenFile(r_path, FileKind::kBase);
  FileId b_file = dm.OpenFile(b_path, FileKind::kBase);
  HeapFile s_heap(&bp, &dm, s_file, &s_schema);
  HeapFile r_heap(&bp, &dm, r_file, &r_schema);
  HeapFile b_heap(&bp, &dm, b_file, &b_schema);
  for (const Tuple& t : sailors) s_heap.Append(t);
  for (const Tuple& t : reserves) r_heap.Append(t);
  for (const Tuple& t : boats) b_heap.Append(t);

  FileId s_idx_file = dm.OpenFile(s_idx_path, FileKind::kIndex);
  FileId b_idx_file = dm.OpenFile(b_idx_path, FileKind::kIndex);
  BPlusTree s_index(&bp, &dm, s_idx_file);
  BPlusTree b_index(&bp, &dm, b_idx_file);
  s_heap.Scan([&](Rid rid, const Tuple& t) { s_index.Insert(t[0], rid); });
  b_heap.Scan([&](Rid rid, const Tuple& t) { b_index.Insert(t[0], rid); });

  int plan_count = 0;
  for (const std::string& order : {"order1", "order2"}) {
    for (bool pushdown : {true, false}) {
      for (const std::string& method1 : {"bnlj", "smj", "ghj", "inlj"}) {
        for (const std::string& method2 : {"bnlj", "smj", "ghj", "inlj"}) {
          ++plan_count;
          PlanBuilder builder(&bp, &dm, 8,
                               TempPath("qc_work") + "_" + std::to_string(plan_count), &stats);
          builder.BindTable("S", &s_heap, {"S.sid", "S.rating", "S.age"});
          builder.BindTable("R", &r_heap, {"R.sid", "R.bid", "R.day"});
          builder.BindTable("B", &b_heap, {"B.bid", "B.color"});
          builder.BindIndex("S", "S.sid", &s_index);
          builder.BindIndex("B", "B.bid", &b_index);

          auto tree =
              BuildQBPlan(order, method1, method2, pushdown, kRmin, kAmax, kColor, kD1, kD2);
          Operator* top = builder.Build(*tree);
          GroupBy group_by(top, "S.rating");
          Selection having(&group_by, {Predicate{1, CompareOp::kGt, kH}});

          having.Open();
          std::vector<Tuple> actual;
          Tuple t;
          while (having.Next(&t)) actual.push_back(t);
          having.Close();
          std::sort(actual.begin(), actual.end());

          if (actual != expected) {
            std::cerr << "MISMATCH order=" << order << " method1=" << method1
                      << " method2=" << method2 << " pushdown=" << pushdown << "\n";
          }
          CHECK(actual == expected);
        }
      }
    }
  }
  CHECK(plan_count >= 12);

  for (const std::string& p : {s_path, r_path, b_path, s_idx_path, b_idx_path}) {
    std::filesystem::remove(p);
  }
TEST_MAIN_END
