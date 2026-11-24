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
#include "plan/plan_builder.h"
#include "plan/qb_plan.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace {

std::string Arg(int argc, char** argv, const std::string& flag, const std::string& def) {
  for (int i = 1; i < argc - 1; ++i) {
    if (flag == argv[i]) return argv[i + 1];
  }
  return def;
}

void PrintResult(const std::string& plan_desc, const qe::SinkResult& result, double time_ms,
                  const qe::IoStats& stats, const qe::IoStats& before) {
  std::cout << "Plan: " << plan_desc << "\n";
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
}

// QA only, S outer / R inner fixed regardless of method (there is no
// join-order choice for a 2-table query — see RunQB for QB's two
// left-deep orders). --sailors-tuple-bytes / --reserves-tuple-bytes must
// match whatever gen_data used to write the data directory; the heap
// file format does not record its own tuple width.
int RunQA(int argc, char** argv) {
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
    // The index file persists across separate run_query invocations on
    // the same --data-dir (that's the point: build once, reuse across
    // many plan runs), so only insert if it's actually empty — otherwise
    // a second invocation would insert every row a second time on top of
    // the first, duplicating every index entry.
    s_idx_file = dm.OpenFile(data_dir + "/S_sid.idx", qe::FileKind::kIndex);
    s_index = std::make_unique<qe::BPlusTree>(&bp, &dm, s_idx_file);
    if (s_index->Empty()) {
      s_heap.Scan([&](qe::Rid rid, const qe::Tuple& t) { s_index->Insert(t[0], rid); });
    }
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
  double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  std::string plan_shape =
      (method == "inlj") ? "R outer, S inner (indexed)" : "S outer, R inner";
  std::string plan_desc = plan_name + "(" + plan_shape + ") on sid, S.rating > " +
                           std::to_string(rmin) + " AND R.day >= " + std::to_string(dmin) +
                           ", buffer_pages=" + std::to_string(buffer_pages);
  PrintResult(plan_desc, result, time_ms, stats, before);
  return 0;
}

