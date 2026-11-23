#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "catalog/catalog.h"
#include "common/stats.h"
#include "gen/data_generator.h"
#include "heap/heap_file.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace {

using qe::Tuple;

void WriteCsv(const std::string& path, const qe::Schema& schema,
              const std::vector<Tuple>& tuples) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("gen_data: cannot write " + path);
  for (std::size_t c = 0; c < schema.NumColumns(); ++c) {
    if (c > 0) out << ',';
    out << schema.ColumnName(c);
  }
  out << '\n';
  for (const Tuple& t : tuples) {
    for (std::size_t c = 0; c < t.size(); ++c) {
      if (c > 0) out << ',';
      out << t[c];
    }
    out << '\n';
  }
}

// Generates one table end to end: build tuples, apply physical order,
// append to a fresh heap file, write the CSV, and report its stats.
void BuildTable(const std::string& name, qe::BufferPool* bp, qe::DiskManager* dm,
                 const std::string& dir, const qe::Schema& schema,
                 std::vector<Tuple> tuples, std::size_t order_key_col,
                 qe::DataGenerator* gen, qe::Catalog* catalog) {
  std::string heap_path = dir + "/" + name + ".heap";
  std::string csv_path = dir + "/" + name + ".csv";
  std::filesystem::remove(heap_path);
  std::filesystem::remove(csv_path);

  tuples = gen->ApplyOrder(std::move(tuples), order_key_col);

  qe::FileId file = dm->OpenFile(heap_path, qe::FileKind::kBase);
  qe::HeapFile heap(bp, dm, file, &schema);
  for (const Tuple& t : tuples) heap.Append(t);

  WriteCsv(csv_path, schema, tuples);
  catalog->ComputeTableStats(name, heap, schema);
  const qe::TableStats& stats = catalog->GetTableStats(name);
  std::cout << name << ": " << stats.tuple_count << " tuples, "
            << stats.page_count << " pages\n";
}

std::string Arg(int argc, char** argv, const std::string& flag,
                 const std::string& def) {
  for (int i = 1; i < argc - 1; ++i) {
    if (flag == argv[i]) return argv[i + 1];
  }
  return def;
}

}  // namespace

int main(int argc, char** argv) {
  qe::GenConfig config;
  config.seed = std::stoull(Arg(argc, argv, "--seed", "42"));
  config.scale = std::stod(Arg(argc, argv, "--scale", "1.0"));
  config.key_dist = Arg(argc, argv, "--key-dist", "uniform");
  config.zipf_theta = std::stod(Arg(argc, argv, "--zipf-theta", "1.0"));
  config.corr = std::stod(Arg(argc, argv, "--corr", "0.0"));
  config.order = Arg(argc, argv, "--order", "random");
  config.cluster_by = Arg(argc, argv, "--cluster-by", "sid");
  config.sailors_tuple_bytes =
      std::stoul(Arg(argc, argv, "--sailors-tuple-bytes", "50"));
  config.reserves_tuple_bytes =
      std::stoul(Arg(argc, argv, "--reserves-tuple-bytes", "100"));
  config.boats_tuple_bytes =
      std::stoul(Arg(argc, argv, "--boats-tuple-bytes", "50"));
  std::string out_dir = Arg(argc, argv, "--out-dir", "data");

  std::filesystem::create_directories(out_dir);

  qe::IoStats io_stats;
  qe::DiskManager dm(&io_stats);
  qe::BufferPool bp(&dm, /*num_frames=*/64, &io_stats);
  qe::DataGenerator gen(config);
  qe::Catalog catalog;

  std::size_t num_sailors = gen.ScaledCount(qe::kBaseSailors);
  std::size_t num_boats = gen.ScaledCount(qe::kBaseBoats);
  std::size_t num_reserves = gen.ScaledCount(qe::kBaseReserves);

  qe::Schema sailors_schema = qe::SailorsSchema(config.sailors_tuple_bytes);
  qe::Schema boats_schema = qe::BoatsSchema(config.boats_tuple_bytes);
  qe::Schema reserves_schema = qe::ReservesSchema(config.reserves_tuple_bytes);

  BuildTable("S", &bp, &dm, out_dir, sailors_schema,
             gen.GenerateSailors(num_sailors), 0, &gen, &catalog);
  BuildTable("B", &bp, &dm, out_dir, boats_schema, gen.GenerateBoats(num_boats),
             0, &gen, &catalog);

  std::size_t reserves_key_col = reserves_schema.ColumnIndex(config.cluster_by);
  BuildTable("R", &bp, &dm, out_dir, reserves_schema,
             gen.GenerateReserves(num_reserves, num_sailors, num_boats),
             reserves_key_col, &gen, &catalog);

  return 0;
}
