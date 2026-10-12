#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"

namespace psim::platform::security {
/// Move-only, wiped on destruction; no formatter, stream insertion or implicit string conversion.
class Secret {
 public:
  explicit Secret(std::string_view value);
  Secret(const Secret&) = delete;
  Secret& operator=(const Secret&) = delete;
  Secret(Secret&& other) noexcept;
  Secret& operator=(Secret&& other) noexcept;
  ~Secret();
  [[nodiscard]] std::string_view view() const noexcept;

 private:
  void wipe() noexcept;
  std::vector<char> bytes_;
};

/// Open once, reject symlinks/non-regular files, check ownership and exact mode 0400 on the fd.
/// Bounded read; errors never contain the path or content. Call on the control/blocking executor.
[[nodiscard]] Result<Secret> read_secret(std::string_view path, std::size_t max_bytes = 65536);
}  // namespace psim::platform::security