// QB: three tables, two left-deep join orders, per-join method, and a
// pushdown on/off toggle (SPEC.md section 8). The plan tree itself is
// built by BuildQBPlan (src/plan/qb_plan.h) and turned into an operator
// tree by PlanBuilder — see docs/PLAN.md Phase 7.
int RunQB(int argc, char** argv) {
  std::string data_dir = Arg(argc, argv, "--data-dir", "data");
  std::string join_order = Arg(argc, argv, "--join-order", "order1");
  std::string method1 = Arg(argc, argv, "--method1", "bnlj");
  std::string method2 = Arg(argc, argv, "--method2", "bnlj");
  bool pushdown = Arg(argc, argv, "--pushdown", "on") == "on";
  std::size_t buffer_pages = std::stoul(Arg(argc, argv, "--buffer-pages", "10"));
  std::int32_t rmin = std::stoi(Arg(argc, argv, "--rmin", "5"));
  std::int32_t amax = std::stoi(Arg(argc, argv, "--amax", "60"));
  std::int32_t color = std::stoi(Arg(argc, argv, "--color", "1"));
  std::int32_t d1 = std::stoi(Arg(argc, argv, "--d1", "1"));
  std::int32_t d2 = std::stoi(Arg(argc, argv, "--d2", "365"));
  std::size_t sailors_tuple_bytes =
      std::stoul(Arg(argc, argv, "--sailors-tuple-bytes", "50"));
  std::size_t reserves_tuple_bytes =
      std::stoul(Arg(argc, argv, "--reserves-tuple-bytes", "100"));
  std::size_t boats_tuple_bytes = std::stoul(Arg(argc, argv, "--boats-tuple-bytes", "50"));

  qe::IoStats stats;
  qe::DiskManager dm(&stats);
  qe::BufferPool bp(&dm, buffer_pages, &stats);

  qe::FileId s_file = dm.OpenFile(data_dir + "/S.heap", qe::FileKind::kBase);
  qe::FileId r_file = dm.OpenFile(data_dir + "/R.heap", qe::FileKind::kBase);
  qe::FileId b_file = dm.OpenFile(data_dir + "/B.heap", qe::FileKind::kBase);
  qe::Schema s_schema = qe::SailorsSchema(sailors_tuple_bytes);
  qe::Schema r_schema = qe::ReservesSchema(reserves_tuple_bytes);
  qe::Schema b_schema = qe::BoatsSchema(boats_tuple_bytes);
  qe::HeapFile s_heap(&bp, &dm, s_file, &s_schema);
  qe::HeapFile r_heap(&bp, &dm, r_file, &r_schema);
  qe::HeapFile b_heap(&bp, &dm, b_file, &b_schema);

  // Index build (setup, before the measured run) — needed whenever
  // either join uses INLJ, which this project only supports probing a
  // base table's own unique key (S.sid, B.bid). The index file persists
  // across separate run_query invocations on the same --data-dir, so
  // only insert if it's actually empty (see the matching comment in
  // RunQA for why: otherwise a second INLJ-using invocation duplicates
  // every index entry on top of the first).
  qe::FileId s_idx_file = dm.OpenFile(data_dir + "/S_sid.idx", qe::FileKind::kIndex);
  qe::FileId b_idx_file = dm.OpenFile(data_dir + "/B_bid.idx", qe::FileKind::kIndex);
  qe::BPlusTree s_index(&bp, &dm, s_idx_file);
  qe::BPlusTree b_index(&bp, &dm, b_idx_file);
  if ((method1 == "inlj" || method2 == "inlj") && s_index.Empty()) {
    s_heap.Scan([&](qe::Rid rid, const qe::Tuple& t) { s_index.Insert(t[0], rid); });
    b_heap.Scan([&](qe::Rid rid, const qe::Tuple& t) { b_index.Insert(t[0], rid); });
  }

  qe::PlanBuilder builder(&bp, &dm, buffer_pages, data_dir + "/tmp_qb", &stats);
  builder.BindTable("S", &s_heap, {"S.sid", "S.rating", "S.age"});
  builder.BindTable("R", &r_heap, {"R.sid", "R.bid", "R.day"});
  builder.BindTable("B", &b_heap, {"B.bid", "B.color"});
  builder.BindIndex("S", "S.sid", &s_index);
  builder.BindIndex("B", "B.bid", &b_index);

  auto tree = qe::BuildQBPlan(join_order, method1, method2, pushdown, rmin, amax, color, d1, d2);
  std::string plan_text = builder.Describe(*tree);

  // bp.Reset() must happen before Build(), not after: Build() eagerly
  // executes and materializes every non-outermost join as part of
  // constructing the tree (PlanBuilder::Materialize), so that I/O (and
  // its wall-clock time) has to be inside the measured window too, not
  // just the final Sink::Run() over the outermost join.
  bp.Reset();
  qe::IoStats before = stats;
  auto start = std::chrono::steady_clock::now();

  qe::Operator* top = builder.Build(*tree);
  qe::Projection projection(top, {"S.sid", "R.day", "B.color"});
  qe::Sink sink(&projection);
  qe::SinkResult result = sink.Run();

  auto end = std::chrono::steady_clock::now();
  double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  std::string plan_desc = plan_text + ", pushdown=" + std::string(pushdown ? "on" : "off") +
                           ", buffer_pages=" + std::to_string(buffer_pages);
  PrintResult(plan_desc, result, time_ms, stats, before);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string query = Arg(argc, argv, "--query", "qa");
  if (query == "qa") return RunQA(argc, argv);
  if (query == "qb") return RunQB(argc, argv);
  std::cerr << "run_query: unknown --query '" << query << "' (want qa|qb)\n";
  return 1;
}
