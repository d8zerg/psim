#include "psim/platform/pg/store.hpp"

#include <boost/asio/this_coro.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "sql.hpp"

namespace psim::platform::pg {
namespace {
std::string encode_bytes(std::string_view value) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    encoded.push_back(kDigits[byte >> 4]);
    encoded.push_back(kDigits[byte & 15]);
  }
  return encoded;
}

std::string decode_bytes(std::string_view hex) {
  if (hex.size() % 2 != 0
      || !std::ranges::all_of(hex, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); })) {
    throw std::invalid_argument("invalid stored PostgreSQL outbox encoding");
  }
  std::string bytes;
  auto digit = [](char ch) {
    return ch <= '9' ? ch - '0' : ch - 'a' + 10;
  };
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<char>((digit(hex[i]) * 16) + digit(hex[i + 1])));
  }
  return bytes;
}
}  // namespace

asio::awaitable<Result<void>> MessageStore::initialize(Connection* connection) {
  if (!connection || !detail::identifier(schema_)) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL message store");
  }
  auto transaction = co_await Transaction::begin(connection);
  if (!transaction) {
    co_return std::unexpected(transaction.error());
  }
  const auto lock = co_await connection->query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1, 0))",
                                                .parameters = {Parameter::text(schema_ + ":migrations")}});
  if (!lock) {
    co_return std::unexpected(lock.error());
  }
  auto schema = co_await ensure_schema(connection, schema_);
  if (!schema) {
    co_return schema;
  }
  for (const auto* sql :
       {"CREATE TABLE IF NOT EXISTS @schema@.psim_inbox (consumer text NOT NULL, event_id text NOT NULL, fingerprint "
        "text NOT NULL, received_at timestamptz NOT NULL DEFAULT clock_timestamp(), PRIMARY KEY (consumer, event_id))",
        "CREATE TABLE IF NOT EXISTS @schema@.psim_outbox (sequence bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY, "
        "event_id text NOT NULL UNIQUE, topic text NOT NULL, key bytea NOT NULL, payload bytea NOT NULL, headers jsonb "
        "NOT NULL, partition integer, fingerprint text NOT NULL, created_at timestamptz NOT NULL DEFAULT "
        "clock_timestamp(), published_at "
        "timestamptz)",
        "CREATE INDEX IF NOT EXISTS psim_outbox_pending ON @schema@.psim_outbox (sequence) WHERE published_at IS "
        "NULL"}) {
    const auto result = co_await connection->query({.sql = *detail::schema_sql(schema_, sql)});
    if (!result) {
      co_return std::unexpected(result.error());
    }
  }
  co_return co_await transaction->commit();
}

asio::awaitable<Result<bool>> MessageStore::accept(Transaction* transaction, std::string consumer, std::string id,
                                                   std::string fingerprint) {
  if (!transaction || !transaction->active() || !detail::identifier(schema_) || consumer.empty() || id.empty()
      || consumer.size() > 1024 || id.size() > 1024 || fingerprint.empty() || fingerprint.size() > 1024) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL inbox identity");
  }
  auto& connection = transaction->connection();
  const auto inserted = co_await connection.query(
      {.sql = *detail::schema_sql(schema_,
                                  "INSERT INTO @schema@.psim_inbox (consumer, event_id, fingerprint) VALUES ($1, $2, "
                                  "$3) ON CONFLICT DO NOTHING RETURNING event_id"),
       .parameters = {Parameter::text(consumer), Parameter::text(id), Parameter::text(fingerprint)}});
  if (!inserted) {
    co_return std::unexpected(inserted.error());
  }
  if (inserted->size() == 1) {
    co_return true;
  }
  const auto existing = co_await connection.query(
      {.sql = *detail::schema_sql(schema_,
                                  "SELECT fingerprint FROM @schema@.psim_inbox WHERE consumer = $1 AND event_id = $2"),
       .parameters = {Parameter::text(consumer), Parameter::text(id)}});
  if (!existing) {
    co_return std::unexpected(existing.error());
  }
  if (existing->size() != 1 || existing->value(0, 0) != fingerprint) {
    co_return fail(ErrorCode::kCommonIdempotencyKeyReused, "PostgreSQL inbox identity has different content");
  }
  co_return false;
}

