#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/this_coro.hpp>
#include <google/protobuf/wrappers.pb.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/redis/connection.hpp"
#include "psim/platform/redis/patterns.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/redis/serde.hpp"
#include "support.hpp"

namespace psim::platform::redis::testing {

namespace {
asio::awaitable<void> cancelled_write(Connection* connection, std::string key, Result<Reply>* result) {
  *result = co_await connection->execute({"INCR", std::move(key)});
}

asio::awaitable<int> consume_many(RateLimiter* limiter, std::string id) {
  int accepted = 0;
  for (int i = 0; i < 100; ++i) {
    auto decision = take(co_await limiter->consume(id, 75, std::chrono::seconds{30}));
    accepted += decision.allowed ? 1 : 0;
  }
  co_return accepted;
}

asio::awaitable<int> consume_tokens(TokenBucket* limiter) {
  int accepted = 0;
  for (int i = 0; i < 100; ++i) {
    accepted += take(co_await limiter->consume("user", 1, 75)).allowed ? 1 : 0;
  }
  co_return accepted;
}
}  // namespace

TEST(RedisConnection, BinaryParametersAndPipelineKeepResponseOrder) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = take(co_await Connection::connect(options()));
    const auto key = unique();
    const std::string binary("a\0\xff", 3);
    const auto replies = take(co_await connection->pipeline(
        {{"SET", key, binary, "PX", "30000"}, {"GET", key}, {"EXISTS", key}, {"DEL", key}, {"GET", key}}));
    EXPECT_EQ(replies.size(), 5);
    EXPECT_EQ(replies[0].data, "OK");
    EXPECT_EQ(replies[1].data, binary);
    EXPECT_EQ(take(replies[2].integer()), 1);
    EXPECT_EQ(take(replies[3].integer()), 1);
    EXPECT_EQ(replies[4].kind, Reply::Kind::kNull);
    EXPECT_TRUE(connection->reusable());
  };
  run(scenario());
}

TEST(RedisConnection, ErrorsAreRedactedAndBadCommandsDoNotPoisonTheConnection) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = take(co_await Connection::connect(options()));
    auto error = co_await connection->execute({"EVAL", "return redis.error_reply('ERR private-password')", "0"});
    EXPECT_FALSE(error);
    EXPECT_EQ(error.error().message().find("private-password"), std::string::npos);
    EXPECT_FALSE(co_await connection->execute({}));
    EXPECT_FALSE(co_await connection->execute({"get", "key"}));
    EXPECT_FALSE(co_await connection->execute({"MULTI"}));
    EXPECT_FALSE(co_await connection->execute({"SUBSCRIBE", "channel"}));
    EXPECT_FALSE(co_await connection->pipeline({}));
    EXPECT_FALSE(co_await connection->pipeline(std::vector<Command>(1025, {"PING"})));
    EXPECT_FALSE(co_await connection->execute({"SET", "key", std::string((4 * 1024 * 1024) + 1, 'x')}));
    EXPECT_EQ(take(co_await connection->execute({"PING"})).data, "PONG");
    const auto replies = take(co_await connection->pipeline({{"PING"}, {"GET", "key", "extra"}, {"PING"}}));
    EXPECT_EQ(replies[1].kind, Reply::Kind::kError);
    EXPECT_EQ(replies[2].data, "PONG");
  };
  run(scenario());
}

TEST(RedisConnection, AuthenticationTlsAndConnectionValidationFailClosed) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = options();
    config.credentials.password = "private-wrong-password";
    auto rejected = co_await Connection::connect(config);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().message().find("private-wrong-password"), std::string::npos);
    config = options();
    config.tls.enabled = true;
    config.connect_timeout = std::chrono::milliseconds{200};
    EXPECT_FALSE(co_await Connection::connect(config));
    config.tls.ca_file = "/no/private-ca-file";
    auto invalid_ca = co_await Connection::connect(config);
    EXPECT_FALSE(invalid_ca);
    EXPECT_EQ(invalid_ca.error().message().find("private-ca-file"), std::string::npos);
    config = options();
    config.endpoint.port = "0";
    EXPECT_FALSE(co_await Connection::connect(config));
    config = options();
    config.max_response_bytes = 100;
    EXPECT_FALSE(co_await Connection::connect(config));
  };
  run(scenario());
}

