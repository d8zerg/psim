#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/files.hpp"

namespace psim::monitoring {
namespace p = platform;
namespace asio = boost::asio;
namespace fs = std::filesystem;
using Json = nlohmann::json;

class OperationError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

inline void require(bool condition, std::string_view message) {
  if (!condition) {
    throw OperationError(std::string(message));
  }
}

template <class T>
T take(p::Result<T> result) {
  if (!result) {
    throw OperationError(std::string(result.error().name()));
  }
  if constexpr (!std::is_void_v<T>) {
    return std::move(*result);
  }
}

inline double seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string read_file(const fs::path& path, std::size_t maximum = std::size_t{16} * 1024 * 1024) {
  return take(p::files::read(path, maximum));
}

inline Json read_json(const fs::path& path) {
  return Json::parse(read_file(path));
}

inline void append_file(const fs::path& path, std::string_view body) {
  take(p::files::append(path, body));
}
}  // namespace psim::monitoring
