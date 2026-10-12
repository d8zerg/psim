#include <boost/asio/as_tuple.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/redis/connection.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/security/trust.hpp"
#include "support.hpp"

namespace psim::platform::security {
namespace {
using namespace testing;  // NOLINT(google-build-using-namespace): local test helpers

std::string keycloak() {
  const auto* value = std::getenv("PSIM_TEST_KEYCLOAK");  // NOLINT(concurrency-mt-unsafe): read before test threads
  require(value != nullptr);
  return value;
}

TEST(SecurityComponent, KeycloakAccessTokenUsesVerifiedHttpsJwksAndRejectsWrongAudience) {
  Fixture fixture;
  auto operation = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                     // closure outlives the structured join or run
    const auto base = keycloak() + "/realms/psim/protocol/openid-connect";
    const auto token_response = take(co_await http_request(
        base + "/token", {.allow_plaintext = true},
        "grant_type=client_credentials&client_id=psim-services&client_secret=psim-dev-services-secret",
        "application/x-www-form-urlencoded"));
    EXPECT_EQ(token_response.status, 200);
    const auto token = Json::parse(token_response.body).at("access_token").get<std::string>();
    const auto keys_response = take(co_await http_request(base + "/certs", {.allow_plaintext = true}));
    EXPECT_EQ(keys_response.status, 200);
    auto verify =
        [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                         // closure outlives the structured join or run
      auto options = JwksOptions{.jwt = {.issuer = keycloak() + "/realms/psim", .audience = "psim"},
                                 .url = url + "/certs",
                                 .http = {.tls = fixture.options(fixture.client, "https-client")}};
      auto cache = take(JwksCache::create(options));
      auto principal = take(co_await cache->verify(token));
      EXPECT_EQ(principal.authorized_party, "psim-services");
      EXPECT_FALSE(principal.subject.empty());
      const auto service = peer(fixture, issue(9, "psim://service/psim-services", &fixture.ca), "service-token-client");
      check(authenticate_service(service, principal, {"psim-services"}, false, std::chrono::system_clock::now()));
      EXPECT_TRUE(co_await cache->verify(token));
      auto wrong_audience =
          take(JwtVerifier::create({.issuer = options.jwt.issuer, .audience = "other-service"}, keys_response.body));
      EXPECT_FALSE(wrong_audience.verify(token, std::chrono::system_clock::now()));
    };
    // The development Keycloak backend is isolated HTTP; the library fetches its actual keys
    // through a verified HTTPS fixture, exercising the production transport and cache together.
    co_await with_https(&fixture, keys_response.body, 200, verify);
    const auto invalid_secret = take(
        co_await http_request(base + "/token", {.allow_plaintext = true},
                              "grant_type=client_credentials&client_id=psim-services&client_secret=wrong-secret-canary",
                              "application/x-www-form-urlencoded"));
    EXPECT_EQ(invalid_secret.status, 401);
  };
  run(operation());
}

TEST(SecurityComponent, SchemaRegistryUsesVerifiedMtlsAndCachesSchemas) {
  Fixture fixture;
  auto operation =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    auto registry = take(kafka::SchemaRegistry::create(
        {.url = url + "/apis/ccompat/v7", .http = {.tls = fixture.options(fixture.client, "registry-client")}}));
    EXPECT_EQ(take(co_await registry.latest_id("psim/events.proto")), 17);
    EXPECT_EQ(take(co_await registry.schema(17)), "message Event {}");
  };
  run(with_https(&fixture, R"({"id":17,"schema":"message Event {}"})", 200, operation));
}

