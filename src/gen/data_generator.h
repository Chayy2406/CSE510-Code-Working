#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "heap/schema.h"

namespace qe {

// Base (scale = 1.0) row counts from SPEC.md section 5.
constexpr std::size_t kBaseSailors = 40000;
constexpr std::size_t kBaseReserves = 100000;
constexpr std::size_t kBaseBoats = 100;

struct GenConfig {
  std::uint64_t seed = 42;
  double scale = 1.0;

  // "uniform" or "zipf"; applies to Reserves.sid and Reserves.bid.
  std::string key_dist = "uniform";
  double zipf_theta = 1.0;

  // 0 = independent, 1 = fully correlated. Blends Sailors.rating with
  // Sailors.age, and Boats.color with Boats.bid.
  double corr = 0.0;

  // "random" or "sorted" physical storage order.
  std::string order = "random";
  // Which Reserves column "sorted" order sorts by: "sid" or "bid".
  // Sailors and Boats are always clustered by their own id when sorted.
  std::string cluster_by = "sid";

  std::size_t sailors_tuple_bytes = 50;
  std::size_t reserves_tuple_bytes = 100;
  std::size_t boats_tuple_bytes = 50;
  std::size_t boat_colors = 5;
};

// Generates synthetic Sailors/Reserves/Boats data in memory, seeded so
// identical configs produce byte-identical tuple sequences every time
// (SPEC.md section 5). Sailors.sid and Boats.bid are dense, unique ids
// 0..count-1 [ASSUMPTION, see docs/DECISIONS.md]; Reserves references them
// directly by that id.
class DataGenerator {
 public:
  explicit DataGenerator(const GenConfig& config)
      : config_(config), rng_(config.seed) {}

  std::vector<Tuple> GenerateSailors(std::size_t count);
  std::vector<Tuple> GenerateBoats(std::size_t count);
  std::vector<Tuple> GenerateReserves(std::size_t count, std::size_t num_sailors,
                                       std::size_t num_boats);

  // Returns `tuples` reordered for physical storage per config_.order:
  // "sorted" sorts by column `key_col`, "random" shuffles deterministically.
  std::vector<Tuple> ApplyOrder(std::vector<Tuple> tuples, std::size_t key_col);

  std::size_t ScaledCount(std::size_t base) const {
    return static_cast<std::size_t>(base * config_.scale);
  }

 private:
  GenConfig config_;
  std::mt19937_64 rng_;
};

}  // namespace qe
