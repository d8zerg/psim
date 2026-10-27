#include "psim/platform/redis/connection.hpp"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/request.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::redis {
namespace native = boost::redis;

namespace {
constexpr std::size_t kMaxRequestBytes = std::size_t{4} * 1024 * 1024;
constexpr std::size_t kMaxNodes = 8192;
constexpr std::size_t kMaxDepth = 32;

std::string error_category(std::string_view text) {
  constexpr std::array kCategories = {"READONLY",  "NOSCRIPT",  "WRONGTYPE", "NOAUTH",     "NOPERM",
                                      "WRONGPASS", "LOADING",   "BUSY",      "MASTERDOWN", "MOVED",
                                      "ASK",       "CROSSSLOT", "ERR"};
  const auto prefix = text.substr(0, text.find(' '));
  for (const auto* category : kCategories) {
    if (prefix == category) {
      return category;
    }
  }
  return "ERR";
}

Result<Reply> parse_reply(std::span<const native::resp3::node> nodes, std::size_t* index) {
  if (*index >= nodes.size()) {
    return fail(ErrorCode::kCommonInternal, "invalid Redis response");
  }
  const auto& node = nodes[(*index)++];
  Reply reply;
  using Type = native::resp3::type;
  switch (node.data_type) {
    case Type::null:
      return reply;
    case Type::simple_error:
    case Type::blob_error:
      reply.kind = Reply::Kind::kError;
      reply.data = error_category(node.value);
      return reply;
    case Type::number:
      reply.kind = Reply::Kind::kInteger;
      break;
    case Type::boolean:
      reply.kind = Reply::Kind::kBoolean;
      break;
    case Type::array:
    case Type::set:
    case Type::map:
    case Type::attribute: {
      reply.kind =
          node.data_type == Type::map || node.data_type == Type::attribute ? Reply::Kind::kMap : Reply::Kind::kArray;
      const auto count = node.aggregate_size * native::resp3::element_multiplicity(node.data_type);
      if (count > nodes.size() - *index) {
        return fail(ErrorCode::kCommonInternal, "invalid Redis aggregate");
      }
      reply.elements.reserve(count);
      for (std::size_t i = 0; i < count; ++i) {
        auto child = parse_reply(nodes, index);
        if (!child) {
          return std::unexpected(child.error());
        }
        reply.elements.push_back(std::move(*child));
      }
      return reply;
    }
    case Type::simple_string:
    case Type::blob_string:
    case Type::verbatim_string:
    case Type::doublean:
    case Type::big_number:
      reply.kind = Reply::Kind::kString;
      break;
    default:
      return fail(ErrorCode::kCommonInternal, "unsupported Redis response type");
  }
  reply.data = node.value;
  return reply;
}

bool valid_endpoint(const Endpoint& endpoint) {
  unsigned int port = 0;
  const std::string_view text(endpoint.port);
  const auto parsed = std::from_chars(text.begin(), text.end(), port);
  return !endpoint.host.empty() && endpoint.host.size() <= 253 && endpoint.host.find('\0') == std::string::npos
         && parsed.ec == std::errc{} && parsed.ptr == text.end() && port > 0 && port <= 65535;
}

bool valid_command(const Command& command) {
  if (command.empty() || command.size() > 4096 || command[0].empty()) {
    return false;
  }
  for (const char letter : command[0]) {
    if (letter < 'A' || letter > 'Z') {
      return false;
    }
  }
  // Session state and blocking commands cannot be returned to a shared pool safely.
  constexpr std::array kProhibited = {
      "AUTH",        "HELLO",        "SELECT",       "QUIT",     "RESET",     "CLIENT",     "MULTI",
      "EXEC",        "DISCARD",      "WATCH",        "UNWATCH",  "SUBSCRIBE", "PSUBSCRIBE", "SSUBSCRIBE",
      "UNSUBSCRIBE", "PUNSUBSCRIBE", "SUNSUBSCRIBE", "MONITOR",  "BLPOP",     "BRPOP",      "BRPOPLPUSH",
      "BLMOVE",      "BLMPOP",       "BZPOPMIN",     "BZPOPMAX", "BZMPOP",    "XREAD",      "XREADGROUP"};
  return std::ranges::find(kProhibited, command[0]) == kProhibited.end();
}
}  // namespace

struct Connection::State {
  State(asio::any_io_executor executor, asio::ssl::context context, ConnectionOptions config, Endpoint address)
      : connection(std::move(executor), std::move(context), native::logger{native::logger::level::disabled}),
        options(std::move(config)),
        endpoint(std::move(address)) {}

  native::connection connection;
  ConnectionOptions options;
  Endpoint endpoint;
  native::config config;
  bool finished = false;
  bool closed = false;
  bool busy = false;
};

