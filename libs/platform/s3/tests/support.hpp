#pragma once
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/s3/client.hpp"

namespace psim::platform::s3::testing {
inline Sdk& sdk() {
  static Sdk instance;
  return instance;
}

template <typename T>
T run(asio::awaitable<T> operation) {
  (void)sdk();
  asio::io_context io;
  auto result = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return result.get();
}

template <typename T>
T take(Result<T> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

inline void check(Result<void> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
}

inline std::string endpoint(bool fault = false) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): test endpoints are configured before threads
  const char* value = std::getenv(
      fault ? "PSIM_TEST_S3_FAULT"
            : "PSIM_TEST_S3");  // NOLINT(concurrency-mt-unsafe): test endpoints are configured before threads
  if (value == nullptr) {
    throw std::runtime_error("missing S3 component endpoint");
  }
  return value;
}

inline Options options(const std::string& role = "filestore", bool fault = false) {
  return {.endpoint = endpoint(fault),
          .bucket = "psim-plans",
          .access_key_file = "/repo/.cache/s3-tls/" + role + ".access",
          .secret_key_file = "/repo/.cache/s3-tls/" + role + ".secret",
          .tls = {.ca_file = "/repo/.cache/s3-tls/ca.pem"}};
}

class Files {
 public:
  Files() {
    std::string pattern = "/tmp/psim-s3-test-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    // NOLINTNEXTLINE(concurrency-mt-unsafe): atomic unique test directory, called before worker threads
    const char* created = ::mkdtemp(name.data());
    if (created == nullptr) {
      throw std::runtime_error("cannot create S3 test directory");
    }
    root_ = created;
  }

  ~Files() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  Files(const Files&) = delete;
  Files& operator=(const Files&) = delete;
  Files(Files&&) = delete;
  Files& operator=(Files&&) = delete;

  [[nodiscard]] std::string path(const std::string& name) const { return root_ + "/" + name; }

  [[nodiscard]] std::string write(const std::string& name, std::size_t size) const {
    const auto file = path(name);
    std::ofstream stream(file, std::ios::binary);
    const std::string chunk(65536, '\x9f');
    while (size > 0) {
      const auto count = std::min(size, chunk.size());
      stream.write(chunk.data(), static_cast<std::streamsize>(count));
      size -= count;
    }
    if (!stream) {
      throw std::runtime_error("cannot write test source");
    }
    return file;
  }

  [[nodiscard]] std::string key() const { return "tenant-a/" + std::filesystem::path(root_).filename().string(); }

  [[nodiscard]] bool clean() const {
    return std::ranges::all_of(std::filesystem::directory_iterator(root_), [](const auto& entry) {
      return entry.path().filename().string().find(".psim-") == std::string::npos;
    });
  }

 private:
  std::string root_;
};
}  // namespace psim::platform::s3::testing