TEST(SecurityComponent, RenewalPreservesEventsAndRevocationClosesAnActiveMtlsStreamImmediately) {
  Fixture fixture;
  const auto replacement_certificate = issue(4, "psim://connector/connector1", &fixture.ca);
  const auto original = peer(fixture, fixture.client, "initial-client");
  const auto replacement = peer(fixture, replacement_certificate, "replacement-client");
  ConnectorTrust trust;
  check(trust.enroll("connector1", "tenant1", original, std::chrono::system_clock::now()));
  bool old_closed = false;
  bool old_client_rejected = false;
  std::vector<unsigned int> events;
  auto operation = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                     // closure outlives the structured join or run
    constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);
    const auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::acceptor acceptor(executor, {asio::ip::make_address("127.0.0.1"), 0});
    auto server_context = take(tls_context(fixture.options(fixture.server, "server"), true));
    auto old_context = take(tls_context(fixture.options(fixture.client, "old-client")));
    auto new_context = take(tls_context(fixture.options(replacement_certificate, "new-client")));
    asio::ssl::stream<asio::ip::tcp::socket> old_stream(executor, *server_context);
    asio::ssl::stream<asio::ip::tcp::socket> new_stream(executor, *server_context);
    asio::steady_timer ready(executor, asio::steady_timer::time_point::max());
    auto old_server = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                        // closure outlives the structured join or run
      co_await acceptor.async_accept(old_stream.next_layer(), asio::use_awaitable);
      co_await old_stream.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
      const auto verified = take(authenticated_peer(old_stream.native_handle()));
      check(trust.attach(
          "old", "connector1", verified,
          [&] {
            old_closed = true;
            boost::system::error_code ignored;
            (void)old_stream.next_layer().close(ignored);
          },
          std::chrono::system_clock::now()));
      for (unsigned int i = 0; i < 100; ++i) {
        std::array<unsigned char, 1> data{};
        co_await asio::async_read(old_stream, asio::buffer(data), asio::use_awaitable);
        EXPECT_TRUE(trust.authenticate("connector1", verified, std::chrono::system_clock::now()));
        EXPECT_EQ(data[0], i);
        events.push_back(i);
        if (i == 50) {
          check(trust.renew("connector1", verified, replacement, std::chrono::system_clock::now()));
          EXPECT_FALSE(old_closed);
        }
      }
      ready.cancel();
      std::array<char, 1> data{};
      auto [error, size] = co_await asio::async_read(old_stream, asio::buffer(data), kAwait);
      EXPECT_TRUE(error);
    };
    auto old_client = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                        // closure outlives the structured join or run
      asio::ssl::stream<asio::ip::tcp::socket> stream(executor, *old_context);
      check(configure_hostname(stream.native_handle(), "localhost"));
      co_await stream.next_layer().async_connect(acceptor.local_endpoint(), asio::use_awaitable);
      co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
      for (unsigned int i = 0; i < 100; ++i) {
        const std::array<unsigned char, 1> data{static_cast<unsigned char>(i)};
        co_await asio::async_write(stream, asio::buffer(data), asio::use_awaitable);
      }
      std::array<char, 1> data{};
      auto [error, size] = co_await asio::async_read(stream, asio::buffer(data), kAwait);
      old_client_rejected = static_cast<bool>(error);
    };
    auto new_server = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                        // closure outlives the structured join or run
      // Starts only after the old stream has finished receiving its first 100 events.
      (void)co_await ready.async_wait(kAwait);
      co_await acceptor.async_accept(new_stream.next_layer(), asio::use_awaitable);
      co_await new_stream.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
      const auto verified = take(authenticated_peer(new_stream.native_handle()));
      EXPECT_EQ(take(trust.authenticate("connector1", verified, std::chrono::system_clock::now())), "tenant1");
      check(trust.revoke("connector1", original.fingerprint()));
      EXPECT_TRUE(old_closed);
      for (unsigned int i = 100; i < 200; ++i) {
        std::array<unsigned char, 1> data{};
        co_await asio::async_read(new_stream, asio::buffer(data), asio::use_awaitable);
        EXPECT_EQ(data[0], i);
        events.push_back(i);
      }
      const std::array<char, 1> marker{'a'};
      co_await asio::async_write(new_stream, asio::buffer(marker), asio::use_awaitable);
    };
    auto new_client = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                        // closure outlives the structured join or run
      (void)co_await ready.async_wait(kAwait);
      asio::ssl::stream<asio::ip::tcp::socket> stream(executor, *new_context);
      check(configure_hostname(stream.native_handle(), "localhost"));
      co_await stream.next_layer().async_connect(acceptor.local_endpoint(), asio::use_awaitable);
      co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
      for (unsigned int i = 100; i < 200; ++i) {
        const std::array<unsigned char, 1> data{static_cast<unsigned char>(i)};
        co_await asio::async_write(stream, asio::buffer(data), asio::use_awaitable);
      }
      std::array<char, 1> data{};
      co_await asio::async_read(stream, asio::buffer(data), asio::use_awaitable);
    };
    // Only the first server accepts initially; the new client is gated by ready.
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): structured join
    co_await ((old_server() && old_client()) && (new_server() && new_client()));
  };
  run(operation());
  EXPECT_TRUE(old_closed);
  EXPECT_TRUE(old_client_rejected);
  EXPECT_EQ(events.size(), 200);
  EXPECT_FALSE(trust.authenticate("connector1", original, std::chrono::system_clock::now()));
  EXPECT_TRUE(trust.authenticate("connector1", replacement, std::chrono::system_clock::now()));
}