asio::awaitable<Result<void>> MessageStore::append(Transaction* transaction, OutboxMessage message) {
  if (!transaction || !transaction->active() || !detail::identifier(schema_) || message.id.empty()
      || message.id.size() > 1024 || message.record.topic.empty() || message.record.topic.size() > 249
      || message.record.topic == "." || message.record.topic == ".."
      || message.record.key.size() > std::size_t{64} * 1024 || message.record.value.size() > std::size_t{1024} * 1024
      || (message.record.partition && *message.record.partition < 0)
      || !std::ranges::all_of(message.record.topic, [](char ch) {
           return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.'
                  || ch == '_' || ch == '-';
         })) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL outbox message");
  }
  std::string headers;
  std::string fingerprint;
  try {
    kafka::Headers encoded_headers;
    std::size_t header_bytes = 0;
    for (const auto& [name, value] : message.record.headers) {
      if (name.empty() || name.find('\0') != std::string::npos) {
        co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL outbox header name");
      }
      constexpr std::size_t kMaxHeaderBytes = std::size_t{32} * 1024;
      if (name.size() > kMaxHeaderBytes || value.size() > kMaxHeaderBytes
          || name.size() + value.size() + header_bytes > kMaxHeaderBytes || encoded_headers.size() >= 4096) {
        co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL outbox headers exceed size limit");
      }
      header_bytes += name.size() + value.size();
      encoded_headers.emplace_back(encode_bytes(name), encode_bytes(value));
    }
    headers = nlohmann::json(encoded_headers).dump();
    if (headers.size() > std::size_t{64} * 1024) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL outbox headers exceed size limit");
    }
    // Lengths separate arbitrary binary key/payload from the textual metadata.
    fingerprint = detail::fingerprint(
        message.record.topic + ":" + std::to_string(message.record.key.size()) + ":" + message.record.key
        + std::to_string(message.record.value.size()) + ":" + message.record.value + headers + ":"
        + (message.record.partition ? std::to_string(*message.record.partition) : "auto"));
  } catch (const std::exception&) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL outbox headers");
  }
  if (fingerprint.empty()) {
    co_return fail(ErrorCode::kCommonInternal, "PostgreSQL outbox checksum failed");
  }
  auto& connection = transaction->connection();
  const auto result = co_await connection.query(
      {.sql = *detail::schema_sql(schema_,
                                  "INSERT INTO @schema@.psim_outbox (event_id, topic, key, payload, headers, "
                                  "fingerprint, partition) VALUES ($1, $2, $3, "
                                  "$4, $5::jsonb, $6, $7) ON CONFLICT (event_id) DO NOTHING RETURNING event_id"),
       .parameters = {Parameter::text(message.id), Parameter::text(message.record.topic),
                      Parameter::bytes(message.record.key), Parameter::bytes(message.record.value),
                      Parameter::text(headers), Parameter::text(fingerprint),
                      message.record.partition ? Parameter::integer(*message.record.partition) : Parameter::null(23)}});
  if (!result) {
    co_return std::unexpected(result.error());
  }
  if (result->size() == 0) {
    const auto existing = co_await connection.query(
        {.sql = *detail::schema_sql(schema_, "SELECT fingerprint FROM @schema@.psim_outbox WHERE event_id = $1"),
         .parameters = {Parameter::text(message.id)}});
    if (!existing) {
      co_return std::unexpected(existing.error());
    }
    if (existing->size() != 1 || existing->value(0, 0) != fingerprint) {
      co_return fail(ErrorCode::kCommonIdempotencyKeyReused, "PostgreSQL outbox identity has different content");
    }
  }
  co_return Result<void>{};
}

namespace {
asio::awaitable<Result<void>> publish_kafka(kafka::Producer* producer, Duration timeout,
                                            std::span<const OutboxMessage> messages) {
  if (producer == nullptr || timeout <= Duration::zero()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Kafka outbox publisher");
  }
  if (producer->in_transaction()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "Kafka outbox publisher requires an idempotent producer");
  }
  for (const auto& message : messages) {
    auto record = message.record;
    std::erase_if(record.headers, [](const auto& header) { return header.first == "psim-event-id"; });
    record.headers.emplace_back("psim-event-id", message.id);
    const auto queued = co_await producer->enqueue(std::move(record));
    if (!queued) {
      co_return queued;
    }
  }
  co_return co_await producer->flush(timeout);
}
}  // namespace

Publisher kafka_publisher(kafka::Producer* producer, Duration timeout) {
  return [producer, timeout](std::span<const OutboxMessage> messages) {
    return publish_kafka(producer, timeout, messages);
  };
}

Duration outbox_poll_interval(const Result<std::size_t>& published) {
  if (!published) {
    return Duration{250};
  }
  return *published == 0 ? Duration{50} : Duration{1};
}

