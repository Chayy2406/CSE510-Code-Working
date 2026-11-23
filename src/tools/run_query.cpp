#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

#include "common/stats.h"
#include "exec/bnlj.h"
#include "exec/projection.h"
#include "exec/sink.h"
#include "heap/heap_file.h"
#include "heap/schema.h"
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

// QA only, BNLJ only, S outer / R inner fixed (Phase 3: there is no plan
// builder yet — see docs/PLAN.md Phase 7 for join-order/method choice).
// --sailors-tuple-bytes / --reserves-tuple-bytes must match whatever
// gen_data used to write the data directory; the heap file format does
// not record its own tuple width.
int main(int argc, char** argv) {
  std::string data_dir = Arg(argc, argv, "--data-dir", "data");
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

  qe::BNLJ bnlj(&s_heap, {"S.sid", "S.rating", "S.age"},
                {qe::Predicate{1, qe::CompareOp::kGt, rmin}}, 0, &r_heap,
                {"R.sid", "R.bid", "R.day"},
                {qe::Predicate{2, qe::CompareOp::kGe, dmin}}, 0, buffer_pages);
  qe::Projection projection(&bnlj, {"S.sid", "S.rating", "R.bid", "R.day"});

  bp.Reset();  // start the measured run with a cold buffer pool
  qe::IoStats before = stats;

  auto start = std::chrono::steady_clock::now();
  qe::Sink sink(&projection);
  qe::SinkResult result = sink.Run();
  auto end = std::chrono::steady_clock::now();
  double time_ms =
      std::chrono::duration<double, std::milli>(end - start).count();

  std::cout << "Plan: BNLJ(S outer, R inner) on sid, "
            << "S.rating > " << rmin << " AND R.day >= " << dmin
            << ", buffer_pages=" << buffer_pages << "\n";
  std::cout << "Rows: " << result.row_count << "\n";
  std::cout << "Time: " << time_ms << " ms\n";
  std::cout << "Reads: " << (stats.page_reads - before.page_reads) << "\n";
  std::cout << "Writes: " << (stats.page_writes - before.page_writes) << "\n";
  std::cout << "Checksum: 0x" << std::hex << result.checksum << std::dec << "\n";
  return 0;
}