TEST(SecurityComponent, PostgreSqlRequiresTrustedHostAndClientCertificate) {
  auto operation = []() -> asio::awaitable<void> {
    pg::ConnectionOptions options{.host = "postgres-tls",
                                  .database = "psim",
                                  .user = "postgres",
                                  .password = "psim-test-tls-postgres",
                                  .properties = {{"sslrootcert", "/repo/.cache/security-tls/ca.pem"},
                                                 {"sslcert", "/repo/.cache/security-tls/client.pem"},
                                                 {"sslkey", "/repo/.cache/security-tls/client.key"}}};
    auto connection = take(co_await pg::Connection::connect(options));
    EXPECT_EQ(take(co_await connection->query({.sql = "SELECT 17"})).integer(0, 0).value(), 17);
    connection->close();
    options.properties["sslrootcert"] = "/repo/.cache/valkey-tls/ca.pem";
    EXPECT_FALSE(co_await pg::Connection::connect(options));
    options.properties["sslrootcert"] = "/repo/.cache/security-tls/ca.pem";
    options.properties.erase("sslcert");
    options.properties.erase("sslkey");
    EXPECT_FALSE(co_await pg::Connection::connect(options));
    options.properties["ssl_min_protocol_version"] = "TLSv1.1";
    EXPECT_FALSE(co_await pg::Connection::connect(options));
    options.properties["ssl_min_protocol_version"] = "TLSv1.3";
    options.properties["sslmode"] = "require";
    EXPECT_FALSE(co_await pg::Connection::connect(options));
  };
  run(operation());
}

TEST(SecurityComponent, ValkeyMtlsUsesSharedTlsPolicyAndProtectedPrivateKey) {
  auto operation = []() -> asio::awaitable<void> {
    redis::ConnectionOptions options{.endpoint = {"valkey-tls", "6380"},
                                     .credentials = {.password = "psim-dev-valkey"},
                                     .tls = {.ca_file = "/repo/.cache/valkey-tls/ca.pem",
                                             .certificate_file = "/repo/.cache/valkey-tls/client.pem",
                                             .private_key_file = "/repo/.cache/valkey-tls/client.key"}};
    auto connection = take(co_await redis::Connection::connect(options));
    EXPECT_EQ(take(co_await connection->execute({"PING"})).data, "PONG");
    connection->close();
    options.tls.ca_file = "/repo/.cache/security-tls/ca.pem";
    EXPECT_FALSE(co_await redis::Connection::connect(options));
  };
  run(operation());
}

TEST(SecurityComponent, KafkaProducesAndConsumesOverVerifiedMtls) {
  async::BlockingPool blocking(1, 4);
  auto operation = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                     // closure outlives the structured join or run
    const auto executor = co_await asio::this_coro::executor;
    const std::string topic = "psim.security.test." + std::to_string(std::random_device{}());
    kafka::ClientOptions client{
        .bootstrap = "kafka-tls:19092",
        .client_id = "psim-security-test",
        .security = kafka::TransportSecurity{.tls = {.ca_file = "/repo/.cache/security-tls/ca.pem",
                                                     .certificate_file = "/repo/.cache/security-tls/client.pem",
                                                     .private_key_file = "/repo/.cache/security-tls/client.key"}}};
    auto producer =
        take(kafka::Producer::create(executor, {.client = client, .delivery_timeout = std::chrono::seconds(15)}));
    auto delivery =
        take(co_await producer->send({.topic = topic, .key = "key", .value = "event-canary", .partition = 0}));
    EXPECT_EQ(delivery.partition, 0);
    auto consumer = take(kafka::Consumer::create(executor, {.client = client, .group_id = topic, .topics = {topic}},
                                                 {.blocking = &blocking}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool received = false;
    while (!received && std::chrono::steady_clock::now() < deadline) {
      auto records = take(co_await consumer->poll(10, std::chrono::milliseconds(200)));
      for (const auto& record : records) {
        EXPECT_EQ(record.value(), "event-canary");
        received = true;
      }
    }
    EXPECT_TRUE(received);
    co_await consumer->close();
    producer.reset();
    // The same broker refuses an unrelated CA and a TLS client without a certificate.
    client.security->tls.boundary = Boundary::kExternal;
    EXPECT_FALSE(kafka::Producer::create(executor, {.client = client}));
    client.security->tls.boundary = Boundary::kInternal;
    client.security->tls.ca_file = "/repo/.cache/valkey-tls/ca.pem";
    auto untrusted =
        take(kafka::Producer::create(executor, {.client = client, .delivery_timeout = std::chrono::milliseconds(500)}));
    EXPECT_FALSE(co_await untrusted->send({.topic = topic, .key = "key", .value = "must-not-arrive", .partition = 0}));
    untrusted.reset();
    client.security->tls.ca_file = "/repo/.cache/security-tls/ca.pem";
    client.security->tls.certificate_file.clear();
    client.security->tls.private_key_file.clear();
    EXPECT_FALSE(kafka::Producer::create(executor, {.client = client}));
    client.security.reset();
    EXPECT_FALSE(kafka::Producer::create(executor, {.client = client}));
    client.allow_plaintext = true;
    client.properties["ssl.endpoint.identification.algorithm"] = "none";
    EXPECT_FALSE(kafka::Producer::create(executor, {.client = client}));
  };
  run(operation());
}
}  // namespace
}  // namespace psim::platform::security
