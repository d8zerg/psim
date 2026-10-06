// Executables link libc++, libc++abi and the libgcc unwinder statically (ADR-032, ADR-034); this
// checks that exceptions still unwind through several frames and destructors run on the way.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace {

struct Guard {
  int* destroyed;

  explicit Guard(int* counter) : destroyed(counter) {}

  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;
  Guard(Guard&&) = delete;
  Guard& operator=(Guard&&) = delete;

  ~Guard() { ++*destroyed; }
};

[[noreturn]] void fail_deep(int depth, int* destroyed) {
  const Guard guard(destroyed);
  if (depth == 0) {
    throw std::runtime_error("unwound");
  }
  fail_deep(depth - 1, destroyed);
}

TEST(StaticRuntime, ExceptionsUnwindThroughFrames) {
  int destroyed = 0;
  std::string message;
  try {
    fail_deep(3, &destroyed);
  } catch (const std::runtime_error& e) {
    message = e.what();
  }
  EXPECT_EQ(message, "unwound");
  EXPECT_EQ(destroyed, 4);
}

}  // namespace
