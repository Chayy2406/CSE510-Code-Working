#include "gen/data_generator.h"

#include <algorithm>
#include <cmath>

namespace qe {

namespace {

std::int32_t Clamp(double v, std::int32_t lo, std::int32_t hi) {
  double r = std::round(v);
  if (r < lo) return lo;
  if (r > hi) return hi;
  return static_cast<std::int32_t>(r);
}

// Builds a Zipfian distribution over ranks [0, n): weight(i) = 1 / (i+1)^theta.
// theta == 0 degenerates to uniform.
std::discrete_distribution<std::size_t> MakeZipf(std::size_t n, double theta) {
  std::vector<double> weights(n);
  for (std::size_t i = 0; i < n; ++i) {
    weights[i] = 1.0 / std::pow(static_cast<double>(i + 1), theta);
  }
  return std::discrete_distribution<std::size_t>(weights.begin(), weights.end());
}

}  // namespace

std::vector<Tuple> DataGenerator::GenerateSailors(std::size_t count) {
  std::uniform_int_distribution<std::int32_t> age_dist(16, 70);
  std::uniform_real_distribution<double> rating_dist(1.0, 10.0);

  std::vector<Tuple> tuples;
  tuples.reserve(count);
  for (std::size_t sid = 0; sid < count; ++sid) {
    std::int32_t age = age_dist(rng_);
    double independent_rating = rating_dist(rng_);
    double corr_rating = 1.0 + (age - 16) / 54.0 * 9.0;
    std::int32_t rating = Clamp(
        independent_rating * (1.0 - config_.corr) + corr_rating * config_.corr,
        1, 10);
    tuples.push_back({static_cast<std::int32_t>(sid), rating, age});
  }
  return tuples;
}

std::vector<Tuple> DataGenerator::GenerateBoats(std::size_t count) {
  std::int32_t k = static_cast<std::int32_t>(config_.boat_colors);
  std::uniform_int_distribution<std::int32_t> color_dist(0, k - 1);

  std::vector<Tuple> tuples;
  tuples.reserve(count);
  for (std::size_t bid = 0; bid < count; ++bid) {
    std::int32_t independent_color = color_dist(rng_);
    double corr_color =
        count > 1 ? static_cast<double>(bid) / (count - 1) * (k - 1) : 0.0;
    std::int32_t color = Clamp(
        independent_color * (1.0 - config_.corr) + corr_color * config_.corr, 0,
        k - 1);
    tuples.push_back({static_cast<std::int32_t>(bid), color});
  }
  return tuples;
}

std::vector<Tuple> DataGenerator::GenerateReserves(std::size_t count,
                                                     std::size_t num_sailors,
                                                     std::size_t num_boats) {
  std::uniform_int_distribution<std::int32_t> day_dist(1, 365);
  std::uniform_int_distribution<std::size_t> uniform_sid(0, num_sailors - 1);
  std::uniform_int_distribution<std::size_t> uniform_bid(0, num_boats - 1);
  bool zipf = config_.key_dist == "zipf";
  auto zipf_sid = zipf ? MakeZipf(num_sailors, config_.zipf_theta)
                        : std::discrete_distribution<std::size_t>();
  auto zipf_bid = zipf ? MakeZipf(num_boats, config_.zipf_theta)
                        : std::discrete_distribution<std::size_t>();

  std::vector<Tuple> tuples;
  tuples.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::size_t sid = zipf ? zipf_sid(rng_) : uniform_sid(rng_);
    std::size_t bid = zipf ? zipf_bid(rng_) : uniform_bid(rng_);
    tuples.push_back({static_cast<std::int32_t>(sid), static_cast<std::int32_t>(bid),
                       day_dist(rng_)});
  }
  return tuples;
}

std::vector<Tuple> DataGenerator::ApplyOrder(std::vector<Tuple> tuples,
                                              std::size_t key_col) {
  if (config_.order == "sorted") {
    std::stable_sort(tuples.begin(), tuples.end(),
                      [key_col](const Tuple& a, const Tuple& b) {
                        return a[key_col] < b[key_col];
                      });
  } else {
    std::shuffle(tuples.begin(), tuples.end(), rng_);
  }
  return tuples;
}

}  // namespace qe
