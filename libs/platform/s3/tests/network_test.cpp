#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/client.hpp"
#include "support.hpp"

namespace psim::platform::s3::testing {
namespace {
namespace http = boost::beast::http;

void proxy(http::verb method, const std::string& suffix, const std::string& body = {}) {
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket socket(io);
  asio::connect(socket, resolver.resolve("toxiproxy", "8474"));
  http::request<http::string_body> request(method, "/proxies/s3" + suffix, 11);
  request.set(http::field::host, "toxiproxy");
  request.set(http::field::content_type, "application/json");
  request.body() = body;
  request.prepare_payload();
  http::write(socket, request);
  boost::beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  if (response.result_int() >= 400) {
    throw std::runtime_error("S3 network fixture failed");
  }
}

class Toxic {
 public:
  Toxic(const std::string& type, const std::string& attributes) {
    proxy(http::verb::post, "/toxics",
          R"({"name":"s3-test","type":")" + type + R"(","stream":"downstream","attributes":)" + attributes + "}");
  }

  ~Toxic() {
    try {
      proxy(http::verb::delete_, "/toxics/s3-test");
    } catch (...) {
      ADD_FAILURE() << "cannot reset S3 toxic";
    }
  }

  Toxic(const Toxic&) = delete;
  Toxic& operator=(const Toxic&) = delete;
  Toxic(Toxic&&) = delete;
  Toxic& operator=(Toxic&&) = delete;
};
}  // namespace

TEST(S3Network, UnknownPutResultReconcilesOnRetryWithoutOverwriting) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    const Files files;
    const auto source = files.write("input", 32768);
    auto config = options("filestore", true);
    config.timeout = Duration(1500);
    auto faulty = take(co_await Client::create(&blocking, config));
    auto direct = take(co_await Client::create(&blocking, options()));
    // Warm the TLS connection before cutting responses, so the PUT can reach the server.
    EXPECT_FALSE(co_await faulty->head(files.key()));
    {
      const Toxic toxic("timeout", "{\"timeout\":0}");
      const auto result = co_await faulty->put(files.key(), source);
      EXPECT_FALSE(result);
      const auto durable = take(co_await direct->head(files.key()));
      EXPECT_EQ(durable.size, 32768);
    }
    const auto recovered = take(co_await faulty->put(files.key(), source));
    EXPECT_EQ(recovered.size, 32768);
    EXPECT_FALSE(co_await faulty->put(files.key(), files.write("changed", 10)));
    check(co_await direct->remove(files.key()));
  };
  run(scenario());
}

TEST(S3Network, DeadlineAndCancellationNeverPublishPartialDownloads) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(3, 8);
    const Files files;
    auto direct = take(co_await Client::create(&blocking, options()));
    const auto object = take(co_await direct->put(files.key(), files.write("input", std::size_t{2} * 1024 * 1024)));
    auto config = options("filestore", true);
    config.timeout = Duration(700);
    auto faulty = take(co_await Client::create(&blocking, config));
    EXPECT_TRUE(co_await faulty->head(object.key));
    {
      const Toxic toxic("bandwidth", "{\"rate\":64}");
      EXPECT_FALSE(co_await faulty->get(object, files.path("timeout")));
      EXPECT_FALSE(std::filesystem::exists(files.path("timeout")));
      EXPECT_TRUE(files.clean());
    }
    config.timeout = Duration(10000);
    auto cancellable = take(co_await Client::create(&blocking, config));
    EXPECT_TRUE(co_await cancellable->head(object.key));
    {
      const Toxic toxic("bandwidth", "{\"rate\":64}");
      async::SystemClock clock;
      auto result =
          co_await async::with_deadline(&clock, Duration(200), cancellable->get(object, files.path("cancelled")));
      EXPECT_FALSE(result);
      EXPECT_FALSE(std::filesystem::exists(files.path("cancelled")));
      EXPECT_TRUE(files.clean());
    }
    EXPECT_TRUE(co_await cancellable->get(object, files.path("recovered")));
    check(co_await direct->remove(object.key));
  };
  run(scenario());
}

TEST(S3Network, BrokenConnectionReturnsAnErrorAndRecoversWithoutSecrets) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    auto config = options("filestore", true);
    config.timeout = Duration(1000);
    auto client = take(co_await Client::create(&blocking, config));
    proxy(http::verb::post, "", "{\"enabled\":false}");
    const auto result = co_await client->head("network-missing");
    EXPECT_FALSE(result);
    proxy(http::verb::post, "", "{\"enabled\":true}");
    const auto recovered = co_await client->head("network-missing");
    EXPECT_FALSE(recovered);
    EXPECT_EQ(recovered.error().code(), ErrorCode::kCommonNotFound);
    EXPECT_EQ(result.error().message().find("psim-dev"), std::string::npos);
  };
  run(scenario());
}
}  // namespace psim::platform::s3::testing
