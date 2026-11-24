#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "heap/schema.h"

namespace qe {

constexpr std::size_t kBaseSailors = 40000;
constexpr std::size_t kBaseReserves = 100000;
constexpr std::size_t kBaseBoats = 100;

struct GenConfig {
  std::uint64_t seed = 42;
  double scale = 1.0;

  std::string key_dist = "uniform";
  double zipf_theta = 1.0;

  double corr = 0.0;

  std::string order = "random";

  std::string cluster_by = "sid";

  std::size_t sailors_tuple_bytes = 50;
  std::size_t reserves_tuple_bytes = 100;
  std::size_t boats_tuple_bytes = 50;
  std::size_t boat_colors = 5;
};

class DataGenerator {
 public:
  explicit DataGenerator(const GenConfig& config)
      : config_(config), rng_(config.seed) {}

  std::vector<Tuple> GenerateSailors(std::size_t count);
  std::vector<Tuple> GenerateBoats(std::size_t count);
  std::vector<Tuple> GenerateReserves(std::size_t count, std::size_t num_sailors,
                                       std::size_t num_boats);

  std::vector<Tuple> ApplyOrder(std::vector<Tuple> tuples, std::size_t key_col);

  std::size_t ScaledCount(std::size_t base) const {
    return static_cast<std::size_t>(base * config_.scale);
  }

 private:
  GenConfig config_;
  std::mt19937_64 rng_;
};

}
