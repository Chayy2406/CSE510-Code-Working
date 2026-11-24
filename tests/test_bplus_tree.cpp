#include <algorithm>
#include <filesystem>
#include <map>
#include <random>
#include <unistd.h>

#include "common/stats.h"
#include "index/bplus_tree.h"
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
}  // namespace

TEST_MAIN_BEGIN
  // Basic insert + equality + range, including duplicate keys.
  {
    std::string path = TempPath("bpt_basic.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    BufferPool bp(&dm, 16, &stats);
    FileId file = dm.OpenFile(path, FileKind::kIndex);
    BPlusTree tree(&bp, &dm, file);

    CHECK(tree.Empty());
    tree.Insert(5, Rid{0, 0});
    tree.Insert(3, Rid{0, 1});
    tree.Insert(5, Rid{0, 2});  // duplicate key
    tree.Insert(8, Rid{0, 3});
    tree.Insert(5, Rid{0, 4});  // another duplicate
    CHECK(!tree.Empty());
    CHECK_EQ(tree.Height(), 1);

    std::vector<Rid> fives;
    tree.EqualityScan(5, [&](Rid r) { fives.push_back(r); });
    std::sort(fives.begin(), fives.end(), [](Rid a, Rid b) { return a.slot < b.slot; });
    std::vector<Rid> expected_fives = {Rid{0, 0}, Rid{0, 2}, Rid{0, 4}};
    CHECK(fives == expected_fives);

    std::vector<Rid> none;
    tree.EqualityScan(99, [&](Rid r) { none.push_back(r); });
    CHECK(none.empty());

    std::vector<std::int32_t> range_keys;
    tree.RangeScan(4, 8, [&](std::int32_t k, Rid) { range_keys.push_back(k); });
    std::vector<std::int32_t> expected_range = {5, 5, 5, 8};
    CHECK(range_keys == expected_range);  // sorted; duplicates all present

    std::filesystem::remove(path);
  }

  // Enough inserts to force multiple leaf splits and at least one
  // internal (root) split, verified against a reference std::multimap.
  // Also verifies RangeScan returns keys in sorted order across leaves.
  {
    std::string path = TempPath("bpt_many.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    BufferPool bp(&dm, 32, &stats);
    FileId file = dm.OpenFile(path, FileKind::kIndex);
    BPlusTree tree(&bp, &dm, file);

    constexpr int kCount = 5000;  // forces many leaf splits + root splits
    std::mt19937 rng(7);
    std::uniform_int_distribution<std::int32_t> key_dist(0, 999);  // guarantees duplicates
    std::multimap<std::int32_t, Rid> reference;
    for (int i = 0; i < kCount; ++i) {
      std::int32_t key = key_dist(rng);
      Rid rid{i, i % 7};
      tree.Insert(key, rid);
      reference.emplace(key, rid);
    }

    CHECK(tree.Height() > 1);  // root must have split at least once

    // Every distinct key: equality scan must return exactly the
    // reference's set of Rids for that key.
    for (std::int32_t key : {0, 1, 500, 999, 42}) {
      std::vector<Rid> actual;
      tree.EqualityScan(key, [&](Rid r) { actual.push_back(r); });
      std::sort(actual.begin(), actual.end(),
                [](Rid a, Rid b) { return a.page_no < b.page_no; });
      std::vector<Rid> expected;
      auto range = reference.equal_range(key);
      for (auto it = range.first; it != range.second; ++it) expected.push_back(it->second);
      std::sort(expected.begin(), expected.end(),
                [](Rid a, Rid b) { return a.page_no < b.page_no; });
      CHECK(actual == expected);
    }

    // Full range scan must return every key in sorted order, with the
    // right total count (duplicates included).
    std::vector<std::int32_t> all_keys;
    tree.RangeScan(0, 999, [&](std::int32_t k, Rid) { all_keys.push_back(k); });
    CHECK_EQ(all_keys.size(), static_cast<std::size_t>(kCount));
    CHECK(std::is_sorted(all_keys.begin(), all_keys.end()));

    // A sub-range must match the reference exactly (count only, since
    // order among equal keys isn't meaningful to compare here).
    std::vector<std::int32_t> sub_range;
    tree.RangeScan(100, 200, [&](std::int32_t k, Rid) { sub_range.push_back(k); });
    std::size_t expected_sub = 0;
    for (auto& kv : reference) {
      if (kv.first >= 100 && kv.first <= 200) ++expected_sub;
    }
    CHECK_EQ(sub_range.size(), expected_sub);

    std::filesystem::remove(path);
  }

  // Reopening an existing index file picks up the same root/height.
  {
    std::string path = TempPath("bpt_reopen.bin");
    std::filesystem::remove(path);
    IoStats stats;
    DiskManager dm(&stats);
    BufferPool bp(&dm, 16, &stats);
    FileId file = dm.OpenFile(path, FileKind::kIndex);
    {
      BPlusTree tree(&bp, &dm, file);
      for (int i = 0; i < 20; ++i) tree.Insert(i, Rid{i, 0});
      bp.FlushAll();
    }
    {
      BPlusTree tree2(&bp, &dm, file);
      CHECK(!tree2.Empty());
      std::vector<Rid> r;
      tree2.EqualityScan(10, [&](Rid rid) { r.push_back(rid); });
      CHECK_EQ(r.size(), std::size_t{1});
      CHECK(r[0].page_no == 10);
    }
    std::filesystem::remove(path);
  }
TEST_MAIN_END