TEST(RedisConnection, BoundedResponsesCloseTheTransportBeforeUnboundedAllocation) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = options();
    config.max_response_bytes = 4096;
    auto connection = take(co_await Connection::connect(config));
    auto rejected = co_await connection->execute({"EVAL", "return string.rep('x',10000)", "0"});
    EXPECT_FALSE(rejected);
    EXPECT_FALSE(connection->reusable());
    EXPECT_FALSE(co_await connection->execute({"PING"}));
    connection = take(co_await Connection::connect(options()));
    EXPECT_FALSE(co_await connection->execute({"EVAL", "local t={}; for i=1,10000 do t[i]=i end; return t", "0"}));
    EXPECT_FALSE(connection->reusable());
  };
  run(scenario());
}

TEST(RedisConnection, MutualTlsChecksCaHostnameAndClientCertificate) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = options();
    config.endpoint = {"valkey-tls", "6380"};
    config.tls.enabled = true;
    config.tls.ca_file = "/repo/.cache/valkey-tls/ca.pem";
    config.tls.certificate_file = "/repo/.cache/valkey-tls/client.pem";
    config.tls.private_key_file = "/repo/.cache/valkey-tls/client.key";
    config.connect_timeout = std::chrono::seconds{1};
    auto connection = take(co_await Connection::connect(config));
    EXPECT_EQ(take(co_await connection->execute({"PING"})).data, "PONG");
    config.tls.server_name = "wrong.example";
    EXPECT_FALSE(co_await Connection::connect(config));
    config.tls.server_name.clear();
    config.tls.ca_file.clear();
    EXPECT_FALSE(co_await Connection::connect(config));
    config.tls.ca_file = "/repo/.cache/valkey-tls/ca.pem";
    config.tls.certificate_file.clear();
    config.tls.private_key_file.clear();
    EXPECT_FALSE(co_await Connection::connect(config));
  };
  run(scenario());
}

TEST(RedisConnection, DeadlineClosesTheConnectionWithoutReplayingTheWrite) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = options();
    auto connection = take(co_await Connection::connect(config));
    Admin admin(co_await asio::this_coro::executor, config.endpoint);
    co_await admin.execute({"CLIENT", "PAUSE", "250", "WRITE"});
    const auto key = unique();
    auto result = co_await connection->execute({"INCR", key}, std::chrono::milliseconds{30});
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::kCommonDeadlineExceeded);
    EXPECT_FALSE(connection->reusable());
    async::SystemClock clock;
    co_await clock.sleep_for(std::chrono::milliseconds{350});
    auto reconnected = take(co_await Connection::connect(config));
    const auto value = take(co_await reconnected->execute({"GET", key}));
    EXPECT_TRUE(value.kind == Reply::Kind::kNull || value.data == "1");
    take(co_await reconnected->execute({"DEL", key}));
  };
  run(scenario());
}

TEST(RedisConnection, CancellationClosesThePendingTransport) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = options();
    auto connection = take(co_await Connection::connect(config));
    Admin admin(co_await asio::this_coro::executor, config.endpoint);
    co_await admin.execute({"CLIENT", "PAUSE", "250", "WRITE"});
    const auto key = unique();
    Result<Reply> result = fail(ErrorCode::kCommonInternal, "pending");
    auto handle =
        async::spawn_cancellable(co_await asio::this_coro::executor, cancelled_write(connection.get(), key, &result));
    async::SystemClock clock;
    co_await clock.sleep_for(std::chrono::milliseconds{20});
    handle.cancel();
    co_await clock.sleep_for(std::chrono::milliseconds{50});
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().message(), "pending");
    EXPECT_FALSE(connection->reusable());
    co_await clock.sleep_for(std::chrono::milliseconds{300});
    auto fresh = take(co_await Connection::connect(config));
    take(co_await fresh->execute({"DEL", key}));
  };
  run(scenario());
}