namespace {
asio::awaitable<Result<std::vector<Reply>>> exchange(std::shared_ptr<Connection::State> state,
                                                     native::request request) {
  std::vector<native::resp3::node> nodes;
  std::size_t bytes = 0;
  native::any_adapter adapter(
      native::any_adapter::impl_t{[&nodes, &bytes, limit = state->options.max_response_bytes](
                                      native::any_adapter::parse_event event, const native::resp3::node_view& node,
                                      boost::system::error_code& error) {
        if (event != native::any_adapter::parse_event::node) {
          return;
        }
        if (node.value.size() > limit - bytes || nodes.size() >= kMaxNodes || node.depth > kMaxDepth
            || node.aggregate_size > kMaxNodes) {
          error = native::error::exceeds_maximum_read_buffer_size;
          return;
        }
        bytes += node.value.size();
        nodes.push_back({node.data_type, node.aggregate_size, node.depth, std::string(node.value)});
      }});
  boost::system::error_code error;
  co_await state->connection.async_exec(request, std::move(adapter), asio::redirect_error(asio::use_awaitable, error));
  if (error) {
    co_return fail(ErrorCode::kCommonUnavailable,
                   "Redis transport or protocol failure; command outcome may be unknown");
  }
  std::vector<Reply> replies;
  std::size_t index = 0;
  while (index < nodes.size()) {
    auto reply = parse_reply(nodes, &index);
    if (!reply) {
      co_return std::unexpected(reply.error());
    }
    replies.push_back(std::move(*reply));
  }
  if (replies.size() != request.get_expected_responses()) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis response count");
  }
  co_return replies;
}
}  // namespace

Result<std::int64_t> Reply::integer() const {
  std::int64_t value = 0;
  const std::string_view text(data);
  const auto parsed = std::from_chars(text.begin(), text.end(), value);
  if (kind != Kind::kInteger || parsed.ec != std::errc{} || parsed.ptr != text.end()) {
    return fail(ErrorCode::kCommonInternal, "Redis response is not an integer");
  }
  return value;
}

Connection::Connection(std::shared_ptr<State> state) : state_(std::move(state)) {}

Connection::~Connection() {
  close();
}

void Connection::close() {
  if (!state_->closed) {
    state_->closed = true;
    state_->connection.cancel();
  }
}

bool Connection::reusable() const noexcept {
  return !state_->closed && !state_->finished && !state_->busy;
}

const Endpoint& Connection::endpoint() const noexcept {
  return state_->endpoint;
}

asio::awaitable<Result<std::unique_ptr<Connection>>> Connection::open(Endpoint endpoint, Credentials credentials,
                                                                      TlsOptions tls, ConnectionOptions options,
                                                                      Duration timeout) {
  if (timeout <= Duration::zero()) {
    co_return fail(ErrorCode::kCommonDeadlineExceeded, "Redis connection deadline exceeded");
  }
  auto executor = co_await asio::this_coro::executor;
  std::shared_ptr<State> state;
  try {
    asio::ssl::context context{asio::ssl::context::tls_client};
    if (tls.enabled) {
      auto configured = security::configure_tls(context.native_handle(), {.ca_file = tls.ca_file,
                                                                          .certificate_file = tls.certificate_file,
                                                                          .private_key_file = tls.private_key_file,
                                                                          .crl_file = tls.crl_file});
      if (!configured) {
        co_return std::unexpected(configured.error());
      }
      context.set_verify_callback(
          asio::ssl::host_name_verification(tls.server_name.empty() ? endpoint.host : tls.server_name));
    }
    state = std::make_shared<State>(executor, std::move(context), std::move(options), std::move(endpoint));
  } catch (const std::exception&) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis TLS configuration");
  }
  state->config.addr = {state->endpoint.host, state->endpoint.port};
  state->config.username = std::move(credentials.username);
  state->config.password = std::move(credentials.password);
  state->config.clientname = "psim-platform";
  state->config.use_ssl = tls.enabled;
  state->config.resolve_timeout = timeout;
  state->config.connect_timeout = timeout;
  state->config.ssl_handshake_timeout = timeout;
  state->config.reconnect_wait_interval = Duration::zero();
  state->config.health_check_interval = Duration::zero();
  state->config.max_read_size = state->options.max_response_bytes;
  // The driver owns its state until cancellation has drained all network operations.
  state->connection.async_run(state->config, [state](boost::system::error_code) { state->finished = true; });
  auto connection = std::unique_ptr<Connection>(
      new Connection(state));  // NOLINT(cppcoreguidelines-owning-memory): private constructor
  auto ping = co_await connection->execute({"PING"}, timeout);
  if (!ping) {
    co_return std::unexpected(ping.error());
  }
  co_return connection;
}

