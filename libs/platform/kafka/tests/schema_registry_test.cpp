#include "psim/platform/kafka/schema_registry.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <map>
#include <string>
#include <utility>

#include "psim/platform/error_codes.hpp"

namespace psim::platform::kafka {
namespace {

namespace http = boost::beast::http;
using asio::ip::tcp;

constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);

/// Answers GET requests from a fixed table; anything else is 404.
class FakeRegistry {
 public:
  explicit FakeRegistry(asio::io_context& io) : acceptor_(io, {asio::ip::make_address("127.0.0.1"), 0}) {
    asio::co_spawn(io, serve(), asio::detached);
  }

  void answer(const std::string& target, http::status status, std::string body) {
    routes_[target] = {status, std::move(body)};
  }

  [[nodiscard]] std::string url() const {
    return "http://127.0.0.1:" + std::to_string(acceptor_.local_endpoint().port()) + "/apis/ccompat/v7/";
  }

  [[nodiscard]] int requests() const noexcept { return requests_; }

  void close() { acceptor_.close(); }

 private:
  asio::awaitable<void> serve() {
    while (acceptor_.is_open()) {
      auto [ec, socket] = co_await acceptor_.async_accept(kAwait);
      if (ec) {
        co_return;
      }
      boost::beast::flat_buffer buffer;
      http::request<http::string_body> request;
      if (auto [read_ec, bytes] = co_await http::async_read(socket, buffer, request, kAwait); read_ec) {
        continue;
      }
      ++requests_;
      http::response<http::string_body> response;
      response.version(request.version());
      response.result(http::status::not_found);
      if (const auto it = routes_.find(std::string(request.target())); it != routes_.end()) {
        response.result(it->second.first);
        response.body() = it->second.second;
      }
      response.prepare_payload();
      std::ignore = co_await http::async_write(socket, response, kAwait);
    }
  }

  tcp::acceptor acceptor_;
  std::map<std::string, std::pair<http::status, std::string>> routes_;
  int requests_ = 0;
};

template <typename T>
T run(asio::io_context& io, asio::awaitable<T> operation) {
  auto result = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.restart();
  while (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    io.run_one();
  }
  return result.get();
}

TEST(SchemaRegistry, ResolvesTheLatestIdAndCachesItsSchema) {
  asio::io_context io;
  FakeRegistry fake(io);
  fake.answer("/apis/ccompat/v7/subjects/psim.events.v1-value/versions/latest", http::status::ok,
              R"({"subject":"psim.events.v1-value","version":3,"id":17,"schema":"syntax = \"proto3\";"})");
  fake.answer("/apis/ccompat/v7/schemas/ids/5", http::status::ok, R"({"schema":"message Old {}"})");
  auto registry = SchemaRegistry::create({.url = fake.url(), .timeout = std::chrono::milliseconds(2000)});
  ASSERT_TRUE(registry.has_value());

  const auto id = run(io, registry->latest_id("psim.events.v1-value"));
  ASSERT_TRUE(id.has_value()) << id.error().message();
  EXPECT_EQ(*id, 17);
  EXPECT_EQ(run(io, registry->schema(17)).value(), "syntax = \"proto3\";");
  EXPECT_EQ(fake.requests(), 1);  // the schema came with the version

  EXPECT_EQ(run(io, registry->schema(5)).value(), "message Old {}");
  EXPECT_EQ(run(io, registry->schema(5)).value(), "message Old {}");
  EXPECT_EQ(fake.requests(), 2);
}

TEST(SchemaRegistry, EscapesSubjectsWithSlashes) {
  asio::io_context io;
  FakeRegistry fake(io);
  fake.answer("/apis/ccompat/v7/subjects/psim%2Fcommon%2Fv1%2Fenvelope.proto/versions/latest", http::status::ok,
              R"({"id":3})");
  auto registry = SchemaRegistry::create({.url = fake.url(), .timeout = std::chrono::milliseconds(2000)});
  EXPECT_EQ(run(io, registry->latest_id("psim/common/v1/envelope.proto")).value(), 3);
}

TEST(SchemaRegistry, MapsFailuresToCatalogCodes) {
  asio::io_context io;
  FakeRegistry fake(io);
  fake.answer("/apis/ccompat/v7/subjects/broken-value/versions/latest", http::status::ok, "not json");
  fake.answer("/apis/ccompat/v7/subjects/down-value/versions/latest", http::status::service_unavailable, "");
  fake.answer("/apis/ccompat/v7/schemas/ids/9", http::status::ok, R"({"id":9})");
  auto registry = SchemaRegistry::create({.url = fake.url(), .timeout = std::chrono::milliseconds(2000)});

  const auto missing = run(io, registry->latest_id("missing-value"));
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error().code(), ErrorCode::kCommonNotFound);
  EXPECT_EQ(run(io, registry->latest_id("broken-value")).error().code(), ErrorCode::kCommonUnavailable);
  EXPECT_EQ(run(io, registry->latest_id("down-value")).error().code(), ErrorCode::kCommonUnavailable);
  EXPECT_EQ(run(io, registry->schema(9)).error().code(), ErrorCode::kCommonUnavailable);
  EXPECT_EQ(run(io, registry->schema(10)).error().code(), ErrorCode::kCommonNotFound);

  const std::string url = fake.url();
  fake.close();
  auto closed = SchemaRegistry::create({.url = url, .timeout = std::chrono::milliseconds(500)});
  EXPECT_EQ(run(io, closed->latest_id("any-value")).error().code(), ErrorCode::kCommonUnavailable);
  auto unresolvable =
      SchemaRegistry::create({.url = "http://no-such-host.invalid:8080", .timeout = std::chrono::milliseconds(500)});
  EXPECT_EQ(run(io, unresolvable->latest_id("any-value")).error().code(), ErrorCode::kCommonUnavailable);
}

TEST(SchemaRegistry, ValidatesTheUrl) {
  EXPECT_FALSE(SchemaRegistry::create({.url = "https://registry:8080", .timeout = {}}).has_value());
  EXPECT_FALSE(SchemaRegistry::create({.url = "http://:8080/x", .timeout = {}}).has_value());
  EXPECT_TRUE(SchemaRegistry::create({.url = "http://registry", .timeout = {}}).has_value());
}

}  // namespace
}  // namespace psim::platform::kafka