TEST(RedisPool, CapacityDeadlinesLeasesAndCloseAreBounded) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options(), .size = 1, .acquire_timeout = std::chrono::milliseconds{40}});
    {
      auto lease = take(co_await pool.acquire());
      EXPECT_FALSE(co_await pool.acquire());
      EXPECT_EQ(pool.idle(), 0);
      auto moved = std::move(lease);
      EXPECT_EQ(take(co_await moved->execute({"PING"})).data, "PONG");
    }
    EXPECT_EQ(pool.idle(), 1);
    {
      auto lease = take(co_await pool.acquire());
      lease->close();
    }
    EXPECT_EQ(pool.idle(), 0);
    auto lease = take(co_await pool.acquire());
    pool.close();
    EXPECT_FALSE(co_await pool.acquire());
    EXPECT_EQ(take(co_await lease->execute({"PING"})).data, "PONG");
    Pool invalid({.connection = options(), .size = 0});
    EXPECT_FALSE(co_await invalid.acquire());
  };
  run(scenario());
}

TEST(RedisCache, TtlBinaryProtobufTenantIsolationAndInvalidInputs) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options()});
    const auto service = unique();
    Cache cache(&pool, {service, "tenant-1"});
    Cache other(&pool, {service, "tenant-2"});
    EXPECT_FALSE(take(co_await cache.get("missing")));
    const std::string bytes("a\0\xff", 3);
    check(co_await cache.put("binary", bytes, std::chrono::seconds{30}));
    EXPECT_EQ(take(co_await cache.get("binary")), std::optional<std::string>{bytes});
    EXPECT_FALSE(take(co_await other.get("binary")));
    EXPECT_TRUE(take(co_await cache.erase("binary")));
    EXPECT_FALSE(take(co_await cache.erase("binary")));
    check(co_await cache.put("short", "expires", std::chrono::milliseconds{30}));
    async::SystemClock clock;
    co_await clock.sleep_for(std::chrono::milliseconds{60});
    EXPECT_FALSE(take(co_await cache.get("short")));
    google::protobuf::StringValue message;
    message.set_value("proto");
    check(co_await cache.put_encoded("proto", take(encode_protobuf(message)), std::chrono::seconds{30}));
    auto encoded = take(co_await cache.get_encoded("proto"));
    EXPECT_TRUE(encoded);
    EXPECT_EQ(take(decode_protobuf<google::protobuf::StringValue>(encoded.value())).value(), "proto");
    EXPECT_FALSE(co_await cache.get("proto"));
    EXPECT_FALSE(co_await cache.put("", "value", std::chrono::seconds{1}));
    EXPECT_FALSE(co_await cache.put("bad", "value", Duration::zero()));
    EXPECT_FALSE(co_await cache.put_encoded("bad", "not-an-envelope", std::chrono::seconds{1}));
    Cache no_pool(nullptr, {service, "tenant"});
    EXPECT_FALSE(co_await no_pool.get("key"));
  };
  run(scenario());
}

TEST(RedisLimiter, ConcurrentClientsEnforceOneAtomicFixedWindow) {
  auto scenario = []() -> asio::awaitable<void> {
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator&&
    Pool first({.connection = options()});
    Pool second({.connection = options()});
    const auto service = unique();
    RateLimiter a(&first, {service, "tenant"});
    RateLimiter b(&second, {service, "tenant"});
    const auto counts = co_await (consume_many(&a, "api") && consume_many(&b, "api"));
    EXPECT_EQ(std::get<0>(counts) + std::get<1>(counts), 75);
    EXPECT_FALSE(take(co_await a.consume("api", 75, std::chrono::seconds{30})).allowed);
    EXPECT_TRUE(take(co_await a.consume("short", 1, std::chrono::milliseconds{30})).allowed);
    EXPECT_FALSE(take(co_await a.consume("short", 1, std::chrono::milliseconds{30})).allowed);
    async::SystemClock clock;
    co_await clock.sleep_for(std::chrono::milliseconds{60});
    EXPECT_TRUE(take(co_await a.consume("short", 1, std::chrono::milliseconds{30})).allowed);
    EXPECT_FALSE(co_await a.consume("bad", 0, std::chrono::seconds{1}));
    EXPECT_FALSE(co_await a.consume("bad", 1, std::chrono::hours{31 * 24}));
  };
  run(scenario());
}

