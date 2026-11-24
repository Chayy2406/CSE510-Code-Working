#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "common/stats.h"
#include "exec/bnlj.h"
#include "exec/ghj.h"
#include "exec/inlj.h"
#include "exec/projection.h"
#include "exec/selection.h"
#include "exec/seq_scan.h"
#include "exec/sink.h"
#include "exec/smj.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "index/bplus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace {

std::string Arg(int argc, char** argv, const std::string& flag, const std::string& def) {
  for (int i = 1; i < argc - 1; ++i) {
    if (flag == argv[i]) return argv[i + 1];
  }
  return def;
}

}  // namespace

// QA only, S outer / R inner fixed regardless of method (Phase 3/4: there
// is no plan builder yet — see docs/PLAN.md Phase 7 for join-order
// choice). --sailors-tuple-bytes / --reserves-tuple-bytes must match
// whatever gen_data used to write the data directory; the heap file
// format does not record its own tuple width.
int main(int argc, char** argv) {
  std::string data_dir = Arg(argc, argv, "--data-dir", "data");
  std::string method = Arg(argc, argv, "--method", "bnlj");
  std::size_t buffer_pages = std::stoul(Arg(argc, argv, "--buffer-pages", "10"));
  std::int32_t rmin = std::stoi(Arg(argc, argv, "--rmin", "5"));
  std::int32_t dmin = std::stoi(Arg(argc, argv, "--dmin", "1"));
  std::size_t sailors_tuple_bytes =
      std::stoul(Arg(argc, argv, "--sailors-tuple-bytes", "50"));
  std::size_t reserves_tuple_bytes =
      std::stoul(Arg(argc, argv, "--reserves-tuple-bytes", "100"));

  qe::IoStats stats;
  qe::DiskManager dm(&stats);
  qe::BufferPool bp(&dm, buffer_pages, &stats);

  qe::FileId s_file = dm.OpenFile(data_dir + "/S.heap", qe::FileKind::kBase);
  qe::FileId r_file = dm.OpenFile(data_dir + "/R.heap", qe::FileKind::kBase);
  qe::Schema s_schema = qe::SailorsSchema(sailors_tuple_bytes);
  qe::Schema r_schema = qe::ReservesSchema(reserves_tuple_bytes);
  qe::HeapFile s_heap(&bp, &dm, s_file, &s_schema);
  qe::HeapFile r_heap(&bp, &dm, r_file, &r_schema);

  // BNLJ reads directly from the heap files with its own pushed-down
  // predicates (see bnlj.h); SMJ/GHJ take generic operators, so the same
  // predicates are applied via a SeqScan+Selection pipeline instead.
  qe::SeqScan s_scan(&s_heap, {"S.sid", "S.rating", "S.age"});
  qe::Selection s_sel(&s_scan, {qe::Predicate{1, qe::CompareOp::kGt, rmin}});
  qe::SeqScan r_scan(&r_heap, {"R.sid", "R.bid", "R.day"});
  qe::Selection r_sel(&r_scan, {qe::Predicate{2, qe::CompareOp::kGe, dmin}});

  std::unique_ptr<qe::BNLJ> bnlj;
  std::unique_ptr<qe::SMJ> smj;
  std::unique_ptr<qe::GHJ> ghj;
  std::unique_ptr<qe::INLJ> inlj;
  std::unique_ptr<qe::Selection> inlj_post_filter;
  qe::FileId s_idx_file = qe::kInvalidFileId;
  std::unique_ptr<qe::BPlusTree> s_index;
  qe::Operator* join = nullptr;
  std::string plan_name;

  if (method == "inlj") {
    // Index build is a one-time setup step (SPEC.md section 12: "build
    // indices and statistics" happens before "run all plans"), so it
    // runs before bp.Reset() below and is not part of the measured I/O.
    s_idx_file = dm.OpenFile(data_dir + "/S_sid.idx", qe::FileKind::kIndex);
    s_index = std::make_unique<qe::BPlusTree>(&bp, &dm, s_idx_file);
    s_heap.Scan([&](qe::Rid rid, const qe::Tuple& t) { s_index->Insert(t[0], rid); });
  }

  if (method == "bnlj") {
    bnlj = std::make_unique<qe::BNLJ>(
        &s_heap, std::vector<std::string>{"S.sid", "S.rating", "S.age"},
        std::vector<qe::Predicate>{qe::Predicate{1, qe::CompareOp::kGt, rmin}}, 0,
        &r_heap, std::vector<std::string>{"R.sid", "R.bid", "R.day"},
        std::vector<qe::Predicate>{qe::Predicate{2, qe::CompareOp::kGe, dmin}}, 0,
        buffer_pages);
    join = bnlj.get();
    plan_name = "BNLJ";
  } else if (method == "smj") {
    smj = std::make_unique<qe::SMJ>(&s_sel, 0, &s_schema,
                                     std::vector<std::string>{"S.sid", "S.rating", "S.age"},
                                     &r_sel, 0, &r_schema,
                                     std::vector<std::string>{"R.sid", "R.bid", "R.day"},
                                     buffer_pages, &bp, &dm, data_dir + "/tmp_smj");
    join = smj.get();
    plan_name = "SMJ";
  } else if (method == "ghj") {
    ghj = std::make_unique<qe::GHJ>(&s_sel, 0, &s_schema,
                                     std::vector<std::string>{"S.sid", "S.rating", "S.age"},
                                     &r_sel, 0, &r_schema,
                                     std::vector<std::string>{"R.sid", "R.bid", "R.day"},
                                     buffer_pages, &bp, &dm, data_dir + "/tmp_ghj", &stats);
    join = ghj.get();
    plan_name = "GHJ";
  } else if (method == "inlj") {
    // R (outer) lacks an index, so it drives the probe; S (inner) is
    // indexed on sid. S.rating > rmin can't be pushed into the index
    // lookup (the index is keyed on sid, not rating), so it is applied
    // as a post-filter on INLJ's output instead (column 4: R has 3
    // columns, so S.rating is outer_names.size() + 1 = 4).
    inlj = std::make_unique<qe::INLJ>(&r_sel, 0, std::vector<std::string>{"R.sid", "R.bid", "R.day"},
                                       s_index.get(), &s_heap,
                                       std::vector<std::string>{"S.sid", "S.rating", "S.age"});
    inlj_post_filter =
        std::make_unique<qe::Selection>(inlj.get(), std::vector<qe::Predicate>{
                                                          qe::Predicate{4, qe::CompareOp::kGt, rmin}});
    join = inlj_post_filter.get();
    plan_name = "INLJ";
  } else {
    std::cerr << "run_query: unknown --method '" << method << "' (want bnlj|smj|ghj|inlj)\n";
    return 1;
  }

  qe::Projection projection(join, {"S.sid", "S.rating", "R.bid", "R.day"});

  bp.Reset();  // start the measured run with a cold buffer pool
  qe::IoStats before = stats;

  auto start = std::chrono::steady_clock::now();
  qe::Sink sink(&projection);
  qe::SinkResult result = sink.Run();
  auto end = std::chrono::steady_clock::now();
  double time_ms =
      std::chrono::duration<double, std::milli>(end - start).count();

  std::string plan_shape = (method == "inlj") ? "R outer, S inner (indexed)"
                                               : "S outer, R inner";
  std::cout << "Plan: " << plan_name << "(" << plan_shape << ") on sid, "
            << "S.rating > " << rmin << " AND R.day >= " << dmin
            << ", buffer_pages=" << buffer_pages << "\n";
  std::cout << "Rows: " << result.row_count << "\n";
  std::cout << "Time: " << time_ms << " ms\n";
  std::cout << "Reads: " << (stats.page_reads - before.page_reads) << "\n";
  std::cout << "Writes: " << (stats.page_writes - before.page_writes) << "\n";
  std::cout << "ReadsBase: " << (stats.reads_base - before.reads_base) << "\n";
  std::cout << "ReadsIndex: " << (stats.reads_index - before.reads_index) << "\n";
  std::cout << "ReadsTemp: " << (stats.reads_temp - before.reads_temp) << "\n";
  std::cout << "WritesTemp: " << (stats.writes_temp - before.writes_temp) << "\n";
  std::cout << "BufferHits: " << (stats.buffer_hits - before.buffer_hits) << "\n";
  std::cout << "GhjOverflowPartitions: "
            << (stats.ghj_overflow_partitions - before.ghj_overflow_partitions) << "\n";
  std::cout << "Checksum: 0x" << std::hex << result.checksum << std::dec << "\n";
  return 0;
}
