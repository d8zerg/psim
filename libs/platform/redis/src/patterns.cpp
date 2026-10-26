#include "psim/platform/redis/patterns.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/redis/connection.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/redis/serde.hpp"

namespace psim::platform::redis {
namespace {
constexpr auto kMaxTtl = std::chrono::hours{24 * 30};
constexpr std::string_view kLimitScript =
    "local c=tonumber(redis.call('GET',KEYS[1]) or '0'); "
    "if c>=tonumber(ARGV[1]) then return {0,c,redis.call('PTTL',KEYS[1])} end; "
    "c=redis.call('INCR',KEYS[1]); if c==1 then redis.call('PEXPIRE',KEYS[1],ARGV[2]) end; "
    "return {1,c,redis.call('PTTL',KEYS[1])}";
constexpr std::string_view kRenewScript =
    "if redis.call('GET',KEYS[1])==ARGV[1] then return redis.call('PEXPIRE',KEYS[1],ARGV[2]) end; return 0";
// Microsecond fixed point and explicit decimal encoding retain full timestamp precision in Lua.
constexpr std::string_view kBucketScript =
    "local rate=tonumber(ARGV[1]); local burst=tonumber(ARGV[2]); local cap=burst*1000000; "
    "local time=redis.call('TIME'); local now=tonumber(time[1])*1000000+tonumber(time[2]); "
    "local state=redis.call('HMGET',KEYS[1],'tokens','updated','rate','burst'); local tokens=cap; "
    "if state[1] then "
    " if tonumber(state[3])~=rate or tonumber(state[4])~=burst then "
    "  return redis.error_reply('ERR token bucket configuration mismatch') end; "
    " local updated=tonumber(state[2]); tokens=tonumber(state[1]); "
    " if not updated or not tokens or tokens<0 or tokens>cap then "
    "  return redis.error_reply('ERR invalid token bucket state') end; "
    " now=math.max(now,updated); tokens=math.min(cap,tokens+math.min(now-updated,tonumber(ARGV[3])*1000)*rate); "
    "end; "
    "local allowed=0; local retry=0; "
    "if tokens>=1000000 then tokens=tokens-1000000; allowed=1 "
    "else retry=math.ceil((1000000-tokens)/(rate*1000)) end; "
    "redis.call('HSET',KEYS[1],'tokens',string.format('%.0f',tokens),'updated',string.format('%.0f',now),"
    "'rate',rate,'burst',burst); "
    "redis.call('PEXPIRE',KEYS[1],math.max(1,math.ceil((cap-tokens)/(rate*1000)))); return {allowed,retry}";
constexpr std::string_view kReleaseScript =
    "if redis.call('GET',KEYS[1])==ARGV[1] then return redis.call('DEL',KEYS[1]) end; return 0";

Result<std::string> ttl_argument(Duration ttl) {
  if (ttl < std::chrono::milliseconds{1} || ttl > kMaxTtl) {
    return fail(ErrorCode::kCommonInvalidArgument, "Redis TTL must be between 1 ms and 30 days");
  }
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(ttl).count());
}

asio::awaitable<Result<Reply>> execute(Pool* pool, Command command) {
  if (pool == nullptr) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "Redis pool is required");
  }
  auto lease = co_await pool->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  co_return co_await (*lease)->execute(std::move(command));
}

Result<std::string> serialize_session(const Session& session) {
  if (session.owner.empty() || session.owner.size() > 512 || session.gateway.empty() || session.gateway.size() > 512) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis session record");
  }
  const nlohmann::json document = {{"version", 1}, {"owner", session.owner}, {"gateway", session.gateway}};
  try {
    return document.dump();
  } catch (const nlohmann::json::exception&) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid UTF-8 in Redis session record");
  }
}

