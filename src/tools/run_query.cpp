#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include "catalog/catalog.h"
#include "common/stats.h"
#include "exec/bnlj.h"
#include "exec/external_sort.h"
#include "exec/ghj.h"
#include "exec/group_by.h"
#include "exec/inlj.h"
#include "exec/projection.h"
#include "exec/selection.h"
#include "exec/seq_scan.h"
#include "exec/sink.h"
#include "exec/smj.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
#include "index/bplus_tree.h"
#include "plan/cost_estimator.h"
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
// tree by PlanBuilder — see docs/PLAN.md Phase 7. QC and QD (below)
// share this same setup: same tables, same join tree, different
// wrapping on top (GroupBy+HAVING for QC, a sort delivery check for QD).
struct QBSetup {
  qe::IoStats stats;
  qe::DiskManager dm{&stats};
  qe::BufferPool bp;
  qe::FileId s_file, r_file, b_file;
  qe::Schema s_schema, r_schema, b_schema;
  qe::HeapFile s_heap, r_heap, b_heap;
  qe::FileId s_idx_file, b_idx_file;
  qe::BPlusTree s_index, b_index;
  qe::PlanBuilder builder;
  std::unique_ptr<qe::PlanNode> tree;
  std::string plan_text;

  QBSetup(const std::string& data_dir, std::size_t buffer_pages, std::size_t s_tb,
          std::size_t r_tb, std::size_t b_tb, const std::string& join_order,
          const std::string& method1, const std::string& method2, bool pushdown,
          std::int32_t rmin, std::int32_t amax, std::int32_t color, std::int32_t d1,
          std::int32_t d2)
      : bp(&dm, buffer_pages, &stats),
        s_file(dm.OpenFile(data_dir + "/S.heap", qe::FileKind::kBase)),
        r_file(dm.OpenFile(data_dir + "/R.heap", qe::FileKind::kBase)),
        b_file(dm.OpenFile(data_dir + "/B.heap", qe::FileKind::kBase)),
        s_schema(qe::SailorsSchema(s_tb)),
        r_schema(qe::ReservesSchema(r_tb)),
        b_schema(qe::BoatsSchema(b_tb)),
        s_heap(&bp, &dm, s_file, &s_schema),
        r_heap(&bp, &dm, r_file, &r_schema),
        b_heap(&bp, &dm, b_file, &b_schema),
        s_idx_file(dm.OpenFile(data_dir + "/S_sid.idx", qe::FileKind::kIndex)),
        b_idx_file(dm.OpenFile(data_dir + "/B_bid.idx", qe::FileKind::kIndex)),
        s_index(&bp, &dm, s_idx_file),
        b_index(&bp, &dm, b_idx_file),
        builder(&bp, &dm, buffer_pages, data_dir + "/tmp_qplan", &stats) {
    // Index build (setup, before the measured run) — needed whenever
    // either join uses INLJ. The index file persists across separate
    // run_query invocations on the same --data-dir, so only insert if
    // it's actually empty (see RunQA's comment for why).
    if ((method1 == "inlj" || method2 == "inlj") && s_index.Empty()) {
      s_heap.Scan([this](qe::Rid rid, const qe::Tuple& t) { s_index.Insert(t[0], rid); });
      b_heap.Scan([this](qe::Rid rid, const qe::Tuple& t) { b_index.Insert(t[0], rid); });
    }
    builder.BindTable("S", &s_heap, {"S.sid", "S.rating", "S.age"});
    builder.BindTable("R", &r_heap, {"R.sid", "R.bid", "R.day"});
    builder.BindTable("B", &b_heap, {"B.bid", "B.color"});
    builder.BindIndex("S", "S.sid", &s_index);
    builder.BindIndex("B", "B.bid", &b_index);
    tree = qe::BuildQBPlan(join_order, method1, method2, pushdown, rmin, amax, color, d1, d2);
    plan_text = builder.Describe(*tree);
  }
};

