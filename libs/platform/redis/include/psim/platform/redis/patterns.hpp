#pragma once

#include <google/protobuf/message_lite.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "psim/platform/redis/pool.hpp"
#include "psim/platform/redis/serde.hpp"

namespace psim::platform::redis {
/// Cache misses are optional values; errors remain errors for the caller's fallback policy.
class Cache {
 public:
  Cache(Pool* pool, Keyspace keys) : pool_(pool), keys_(std::move(keys)) {}

  asio::awaitable<Result<void>> put(std::string id, std::string bytes, Duration ttl);
  asio::awaitable<Result<std::optional<std::string>>> get(std::string id);
  asio::awaitable<Result<bool>> erase(std::string id);
  /// The caller supplies an owned, already serialized envelope (encode_protobuf or encode).
  asio::awaitable<Result<void>> put_encoded(std::string id, std::string envelope, Duration ttl);
  asio::awaitable<Result<std::optional<std::string>>> get_encoded(std::string id);

 private:
  Pool* pool_;
  Keyspace keys_;
};

struct LimitDecision {
  bool allowed = false;
  std::int64_t accepted = 0;
  std::chrono::milliseconds retry_after{};
};

/// Atomic fixed window beginning with the first accepted request, using server TTL.
/// Failures are explicit; the service chooses fail-open or fail-closed per use case.
class RateLimiter {
 public:
  RateLimiter(Pool* pool, Keyspace keys) : pool_(pool), keys_(std::move(keys)) {}

  asio::awaitable<Result<LimitDecision>> consume(std::string id, std::int64_t limit, Duration window);

 private:
  Pool* pool_;
  Keyspace keys_;
};

struct Session {
  /// A fresh unguessable generation token supplied by the gateway for each connection.
  std::string owner{};
  std::string gateway{};
  friend bool operator==(const Session&, const Session&) = default;
};

/// TTL leases with compare-and-renew/delete. An old owner cannot modify a new lease.
/// This is a recoverable routing hint, never an authorization or distributed lock source.
class SessionRegistry {
 public:
  SessionRegistry(Pool* pool, Keyspace keys) : pool_(pool), keys_(std::move(keys)) {}

  asio::awaitable<Result<bool>> claim(std::string id, Session session, Duration ttl);
  asio::awaitable<Result<std::optional<Session>>> lookup(std::string id);
  asio::awaitable<Result<bool>> renew(std::string id, Session session, Duration ttl);
  asio::awaitable<Result<bool>> release(std::string id, Session session);

 private:
  asio::awaitable<Result<bool>> compare(std::string id, Session session, Duration ttl, bool remove);
  Pool* pool_;
  Keyspace keys_;
};

/// Ephemeral notifications through PUBLISH. Receipt and delivery are not durable.
class Notifications {
 public:
  Notifications(Pool* pool, Keyspace keys) : pool_(pool), keys_(std::move(keys)) {}

  asio::awaitable<Result<std::int64_t>> publish(std::string channel, std::string bytes);

 private:
  Pool* pool_;
  Keyspace keys_;
};
}  // namespace psim::platform::redis