Result<Session> deserialize_session(std::string_view bytes) {
  const auto document = nlohmann::json::parse(bytes, nullptr, false);
  if (!document.is_object() || document.size() != 3 || !document.contains("version") || document["version"] != 1
      || !document.contains("owner") || !document["owner"].is_string() || !document.contains("gateway")
      || !document["gateway"].is_string()) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis session record");
  }
  Session session{document["owner"].get<std::string>(), document["gateway"].get<std::string>()};
  auto validated = serialize_session(session);
  if (!validated) {
    return std::unexpected(validated.error());
  }
  return session;
}
}  // namespace

asio::awaitable<Result<void>> Cache::put(std::string id, std::string bytes, Duration ttl) {
  auto envelope = encode({.payload = std::move(bytes)});
  if (!envelope) {
    co_return std::unexpected(envelope.error());
  }
  co_return co_await put_encoded(std::move(id), std::move(*envelope), ttl);
}

asio::awaitable<Result<void>> Cache::put_encoded(std::string id, std::string envelope, Duration ttl) {
  auto key = keys_.key("cache", id);
  auto expiry = ttl_argument(ttl);
  auto value = decode(envelope);
  if (!key || !expiry || !value) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis cache key, TTL or value");
  }
  auto reply = co_await execute(pool_, {"SET", *key, std::move(envelope), "PX", *expiry});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind != Reply::Kind::kString || reply->data != "OK") {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis cache write response");
  }
  co_return Result<void>{};
}

asio::awaitable<Result<std::optional<std::string>>> Cache::get_encoded(std::string id) {
  auto key = keys_.key("cache", id);
  if (!key) {
    co_return std::unexpected(key.error());
  }
  auto reply = co_await execute(pool_, {"GET", *key});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind == Reply::Kind::kNull) {
    co_return std::optional<std::string>{};
  }
  if (reply->kind != Reply::Kind::kString) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis cache read response");
  }
  auto envelope = decode(reply->data);
  if (!envelope) {
    co_return std::unexpected(envelope.error());
  }
  co_return std::optional<std::string>{std::move(reply->data)};
}

asio::awaitable<Result<std::optional<std::string>>> Cache::get(std::string id) {
  auto bytes = co_await get_encoded(std::move(id));
  if (!bytes) {
    co_return std::unexpected(bytes.error());
  }
  if (!*bytes) {
    co_return std::optional<std::string>{};
  }
  auto envelope = decode(**bytes);
  if (!envelope || envelope->format != Format::kBytes) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "Redis cache codec mismatch");
  }
  co_return std::optional<std::string>{std::move(envelope->payload)};
}

asio::awaitable<Result<bool>> Cache::erase(std::string id) {
  auto key = keys_.key("cache", id);
  if (!key) {
    co_return std::unexpected(key.error());
  }
  auto reply = co_await execute(pool_, {"DEL", *key});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  auto count = reply->integer();
  if (!count) {
    co_return std::unexpected(count.error());
  }
  co_return *count != 0;
}

asio::awaitable<Result<LimitDecision>> RateLimiter::consume(std::string id, std::int64_t limit, Duration window) {
  auto key = keys_.key("limit", id);
  auto expiry = ttl_argument(window);
  if (!key || !expiry || limit <= 0 || limit > 1000000000) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis rate limit");
  }
  auto reply = co_await execute(pool_, {"EVAL", std::string(kLimitScript), "1", *key, std::to_string(limit), *expiry});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind != Reply::Kind::kArray || reply->elements.size() != 3) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis rate limit response");
  }
  auto allowed = reply->elements[0].integer();
  auto accepted = reply->elements[1].integer();
  auto remaining = reply->elements[2].integer();
  if (!allowed || !accepted || !remaining || *accepted < 0 || *remaining < 0 || (*allowed != 0 && *allowed != 1)) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis rate limit response");
  }
  co_return LimitDecision{*allowed == 1, *accepted, std::chrono::milliseconds{*remaining}};
}