TEST(RedisTokenBucket, ConcurrentPoolsShareBurstDespiteClientRestartAndClockReversal) {
  auto scenario = []() -> asio::awaitable<void> {
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator&&
    const auto service = unique();
    const Keyspace keys(service, "tenant");
    const auto key = take(keys.key("bucket", "user"));
    Pool first({.connection = options()});
    Pool second({.connection = options()});
    TokenBucket a(&first, keys);
    TokenBucket b(&second, keys);
    Admin admin(co_await asio::this_coro::executor, options().endpoint);
    // A future server timestamp models a clock reversal without time-sensitive concurrency assertions.
    const auto seeded = co_await admin.execute(
        {"EVAL",
         "local t=redis.call('TIME'); redis.call('HSET',KEYS[1],'tokens',75000000,'updated',"
         "string.format('%.0f',tonumber(t[1])*1000000+tonumber(t[2])+10000000),'rate',1,'burst',75); "
         "return redis.call('PEXPIRE',KEYS[1],75000)",
         "1", key});
    EXPECT_TRUE(seeded);
    const auto counts = co_await (consume_tokens(&a) && consume_tokens(&b));
    EXPECT_EQ(std::get<0>(counts) + std::get<1>(counts), 75);
    first.close();
    Pool restarted({.connection = options()});
    EXPECT_FALSE(take(co_await TokenBucket(&restarted, keys).consume("user", 1, 75)).allowed);
    EXPECT_TRUE(take(co_await TokenBucket(&second, {service, "other-tenant"}).consume("user", 1, 75)).allowed);
    EXPECT_TRUE(take(co_await b.consume("other-user", 1, 75)).allowed);
    EXPECT_FALSE(co_await a.consume("user", 1, 75));
  };
  run(scenario());
}

TEST(RedisTokenBucket, RefillsContinuouslyExpiresAndRejectsMismatchedConfiguration) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options()});
    const Keyspace keys(unique(), "tenant");
    TokenBucket bucket(&pool, keys);
    EXPECT_TRUE(take(co_await bucket.consume("user", 1, 4)).allowed);
    EXPECT_FALSE(co_await bucket.consume("user", 2, 4));
    EXPECT_FALSE(co_await bucket.consume("user", 1, 5));
    EXPECT_TRUE(take(co_await bucket.consume("other-user", 1, 4)).allowed);
    EXPECT_FALSE(co_await bucket.consume("bad", 0, 1));
    EXPECT_FALSE(co_await bucket.consume("bad", 1, 0));
    EXPECT_FALSE(co_await bucket.consume("bad", 1000000001, 1));
    EXPECT_FALSE(co_await bucket.consume("bad", 1, 1000000001));
    EXPECT_FALSE(co_await bucket.consume("bad", 1, 1000000000));
    Admin admin(co_await asio::this_coro::executor, options().endpoint);
    const auto key = take(keys.key("bucket", "user"));
    const auto seeded =
        co_await admin.execute({"EVAL",
                                "local t=redis.call('TIME'); redis.call('HSET',KEYS[1],'tokens',0,'updated',"
                                "string.format('%.0f',tonumber(t[1])*1000000+tonumber(t[2])-1500000)); return 1",
                                "1", key});
    EXPECT_TRUE(seeded);
    EXPECT_TRUE(take(co_await bucket.consume("user", 1, 4)).allowed);
    auto exhausted = take(co_await bucket.consume("user", 1, 4));
    EXPECT_FALSE(exhausted.allowed);
    EXPECT_GT(exhausted.retry_after.count(), 0);
    EXPECT_LE(exhausted.retry_after.count(), 500);
    EXPECT_TRUE(take(co_await bucket.consume("expires", 100, 1)).allowed);
    async::SystemClock clock;
    co_await clock.sleep_for(std::chrono::milliseconds(40));
    auto connection = take(co_await Connection::connect(options()));
    EXPECT_EQ(take(take(co_await connection->execute({"EXISTS", take(keys.key("bucket", "expires"))})).integer()), 0);
    EXPECT_TRUE(take(co_await bucket.consume("expires", 100, 1)).allowed);
    take(co_await connection->execute({"SET", key, "invalid-state", "PX", "30000"}));
    EXPECT_FALSE(co_await bucket.consume("user", 1, 4));
    TokenBucket invalid(nullptr, keys);
    EXPECT_FALSE(co_await invalid.consume("user", 1, 4));
  };
  run(scenario());
}