asio::awaitable<Result<std::unique_ptr<Connection>>> Connection::connect(ConnectionOptions options) {
  co_await asio::this_coro::throw_if_cancelled(false);
  if (options.connect_timeout <= Duration::zero() || options.command_timeout <= Duration::zero()
      || options.max_response_bytes < 1024 || options.max_response_bytes > std::size_t{16} * 1024 * 1024
      || !valid_endpoint(options.endpoint) || options.sentinel.endpoints.size() > 16
      || options.sentinel.probe_timeout <= Duration::zero() || options.sentinel.primary_name.empty()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis connection options");
  }
  const auto deadline = std::chrono::steady_clock::now() + options.connect_timeout;
  auto remaining = [&deadline]() {
    return deadline - std::chrono::steady_clock::now();
  };
  if (options.sentinel.endpoints.empty()) {
    auto connection = co_await open(options.endpoint, options.credentials, options.tls, options, remaining());
    if (!connection) {
      co_return std::unexpected(connection.error());
    }
    if (options.require_primary) {
      auto role = co_await (*connection)->verify_primary(remaining());
      if (!role) {
        co_return std::unexpected(role.error());
      }
    }
    co_return std::move(*connection);
  }
  for (const auto& sentinel : options.sentinel.endpoints) {
    if (!valid_endpoint(sentinel)) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Sentinel endpoint");
    }
    if (remaining() <= Duration::zero()) {
      break;
    }
    const auto probe_deadline =
        std::chrono::steady_clock::now() + std::min(remaining(), options.sentinel.probe_timeout);
    auto discovery = co_await open(sentinel, options.sentinel.credentials, options.sentinel.tls, options,
                                   probe_deadline - std::chrono::steady_clock::now());
    if (!discovery) {
      continue;
    }
    auto address = co_await (*discovery)
                       ->execute({"SENTINEL", "get-master-addr-by-name", options.sentinel.primary_name},
                                 probe_deadline - std::chrono::steady_clock::now());
    (*discovery)->close();
    if (!address || address->kind != Reply::Kind::kArray || address->elements.size() != 2) {
      continue;
    }
    Endpoint primary{address->elements[0].data, address->elements[1].data};
    if (!valid_endpoint(primary) || remaining() <= Duration::zero()) {
      continue;
    }
    auto connection = co_await open(std::move(primary), options.credentials, options.tls, options,
                                    std::min(remaining(), options.sentinel.probe_timeout));
    if (!connection) {
      continue;
    }
    auto role = co_await (*connection)->verify_primary(std::min(remaining(), options.sentinel.probe_timeout));
    if (role) {
      co_return std::move(*connection);
    }
  }
  co_return fail(ErrorCode::kCommonUnavailable, "Sentinel could not discover a writable Redis primary");
}

asio::awaitable<Result<std::vector<Reply>>> Connection::pipeline(std::vector<Command> commands, Duration timeout) {
  co_await asio::this_coro::throw_if_cancelled(false);
  auto state = state_;
  if (state->closed || state->finished || state->busy) {
    co_return fail(ErrorCode::kCommonUnavailable, "Redis connection unavailable or already in use");
  }
  if (commands.empty() || commands.size() > 1024) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis batch size");
  }
  native::request request;
  std::size_t bytes = 0;
  for (const auto& command : commands) {
    if (!valid_command(command)) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "invalid or stateful Redis command");
    }
    for (const auto& argument : command) {
      if (argument.size() > kMaxRequestBytes - bytes) {
        co_return fail(ErrorCode::kCommonInvalidArgument, "Redis request too large");
      }
      bytes += argument.size();
    }
    if (command.size() == 1) {
      request.push(command[0]);
    } else {
      request.push_range(command[0], command[1], command.begin() + 2, command.end());
      // push_range skips an empty range, so a one-argument command is handled separately.
      if (command.size() == 2) {
        request.push(command[0], command[1]);
      }
    }
    if (request.payload().size() > kMaxRequestBytes) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "Redis encoded request too large");
    }
  }
  if (timeout <= Duration::zero()) {
    timeout = state->options.command_timeout;
  }
  state->busy = true;
  async::SystemClock clock;
  auto response = co_await async::with_deadline(&clock, timeout, exchange(state, std::move(request)));
  state->busy = false;
  if (!response || !*response) {
    close();
    if (!response) {
      co_return std::unexpected(response.error());
    }
    co_return std::unexpected(response->error());
  }
  for (const auto& reply : **response) {
    if (reply.kind == Reply::Kind::kError && reply.data == "READONLY") {
      close();
    }
  }
  co_return std::move(**response);
}

asio::awaitable<Result<Reply>> Connection::execute(Command command, Duration timeout) {
  std::vector<Command> commands;
  commands.push_back(std::move(command));
  auto replies = co_await pipeline(std::move(commands), timeout);
  if (!replies) {
    co_return std::unexpected(replies.error());
  }
  auto reply = std::move(replies->front());
  if (reply.kind == Reply::Kind::kError) {
    const auto code = reply.data == "READONLY" || reply.data == "LOADING" || reply.data == "MASTERDOWN"
                          ? ErrorCode::kCommonUnavailable
                          : ErrorCode::kCommonInvalidArgument;
    co_return fail(code, "Redis server rejected command: " + reply.data);
  }
  co_return reply;
}

asio::awaitable<Result<void>> Connection::verify_primary(Duration timeout) {
  if (timeout < Duration::zero()) {
    co_return fail(ErrorCode::kCommonDeadlineExceeded, "Redis primary verification deadline exceeded");
  }
  auto reply = co_await execute({"ROLE"}, timeout);
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind != Reply::Kind::kArray || reply->elements.empty() || reply->elements[0].data != "master") {
    close();
    co_return fail(ErrorCode::kCommonUnavailable, "Redis endpoint is not a primary");
  }
  co_return Result<void>{};
}
}  // namespace psim::platform::redis