asio::awaitable<Result<TokenDecision>> TokenBucket::consume(std::string id, std::int64_t rate, std::int64_t burst) {
  auto key = keys_.key("bucket", id);
  if (!key || rate <= 0 || rate > 1000000000 || burst <= 0 || burst > 1000000000) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis token bucket");
  }
  const auto refill = std::chrono::milliseconds{(burst * 1000 + rate - 1) / rate};
  if (refill > kMaxTtl) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "Redis token bucket refill exceeds 30 days");
  }
  auto reply = co_await execute(pool_, {"EVAL", std::string(kBucketScript), "1", *key, std::to_string(rate),
                                        std::to_string(burst), std::to_string(refill.count())});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind != Reply::Kind::kArray || reply->elements.size() != 2) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis token bucket response");
  }
  auto allowed = reply->elements[0].integer();
  auto retry = reply->elements[1].integer();
  if (!allowed || !retry || (*allowed != 0 && *allowed != 1) || *retry < 0 || *retry > 1000
      || (*allowed == 1 && *retry != 0) || (*allowed == 0 && *retry == 0)) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis token bucket response");
  }
  co_return TokenDecision{*allowed == 1, std::chrono::milliseconds{*retry}};
}

asio::awaitable<Result<bool>> SessionRegistry::claim(std::string id, Session session, Duration ttl) {
  auto key = keys_.key("session", id);
  auto value = serialize_session(session);
  auto expiry = ttl_argument(ttl);
  if (!key || !value || !expiry) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis session lease");
  }
  auto reply = co_await execute(pool_, {"SET", *key, *value, "NX", "PX", *expiry});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind == Reply::Kind::kNull) {
    co_return false;
  }
  if (reply->kind != Reply::Kind::kString || reply->data != "OK") {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis session claim response");
  }
  co_return true;
}

asio::awaitable<Result<std::optional<Session>>> SessionRegistry::lookup(std::string id) {
  auto key = keys_.key("session", id);
  if (!key) {
    co_return std::unexpected(key.error());
  }
  auto reply = co_await execute(pool_, {"GET", *key});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  if (reply->kind == Reply::Kind::kNull) {
    co_return std::optional<Session>{};
  }
  if (reply->kind != Reply::Kind::kString) {
    co_return fail(ErrorCode::kCommonInternal, "invalid Redis session read response");
  }
  auto session = deserialize_session(reply->data);
  if (!session) {
    co_return std::unexpected(session.error());
  }
  co_return std::optional<Session>{std::move(*session)};
}

asio::awaitable<Result<bool>> SessionRegistry::compare(std::string id, Session session, Duration ttl, bool remove) {
  auto key = keys_.key("session", id);
  auto value = serialize_session(session);
  auto expiry = ttl_argument(ttl);
  if (!key || !value || !expiry) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis session lease");
  }
  auto reply = co_await execute(
      pool_, {"EVAL", std::string(remove ? kReleaseScript : kRenewScript), "1", *key, *value, *expiry});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  auto count = reply->integer();
  if (!count) {
    co_return std::unexpected(count.error());
  }
  co_return *count == 1;
}

asio::awaitable<Result<bool>> SessionRegistry::renew(std::string id, Session session, Duration ttl) {
  co_return co_await compare(std::move(id), std::move(session), ttl, false);
}

asio::awaitable<Result<bool>> SessionRegistry::release(std::string id, Session session) {
  co_return co_await compare(std::move(id), std::move(session), std::chrono::seconds{1}, true);
}

asio::awaitable<Result<std::int64_t>> Notifications::publish(std::string channel, std::string bytes) {
  auto key = keys_.key("notification", channel);
  auto envelope = encode({.payload = std::move(bytes)});
  if (!key || !envelope) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis notification");
  }
  auto reply = co_await execute(pool_, {"PUBLISH", *key, *envelope});
  if (!reply) {
    co_return std::unexpected(reply.error());
  }
  co_return reply->integer();
}
}  // namespace psim::platform::redis