struct QBArgs {
  std::string data_dir, join_order, method1, method2;
  bool pushdown;
  std::size_t buffer_pages, sailors_tuple_bytes, reserves_tuple_bytes, boats_tuple_bytes;
  std::int32_t rmin, amax, color, d1, d2;

  static QBArgs Parse(int argc, char** argv) {
    QBArgs a;
    a.data_dir = Arg(argc, argv, "--data-dir", "data");
    a.join_order = Arg(argc, argv, "--join-order", "order1");
    a.method1 = Arg(argc, argv, "--method1", "bnlj");
    a.method2 = Arg(argc, argv, "--method2", "bnlj");
    a.pushdown = Arg(argc, argv, "--pushdown", "on") == "on";
    a.buffer_pages = std::stoul(Arg(argc, argv, "--buffer-pages", "10"));
    a.rmin = std::stoi(Arg(argc, argv, "--rmin", "5"));
    a.amax = std::stoi(Arg(argc, argv, "--amax", "60"));
    a.color = std::stoi(Arg(argc, argv, "--color", "1"));
    a.d1 = std::stoi(Arg(argc, argv, "--d1", "1"));
    a.d2 = std::stoi(Arg(argc, argv, "--d2", "365"));
    a.sailors_tuple_bytes = std::stoul(Arg(argc, argv, "--sailors-tuple-bytes", "50"));
    a.reserves_tuple_bytes = std::stoul(Arg(argc, argv, "--reserves-tuple-bytes", "100"));
    a.boats_tuple_bytes = std::stoul(Arg(argc, argv, "--boats-tuple-bytes", "50"));
    return a;
  }

  std::unique_ptr<QBSetup> MakeSetup() const {
    return std::make_unique<QBSetup>(data_dir, buffer_pages, sailors_tuple_bytes,
                                      reserves_tuple_bytes, boats_tuple_bytes, join_order,
                                      method1, method2, pushdown, rmin, amax, color, d1, d2);
  }
};

// Estimates QB's predicted cardinalities, per-join I/O, and each join's
// rank among all four methods at that step (SPEC.md section 8). This
// estimates the *given* plan; it does not search the plan space
// (explicitly out of scope — SPEC.md section 13). Index-lookup cost c
// is height (estimated from catalog tuple counts, since both indexed
// columns are unique keys) plus one data-page fetch.
struct QBEstimate {
  double s_card, r_card, b_card;
  double join1_card, join1_io;
  double join2_card, join2_io;
  int join1_rank, join2_rank;
  double TotalIO() const { return join1_io + join2_io; }
};