TEST(RedisTokenBucket, HighRateWithUnitBurstUsesMicrosecondRefill) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options()});
    TokenBucket bucket(&pool, {unique(), "tenant"});
    for (int i = 0; i < 20; ++i) {
      EXPECT_TRUE(take(co_await bucket.consume("user", 100000, 1)).allowed);
      async::SystemClock clock;
      co_await clock.sleep_for(std::chrono::microseconds(50));
    }
  };
  run(scenario());
}

TEST(RedisSessions, OldOwnersCannotRenewOrRemoveTheNewLease) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options()});
    SessionRegistry sessions(&pool, {unique(), "tenant"});
    const Session old{"old-generation", "gateway-a"};
    const Session current{"new-generation", "gateway-b"};
    EXPECT_FALSE(take(co_await sessions.lookup("device")));
    EXPECT_TRUE(take(co_await sessions.claim("device", old, std::chrono::milliseconds{30})));
    EXPECT_FALSE(take(co_await sessions.claim("device", current, std::chrono::seconds{30})));
    EXPECT_FALSE(take(co_await sessions.renew("device", current, std::chrono::seconds{30})));
    EXPECT_EQ(take(co_await sessions.lookup("device")), std::optional<Session>{old});
    async::SystemClock clock;
    co_await clock.sleep_for(std::chrono::milliseconds{60});
    EXPECT_TRUE(take(co_await sessions.claim("device", current, std::chrono::seconds{30})));
    EXPECT_FALSE(take(co_await sessions.renew("device", old, std::chrono::seconds{30})));
    EXPECT_FALSE(take(co_await sessions.release("device", old)));
    EXPECT_TRUE(take(co_await sessions.renew("device", current, std::chrono::seconds{30})));
    EXPECT_TRUE(take(co_await sessions.release("device", current)));
    EXPECT_FALSE(take(co_await sessions.lookup("device")));
    EXPECT_FALSE(co_await sessions.claim("bad", {}, std::chrono::seconds{1}));
  };
  run(scenario());
}

TEST(RedisNotifications, BinaryNotificationReachesTheScopedSubscriber) {
  auto scenario = []() -> asio::awaitable<void> {
    const Keyspace keys(unique(), "tenant");
    Pool pool({.connection = options()});
    Notifications notifications(&pool, keys);
    Admin subscriber(co_await asio::this_coro::executor, options().endpoint);
    boost::redis::generic_response pushes;
    subscriber.native().set_receive_response(pushes);
    co_await subscriber.execute({"SUBSCRIBE", take(keys.key("notification", "updates"))});
    co_await subscriber.native().async_receive(asio::use_awaitable);
    pushes.value().clear();
    const std::string binary("a\0\xff", 3);
    EXPECT_EQ(take(co_await notifications.publish("updates", binary)), 1);
    co_await subscriber.native().async_receive(asio::use_awaitable);
    EXPECT_TRUE(pushes);
    EXPECT_EQ(pushes.value().size(), 4);
    EXPECT_EQ(pushes.value()[1].value, "message");
    EXPECT_EQ(take(decode(pushes.value()[3].value)).payload, binary);
    EXPECT_EQ(take(co_await notifications.publish("no-listener", binary)), 0);
    EXPECT_FALSE(co_await notifications.publish("", binary));
  };
  run(scenario());
}

