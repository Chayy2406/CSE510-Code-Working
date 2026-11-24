#pragma once

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace testutil {

inline int g_checks = 0;
inline int g_failures = 0;

inline void RecordFailure(const std::string& expr, const char* file, int line,
                           const std::string& detail) {
  ++g_failures;
  std::cerr << file << ":" << line << ": CHECK failed: " << expr;
  if (!detail.empty()) std::cerr << " (" << detail << ")";
  std::cerr << "\n";
}

inline int Finish() {
  if (g_failures > 0) {
    std::cerr << g_failures << " of " << g_checks << " checks failed\n";
    return 1;
  }
  std::cerr << "all " << g_checks << " checks passed\n";
  return 0;
}

}

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++testutil::g_checks;                                               \
    if (!(cond)) {                                                      \
      testutil::RecordFailure(#cond, __FILE__, __LINE__, "");           \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                  \
  do {                                                                  \
    ++testutil::g_checks;                                               \
    auto va = (a);                                                      \
    auto vb = (b);                                                      \
    if (!(va == vb)) {                                                  \
      std::ostringstream os;                                            \
      os << "lhs=" << va << " rhs=" << vb;                              \
      testutil::RecordFailure(#a " == " #b, __FILE__, __LINE__, os.str()); \
    }                                                                    \
  } while (0)

#define TEST_MAIN_BEGIN int main() {
#define TEST_MAIN_END return testutil::Finish(); }