QBEstimate EstimateQB(const QBArgs& a, const QBSetup& setup) {
  qe::Catalog catalog;
  catalog.ComputeTableStats("S", setup.s_heap, setup.s_schema);
  catalog.ComputeTableStats("R", setup.r_heap, setup.r_schema);
  catalog.ComputeTableStats("B", setup.b_heap, setup.b_schema);
  const qe::TableStats& s_stats = catalog.GetTableStats("S");
  const qe::TableStats& r_stats = catalog.GetTableStats("R");
  const qe::TableStats& b_stats = catalog.GetTableStats("B");

  qe::CostEstimator est;
  est.SetTable("S", s_stats.tuple_count, s_stats.page_count);
  est.SetTable("R", r_stats.tuple_count, r_stats.page_count);
  est.SetTable("B", b_stats.tuple_count, b_stats.page_count);
  est.SetColumnStats("S.sid", s_stats.columns[0].min_value, s_stats.columns[0].max_value,
                     s_stats.columns[0].distinct_count);
  est.SetColumnStats("S.rating", s_stats.columns[1].min_value, s_stats.columns[1].max_value,
                     s_stats.columns[1].distinct_count);
  est.SetColumnStats("S.age", s_stats.columns[2].min_value, s_stats.columns[2].max_value,
                     s_stats.columns[2].distinct_count);
  est.SetColumnStats("R.sid", r_stats.columns[0].min_value, r_stats.columns[0].max_value,
                     r_stats.columns[0].distinct_count);
  est.SetColumnStats("R.bid", r_stats.columns[1].min_value, r_stats.columns[1].max_value,
                     r_stats.columns[1].distinct_count);
  est.SetColumnStats("R.day", r_stats.columns[2].min_value, r_stats.columns[2].max_value,
                     r_stats.columns[2].distinct_count);
  est.SetColumnStats("B.bid", b_stats.columns[0].min_value, b_stats.columns[0].max_value,
                     b_stats.columns[0].distinct_count);
  est.SetColumnStats("B.color", b_stats.columns[1].min_value, b_stats.columns[1].max_value,
                     b_stats.columns[1].distinct_count);

  std::vector<qe::NamedPredicate> s_preds = {{"S.rating", qe::CompareOp::kGt, a.rmin},
                                              {"S.age", qe::CompareOp::kLt, a.amax}};
  std::vector<qe::NamedPredicate> r_preds = {{"R.day", qe::CompareOp::kGe, a.d1},
                                              {"R.day", qe::CompareOp::kLe, a.d2}};
  std::vector<qe::NamedPredicate> b_preds = {{"B.color", qe::CompareOp::kEq, a.color}};

  QBEstimate e;
  e.s_card = a.pushdown ? est.EstimateScanCardinality("S", s_preds) : est.TupleCount("S");
  e.r_card = a.pushdown ? est.EstimateScanCardinality("R", r_preds) : est.TupleCount("R");
  e.b_card = a.pushdown ? est.EstimateScanCardinality("B", b_preds) : est.TupleCount("B");

  auto index_lookup_cost = [](double inner_tuple_count) {
    constexpr double kFanout = 300.0;
    double height = inner_tuple_count <= 1 ? 1.0 : std::ceil(std::log(inner_tuple_count) /
                                                              std::log(kFanout));
    return std::max(1.0, height) + 1;  // + one data page fetch (unique key)
  };

  auto rank_of = [&](const std::string& method, double M, double N, double outer_card, double c) {
    std::vector<std::pair<std::string, double>> ranked;
    for (const char* m : {"bnlj", "smj", "ghj", "inlj"}) {
      ranked.push_back({m, qe::CostEstimator::IOForMethod(m, M, N, a.buffer_pages, outer_card, c)});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](auto& x, auto& y) { return x.second < y.second; });
    for (std::size_t i = 0; i < ranked.size(); ++i) {
      if (ranked[i].first == method) return static_cast<int>(i) + 1;
    }
    return -1;
  };

  // Join1: R outer, (S or B) inner, depending on join order.
  bool order1 = (a.join_order == "order1");
  std::string inner1 = order1 ? "S" : "B";
  double inner1_card = order1 ? e.s_card : e.b_card;
  std::size_t r_key_distinct = est.DistinctCount(order1 ? "R.sid" : "R.bid");
  std::size_t inner1_key_distinct = est.DistinctCount(order1 ? "S.sid" : "B.bid");
  double join1_M = est.PageCount("R");
  double join1_N = est.PageCount(inner1);
  double c1 = index_lookup_cost(order1 ? s_stats.tuple_count : b_stats.tuple_count);
  e.join1_card =
      qe::CostEstimator::EstimateJoinCardinality(e.r_card, inner1_card, r_key_distinct,
                                                 inner1_key_distinct);
  e.join1_io = qe::CostEstimator::IOForMethod(a.method1, join1_M, join1_N, a.buffer_pages,
                                              e.r_card, c1);
  e.join1_rank = rank_of(a.method1, join1_M, join1_N, e.r_card, c1);

  // Join2: join1's result outer, whichever of S/B wasn't join1's inner.
  std::string inner2 = order1 ? "B" : "S";
  double inner2_card = order1 ? e.b_card : e.s_card;
  std::size_t inner2_key_distinct = est.DistinctCount(order1 ? "B.bid" : "S.sid");
  std::size_t join1_cols = order1 ? 6 : 5;  // R(3)+S(3) or R(3)+B(2)
  double join2_M = qe::CostEstimator::PagesForCardinality(e.join1_card, join1_cols);
  double join2_N = est.PageCount(inner2);
  double c2 = index_lookup_cost(order1 ? b_stats.tuple_count : s_stats.tuple_count);
  e.join2_card =
      qe::CostEstimator::EstimateJoinCardinality(e.join1_card, inner2_card, r_key_distinct,
                                                 inner2_key_distinct);
  e.join2_io = qe::CostEstimator::IOForMethod(a.method2, join2_M, join2_N, a.buffer_pages,
                                              e.join1_card, c2);
  e.join2_rank = rank_of(a.method2, join2_M, join2_N, e.join1_card, c2);
  return e;
}