TEST(RedisSentinel, DiscoveryFallsBackAndRejectsReplicasAndUnknownPrimaries) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = sentinel_options();
    config.sentinel.endpoints.insert(config.sentinel.endpoints.begin(), {"127.0.0.1", "1"});
    auto connection = take(co_await Connection::connect(config));
    EXPECT_EQ(take(co_await connection->execute({"PING"})).data, "PONG");
    config.sentinel.primary_name = "unknown-primary";
    EXPECT_FALSE(co_await Connection::connect(config));
    config = sentinel_options();
    config.sentinel.credentials.password = "private-wrong-sentinel-password";
    EXPECT_FALSE(co_await Connection::connect(config));
    config = options();
    config.endpoint.host = "valkey-replica-2";
    Admin admin(co_await asio::this_coro::executor, config.endpoint);
    auto role = co_await admin.execute({"ROLE"});
    if (role.value()[1].value != "master") {
      EXPECT_FALSE(co_await Connection::connect(config));
    }
  };
  run(scenario());
}

TEST(RedisSentinel, AutomaticFailoverRecoversPooledWritesUnderLoad) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = sentinel_options();
    config.connect_timeout = std::chrono::milliseconds{600};
    config.command_timeout = std::chrono::milliseconds{300};
    config.sentinel.probe_timeout = std::chrono::milliseconds{200};
    Pool pool({.connection = config, .size = 2, .acquire_timeout = std::chrono::milliseconds{800}});
    Endpoint previous;
    const auto key = unique();
    {
      auto lease = take(co_await pool.acquire());
      previous = lease->endpoint();
      take(co_await lease->execute({"SET", key, "0", "PX", "30000"}));
      async::SystemClock readiness;
      const auto deadline = readiness.steady() + std::chrono::seconds{30};
      std::int64_t replicas = 0;
      while (replicas < 2 && readiness.steady() < deadline) {
        replicas = take(take(co_await lease->execute({"WAIT", "2", "500"}, std::chrono::seconds{1})).integer());
        co_await readiness.sleep_for(std::chrono::milliseconds{100});
      }
      EXPECT_EQ(replicas, 2);
    }
    Admin admin(co_await asio::this_coro::executor, previous);
    co_await admin.execute({"CLIENT", "PAUSE", "8000", "ALL"});
    async::SystemClock clock;
    const auto started = clock.steady();
    int successful = 0;
    int transient_failures = 0;
    bool switched = false;
    while (clock.steady() - started < std::chrono::seconds{12}) {
      auto lease = co_await pool.acquire();
      if (!lease) {
        ++transient_failures;
      } else {
        auto result = co_await (*lease)->execute({"INCR", key});
        if (result) {
          ++successful;
          switched = switched || (*lease)->endpoint() != previous;
        } else {
          ++transient_failures;
        }
      }
      co_await clock.sleep_for(std::chrono::milliseconds{10});
    }
    EXPECT_TRUE(switched);
    EXPECT_GT(successful, 50);
    EXPECT_GT(transient_failures, 0);
    auto lease = take(co_await pool.acquire());
    EXPECT_NE(lease->endpoint(), previous);
    check(co_await lease->verify_primary());
    take(co_await lease->execute({"DEL", key}));
  };
  run(scenario());
}

TEST(RedisPool, NetworkCutClosesTheLeaseAndRecoveryBuildsANewConnection) {
  proxy_enable(true);
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options(true), .size = 1});
    {
      auto lease = take(co_await pool.acquire());
      proxy_enable(false);
      const auto result = co_await lease->execute({"PING"}, std::chrono::milliseconds{100});
      proxy_enable(true);
      EXPECT_FALSE(result);
      EXPECT_FALSE(lease->reusable());
    }
    auto lease = take(co_await pool.acquire());
    EXPECT_EQ(take(co_await lease->execute({"PING"})).data, "PONG");
  };
  run(scenario());
  proxy_enable(true);
}
}  // namespace psim::platform::redis::testing
