#pragma once

#include <cstdint>
#include <stdexcept>

namespace psim::sim {

/// SplitMix64 with rejection sampling: seed-to-decisions is independent of STL distributions.
/// Unsigned wrapping arithmetic is part of the specified algorithm; this is not a security RNG.
class Random {
 public:
  explicit Random(std::uint64_t seed) : state_(seed) {}

  [[nodiscard]] std::uint64_t next() noexcept {
    state_ += 0x9e3779b97f4a7c15ULL;
    auto value = state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
  }

  /// Uniform integer in [0, bound). Rejection avoids modulo bias.
  [[nodiscard]] std::uint64_t bounded(std::uint64_t bound) {
    if (bound == 0) {
      throw std::invalid_argument("random bound must be positive");
    }
    const auto threshold = (std::uint64_t{0} - bound) % bound;
    for (;;) {
      const auto value = next();
      if (value >= threshold) {
        return value % bound;
      }
    }
  }

 private:
  std::uint64_t state_;
};

}  // namespace psim::sim
