#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"

namespace psim::platform::redis {
namespace asio = boost::asio;
using Duration = async::Duration;

struct Endpoint {
  std::string host = "127.0.0.1";
  std::string port = "6379";
  friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

struct Credentials {
  std::string username = "default";
  std::string password{};
};

struct TlsOptions {
  bool enabled = true;
  std::string ca_file{};
  std::string certificate_file{};
  std::string private_key_file{};
  /// Empty means the endpoint hostname, including a primary discovered through Sentinel.
  std::string server_name{};
  std::string crl_file{};
};

struct SentinelOptions {
  std::vector<Endpoint> endpoints{};
  std::string primary_name = "psim";
  Credentials credentials{};
  TlsOptions tls{};
  Duration probe_timeout = std::chrono::seconds{1};
};

struct ConnectionOptions {
  Endpoint endpoint{};
  Credentials credentials{};
  TlsOptions tls{};
  /// Nonempty endpoints select Sentinel discovery instead of the static endpoint.
  SentinelOptions sentinel{};
  Duration connect_timeout = std::chrono::seconds{5};
  Duration command_timeout = std::chrono::seconds{5};
  std::size_t max_response_bytes = std::size_t{4} * 1024 * 1024;
};

/// A bounded RESP3 value. Error text contains only a recognized protocol error category.
struct Reply {
  enum class Kind : std::uint8_t { kNull, kString, kInteger, kBoolean, kArray, kMap, kError };
  Kind kind = Kind::kNull;
  std::string data{};
  std::vector<Reply> elements{};
  [[nodiscard]] Result<std::int64_t> integer() const;
};

using Command = std::vector<std::string>;

/// One shard-owned connection. No automatic replay or reconnection of submitted commands.
/// A failed, cancelled or timed-out operation closes the transport; reconnect via the pool.
class Connection {
 public:
  struct State;
  static asio::awaitable<Result<std::unique_ptr<Connection>>> connect(ConnectionOptions options);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;
  ~Connection();
  asio::awaitable<Result<Reply>> execute(Command command, Duration timeout = Duration::zero());
  /// Server errors remain individual kError replies; other commands may have succeeded.
  asio::awaitable<Result<std::vector<Reply>>> pipeline(std::vector<Command> commands,
                                                       Duration timeout = Duration::zero());
  asio::awaitable<Result<void>> verify_primary(Duration timeout = Duration::zero());
  void close();
  [[nodiscard]] bool reusable() const noexcept;
  [[nodiscard]] const Endpoint& endpoint() const noexcept;

 private:
  explicit Connection(std::shared_ptr<State> state);
  static asio::awaitable<Result<std::unique_ptr<Connection>>> open(Endpoint endpoint, Credentials credentials,
                                                                   TlsOptions tls, ConnectionOptions options,
                                                                   Duration timeout);
  std::shared_ptr<State> state_;
};
}  // namespace psim::platform::redis
