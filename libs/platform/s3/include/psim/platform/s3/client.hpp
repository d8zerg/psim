#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::s3 {
namespace asio = boost::asio;
using Duration = std::chrono::milliseconds;
class Metrics;

/// Process lifetime. Construct before starting shards; destroy after every client and pending
/// operation has finished. Initialization/shutdown of the SDK runs on the control thread.
class Sdk {
 public:
  Sdk();
  ~Sdk();
  Sdk(const Sdk&) = delete;
  Sdk& operator=(const Sdk&) = delete;
  Sdk(Sdk&&) = delete;
  Sdk& operator=(Sdk&&) = delete;
};

struct Options {
  std::string endpoint;
  std::string region = "us-east-1";
  std::string bucket;
  std::string access_key_file;
  std::string secret_key_file;
  security::TlsOptions tls{};
  bool allow_plaintext = false;
  std::size_t concurrency = 4;
  std::size_t max_object_bytes = std::size_t{20} * 1024 * 1024;
  Duration timeout{10000};
  Duration acquire_timeout{5000};
  Duration connect_timeout{2000};
  unsigned int read_attempts = 3;
};

struct Object {
  std::string key;
  std::uint64_t size = 0;
  std::string sha256;
  /// Opaque conditional-read token, never interpreted as a checksum or a catalog version.
  std::string etag{};
};

struct ListPage {
  std::vector<std::string> keys;
  std::string continuation;
};

enum class WriteMode : std::uint8_t { kReconcile, kWriteOnly };

/// Immutable keys with conditional creation. Native SDK/file operations run in BlockingPool.
/// Admission is bounded on one executor/shard. No redirects, ambient credentials or SDK retries.
class Client {
 public:
  static asio::awaitable<Result<std::unique_ptr<Client>>> create(async::BlockingPool* blocking, Options options,
                                                                 Metrics* metrics = nullptr);
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  ~Client();

  /// Snapshot a regular input file with bounded RAM and disk, hash and conditionally PUT it.
  /// An existing key is accepted only for the same size/SHA-256. An unknown write outcome is
  /// reconciled with HEAD; only a confirmed absence permits another conditional PUT.
  asio::awaitable<Result<Object>> put(std::string key, std::string source_file, WriteMode mode = WriteMode::kReconcile);
  /// Bounded bucket enumeration. Only explicit read identities need ListBucket permission.
  asio::awaitable<Result<ListPage>> list(std::string continuation = {}, std::size_t max_keys = 1000);
  asio::awaitable<Result<Object>> head(std::string key);
  /// Verify the independently supplied digest/length, GET with If-Match, hash streamed bytes,
  /// then atomically create destination_file. Existing destinations are never overwritten.
  /// A failed/cancelled transfer removes its temporary file and returns no successful object.
  asio::awaitable<Result<Object>> get(Object expected, std::string destination_file);
  /// One DELETE attempt. Transport errors may have an unknown result; no implicit retry.
  asio::awaitable<Result<void>> remove(std::string key);
  void close();

 private:
  struct State;
  explicit Client(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};
}  // namespace psim::platform::s3