void PrintExplain(const QBArgs& a, const QBEstimate& e) {
  std::cout << "Explain:\n";
  std::cout << "  Scan S: est_rows=" << e.s_card << "\n";
  std::cout << "  Scan R: est_rows=" << e.r_card << "\n";
  std::cout << "  Scan B: est_rows=" << e.b_card << "\n";
  std::cout << "  Join1 " << a.method1 << ": est_rows=" << e.join1_card
            << " est_io=" << e.join1_io << " rank=" << e.join1_rank << " of 4\n";
  std::cout << "  Join2 " << a.method2 << ": est_rows=" << e.join2_card
            << " est_io=" << e.join2_io << " rank=" << e.join2_rank << " of 4\n";
  std::cout << "  Total estimated I/O: " << e.TotalIO() << "\n";
}

int RunQB(int argc, char** argv) {
  QBArgs a = QBArgs::Parse(argc, argv);
  bool explain = Arg(argc, argv, "--explain", "off") == "on";
  auto setup = a.MakeSetup();

  QBEstimate estimate;
  if (explain) {
    estimate = EstimateQB(a, *setup);
    PrintExplain(a, estimate);
  }

  // bp.Reset() must happen before Build(), not after: Build() eagerly
  // executes and materializes every non-outermost join as part of
  // constructing the tree (PlanBuilder::Materialize), so that I/O (and
  // its wall-clock time) has to be inside the measured window too, not
  // just the final Sink::Run() over the outermost join.
  setup->bp.Reset();
  qe::IoStats before = setup->stats;
  auto start = std::chrono::steady_clock::now();

  qe::Operator* top = setup->builder.Build(*setup->tree);
  qe::Projection projection(top, {"S.sid", "R.day", "B.color"});
  qe::Sink sink(&projection);
  qe::SinkResult result = sink.Run();

  auto end = std::chrono::steady_clock::now();
  double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  std::string plan_desc = setup->plan_text +
                           ", pushdown=" + std::string(a.pushdown ? "on" : "off") +
                           ", buffer_pages=" + std::to_string(a.buffer_pages);
  PrintResult(plan_desc, result, time_ms, setup->stats, before);
  if (explain) {
    double measured_io = static_cast<double>((setup->stats.page_reads - before.page_reads) +
                                              (setup->stats.page_writes - before.page_writes));
    std::cout << "Measured/Estimated I/O ratio: " << (measured_io / estimate.TotalIO()) << "\n";
  }
  return 0;
}

// QC: QB's join and predicates, GROUP BY S.rating, HAVING COUNT(*) > h
// (SPEC.md section 7). GroupBy is an in-memory hash aggregate (SPEC.md
// section 6: small group count — S.rating has 10 distinct values).
int RunQC(int argc, char** argv) {
  QBArgs a = QBArgs::Parse(argc, argv);
  std::int32_t h = std::stoi(Arg(argc, argv, "--h", "0"));
  auto setup = a.MakeSetup();

  setup->bp.Reset();
  qe::IoStats before = setup->stats;
  auto start = std::chrono::steady_clock::now();

  qe::Operator* top = setup->builder.Build(*setup->tree);
  qe::GroupBy group_by(top, "S.rating");
  qe::Selection having(&group_by, {qe::Predicate{1, qe::CompareOp::kGt, h}});
  qe::Sink sink(&having);
  qe::SinkResult result = sink.Run();

  auto end = std::chrono::steady_clock::now();
  double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  std::string plan_desc = "GroupBy(S.rating, COUNT(*)) HAVING > " + std::to_string(h) + " on " +
                           setup->plan_text +
                           ", pushdown=" + std::string(a.pushdown ? "on" : "off") +
                           ", buffer_pages=" + std::to_string(a.buffer_pages);
  PrintResult(plan_desc, result, time_ms, setup->stats, before);
  return 0;
}

