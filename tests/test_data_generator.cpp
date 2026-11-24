#include <algorithm>
#include <string>
#include <vector>

#include "gen/data_generator.h"
#include "test_util.h"

using qe::DataGenerator;
using qe::GenConfig;
using qe::Tuple;

TEST_MAIN_BEGIN

  {
    GenConfig config;
    config.seed = 1234;
    config.scale = 0.01;
    config.key_dist = "zipf";
    config.zipf_theta = 1.5;
    config.corr = 0.5;
    config.order = "random";

    DataGenerator gen_a(config);
    std::size_t sailors = gen_a.ScaledCount(qe::kBaseSailors);
    std::size_t boats = gen_a.ScaledCount(qe::kBaseBoats);
    std::size_t reserves = gen_a.ScaledCount(qe::kBaseReserves);

    std::vector<Tuple> s_a = gen_a.ApplyOrder(gen_a.GenerateSailors(sailors), 0);
    std::vector<Tuple> b_a = gen_a.ApplyOrder(gen_a.GenerateBoats(boats), 0);
    std::vector<Tuple> r_a =
        gen_a.ApplyOrder(gen_a.GenerateReserves(reserves, sailors, boats), 0);

    DataGenerator gen_b(config);
    std::vector<Tuple> s_b = gen_b.ApplyOrder(gen_b.GenerateSailors(sailors), 0);
    std::vector<Tuple> b_b = gen_b.ApplyOrder(gen_b.GenerateBoats(boats), 0);
    std::vector<Tuple> r_b =
        gen_b.ApplyOrder(gen_b.GenerateReserves(reserves, sailors, boats), 0);

    CHECK(s_a == s_b);
    CHECK(b_a == b_b);
    CHECK(r_a == r_b);
    CHECK(!s_a.empty());
    CHECK(!r_a.empty());
  }

  {
    constexpr std::size_t kSailors = 100;
    constexpr std::size_t kBoats = 20;
    constexpr std::size_t kReserves = 5000;

    auto max_over_mean = [&](const std::string& key_dist, double theta) {
      GenConfig config;
      config.seed = 7;
      config.key_dist = key_dist;
      config.zipf_theta = theta;
      DataGenerator gen(config);
      std::vector<Tuple> r = gen.GenerateReserves(kReserves, kSailors, kBoats);
      std::vector<std::size_t> freq(kSailors, 0);
      for (const Tuple& t : r) ++freq[t[0]];
      std::size_t max_freq = *std::max_element(freq.begin(), freq.end());
      double mean = static_cast<double>(kReserves) / kSailors;
      return max_freq / mean;
    };

    double uniform_ratio = max_over_mean("uniform", 0.0);
    double zipf_ratio = max_over_mean("zipf", 2.0);

    CHECK(uniform_ratio < 3.0);

    CHECK(zipf_ratio > 10.0);
    CHECK(zipf_ratio > uniform_ratio);
  }
TEST_MAIN_END