asio::awaitable<Result<std::size_t>> OutboxRelay::drain_once() {
  if (!pool_ || !publisher_ || !detail::identifier(store_.schema()) || batch_size_ == 0 || batch_size_ > 4096) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL outbox relay options");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  if (empty_) {
    const auto pending = co_await (*lease)->query(
        {.sql = *detail::schema_sql(store_.schema(),
                                    "SELECT 1 FROM @schema@.psim_outbox WHERE published_at IS NULL LIMIT 1")});
    if (!pending) {
      co_return std::unexpected(pending.error());
    }
    if (pending->size() == 0) {
      co_return 0;
    }
  }
  auto transaction = co_await Transaction::begin(&**lease);
  if (!transaction) {
    co_return std::unexpected(transaction.error());
  }
  const auto leader = co_await (*lease)->query({.sql = "SELECT pg_try_advisory_xact_lock(hashtextextended($1, 0))",
                                                .parameters = {Parameter::text(store_.schema() + ":outbox")}});
  if (!leader) {
    co_return std::unexpected(leader.error());
  }
  if (leader->value(0, 0) != "t") {
    empty_ = true;
    const auto rollback = co_await transaction->rollback();
    if (!rollback) {
      co_return std::unexpected(rollback.error());
    }
    co_return 0;
  }
  // Bound encoded results as well as row count, so valid maximum-size messages cannot stall the relay.
  const auto rows = co_await (*lease)->query(
      {.sql = *detail::schema_sql(
           store_.schema(),
           "WITH candidates AS (SELECT sequence, event_id, topic, key, payload, headers FROM @schema@.psim_outbox "
           "WHERE published_at IS NULL ORDER BY sequence LIMIT $1), sized AS (SELECT sequence, "
           "sum(2 * octet_length(key)::bigint + 2 * octet_length(payload)::bigint + octet_length(headers::text) "
           "+ octet_length(event_id) + octet_length(topic) + 256) OVER (ORDER BY sequence) AS bytes FROM candidates) "
           "SELECT o.event_id, o.topic, encode(o.key, 'hex'), encode(o.payload, 'hex'), o.headers::text, o.partition "
           "FROM @schema@.psim_outbox o JOIN sized s USING (sequence) WHERE s.bytes <= $2 "
           "ORDER BY o.sequence FOR UPDATE OF o"),
       .parameters = {Parameter::integer(static_cast<std::int64_t>(batch_size_)),
                      Parameter::integer(std::int64_t{4} * 1024 * 1024)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  std::vector<OutboxMessage> messages;
  for (std::size_t row = 0; row < rows->size(); ++row) {
    const auto id = rows->value(row, 0);
    const auto topic = rows->value(row, 1);
    const auto key = rows->value(row, 2);
    const auto payload = rows->value(row, 3);
    const auto headers = rows->value(row, 4);
    const auto partition = rows->value(row, 5);
    if (!id || !topic || !key || !payload || !headers) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "invalid stored PostgreSQL outbox fields");
    }
    std::optional<std::int32_t> partition_id;
    if (partition) {
      const auto parsed = rows->integer(row, 5);
      if (!parsed || *parsed < 0 || *parsed > std::numeric_limits<std::int32_t>::max()) {
        co_return fail(ErrorCode::kCommonInvalidArgument, "invalid stored PostgreSQL outbox partition");
      }
      partition_id = static_cast<std::int32_t>(*parsed);
    }
    try {
      auto decoded_headers = nlohmann::json::parse(*headers).get<kafka::Headers>();
      for (auto& [name, value] : decoded_headers) {
        name = decode_bytes(name);
        value = decode_bytes(value);
      }
      messages.push_back({.id = std::string(*id),
                          .record = {.topic = std::string(*topic),
                                     .key = decode_bytes(*key),
                                     .value = decode_bytes(*payload),
                                     .headers = std::move(decoded_headers),
                                     .partition = partition_id}});
    } catch (const std::exception&) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "invalid stored PostgreSQL outbox message");
    }
  }
  if (!messages.empty()) {
    const auto published = co_await publisher_(messages);
    if (!published) {
      co_return std::unexpected(published.error());
    }
    for (const auto& message : messages) {
      const auto marked = co_await (*lease)->query(
          {.sql = *detail::schema_sql(
               store_.schema(), "UPDATE @schema@.psim_outbox SET published_at = clock_timestamp() WHERE event_id = $1"),
           .parameters = {Parameter::text(message.id)}});
      if (!marked) {
        co_return std::unexpected(marked.error());
      }
    }
  }
  const auto committed = co_await transaction->commit();
  if (!committed) {
    co_return std::unexpected(committed.error());
  }
  empty_ = messages.empty();
  co_return messages.size();
}

asio::awaitable<Result<void>> OutboxRelay::run(async::Clock* clock, Duration interval) {
  if (clock == nullptr || interval <= Duration::zero()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL outbox polling interval");
  }
  co_await asio::this_coro::throw_if_cancelled(false);
  while ((co_await asio::this_coro::cancellation_state).cancelled() == asio::cancellation_type::none) {
    const auto published = co_await drain_once();
    if (!published && !published.error().retryable()) {
      co_return std::unexpected(published.error());
    }
    if (!published || *published == 0) {
      if (!(co_await clock->sleep_for(interval))) {
        break;
      }
    }
  }
  co_return Result<void>{};
}
}  // namespace psim::platform::pg