// QD: QB's join and predicates, ORDER BY S.sid (SPEC.md section 7). An
// SMJ final join on sid (join_order=order2, method2=smj) delivers this
// order for free, since a sort-merge join emits tuples in join-key
// order; any other combination needs an explicit final sort.
int RunQD(int argc, char** argv) {
  QBArgs a = QBArgs::Parse(argc, argv);
  auto setup = a.MakeSetup();

  setup->bp.Reset();
  qe::IoStats before = setup->stats;
  auto start = std::chrono::steady_clock::now();

  qe::Operator* top = setup->builder.Build(*setup->tree);

  bool free_order = (a.join_order == "order2" && a.method2 == "smj");
  std::unique_ptr<qe::Schema> sort_schema;
  std::unique_ptr<qe::ExternalSort> sorter;
  std::unique_ptr<qe::FusedMerge> merged;
  qe::Operator* ordered = top;
  std::string order_desc = "already sorted by S.sid (SMJ on sid)";

  if (!free_order) {
    const std::vector<std::string>& schema = top->OutputSchema();
    auto it = std::find(schema.begin(), schema.end(), "S.sid");
    std::size_t sid_col = static_cast<std::size_t>(it - schema.begin());
    sort_schema = std::make_unique<qe::Schema>(schema, schema.size() * sizeof(std::int32_t));
    sorter = std::make_unique<qe::ExternalSort>(top, sid_col, sort_schema.get(), schema,
                                                 a.buffer_pages, &setup->bp, &setup->dm,
                                                 a.data_dir + "/tmp_qd_sort");
    qe::SortedRunSet runs = sorter->Sort();
    merged = std::make_unique<qe::FusedMerge>(runs, schema);
    ordered = merged.get();
    order_desc = "explicit final sort by S.sid";
  }

  qe::Projection projection(ordered, {"S.sid", "R.day", "B.color"});

  // SPEC.md section 9: "for QD also verify ordering by S.sid" — checked
  // here directly (column 0 of the projection) rather than via Sink,
  // which only tracks the order-insensitive checksum.
  projection.Open();
  qe::SinkResult result;
  bool order_ok = true;
  std::int32_t prev_sid = std::numeric_limits<std::int32_t>::min();
  qe::Tuple t;
  while (projection.Next(&t)) {
    if (t[0] < prev_sid) order_ok = false;
    prev_sid = t[0];
    ++result.row_count;
    result.checksum = qe::AccumulateChecksum(result.checksum, t);
  }
  projection.Close();

  auto end = std::chrono::steady_clock::now();
  double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  std::string plan_desc = "OrderBy(S.sid) [" + order_desc + "] on " + setup->plan_text +
                           ", pushdown=" + std::string(a.pushdown ? "on" : "off") +
                           ", buffer_pages=" + std::to_string(a.buffer_pages);
  PrintResult(plan_desc, result, time_ms, setup->stats, before);
  std::cout << "OrderOk: " << (order_ok ? "true" : "false") << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string query = Arg(argc, argv, "--query", "qa");
  if (query == "qa") return RunQA(argc, argv);
  if (query == "qb") return RunQB(argc, argv);
  if (query == "qc") return RunQC(argc, argv);
  if (query == "qd") return RunQD(argc, argv);
  std::cerr << "run_query: unknown --query '" << query << "' (want qa|qb|qc|qd)\n";
  return 1;
}
