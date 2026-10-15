#include "psim/platform/clickhouse/loader.hpp"

#include <boost/asio/this_coro.hpp>
#include <clickhouse/block.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/metrics.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::platform::clickhouse {
namespace {
using Json = nlohmann::json;

Json binary(std::string_view value) {
  return Json::binary(std::vector<std::uint8_t>(value.begin(), value.end()));
}

std::string unbinary(const Json& value) {
  const auto& bytes = value.get_binary();
  return {bytes.begin(), bytes.end()};
}

std::string unhex(std::string_view value) {
  std::string bytes;
  bytes.reserve(value.size() / 2);
  const auto digit = [](char ch) {
    return ch >= '0' && ch <= '9' ? ch - '0' : ch - 'a' + 10;
  };
  for (std::size_t index = 0; index + 1 < value.size(); index += 2) {
    bytes += static_cast<char>((digit(value[index]) * 16) + digit(value[index + 1]));
  }
  return bytes;
}

std::vector<pg::Parameter> key(const std::string& loader, const kafka::TopicPartition& partition) {
  return {pg::Parameter::text(loader), pg::Parameter::text(partition.topic),
          pg::Parameter::integer(partition.partition)};
}

Result<std::string> pack(const Converted& batch, std::span<const kafka::Message* const> messages,
                         const LoaderOptions& options) {
  auto block = detail::encode(batch.block, options.max_bytes);
  if (!block) {
    return std::unexpected(block.error());
  }
  std::set<std::size_t> indexes;
  Json rejected = Json::array();
  for (const auto& item : batch.rejected) {
    if (item.index >= messages.size() || !indexes.insert(item.index).second
        || item.code != ErrorCode::kProcessingPayloadInvalid) {
      return fail(ErrorCode::kCommonInvalidArgument, "converter returned invalid rejection indexes or error code");
    }
    const auto* message = messages[item.index];
    Json headers = Json::array();
    for (const auto& [name, value] : message->headers()) {
      headers.push_back({binary(name), binary(value)});
    }
    headers.push_back({binary("psim-error-code"), binary(info(item.code).name)});
    headers.push_back({binary("psim-source-topic"), binary(message->topic())});
    headers.push_back({binary("psim-source-partition"), binary(std::to_string(message->partition()))});
    headers.push_back({binary("psim-source-offset"), binary(std::to_string(message->offset()))});
    rejected.push_back(
        {{"key", binary(message->key())}, {"value", binary(message->value())}, {"headers", std::move(headers)}});
  }
  if (batch.block.GetRowCount() + indexes.size() != messages.size()) {
    return fail(ErrorCode::kCommonInvalidArgument, "converter did not account for every source record");
  }
  const Json document{{"format", 1},
                      {"table", options.table},
                      {"dlq", options.dlq},
                      {"block", binary(*block)},
                      {"rejected", std::move(rejected)}};
  const auto bytes = Json::to_cbor(document);
  if (bytes.size() > options.max_bytes) {
    return fail(ErrorCode::kCommonRateLimited, "prepared ClickHouse batch exceeds journal limit");
  }
  return std::string(bytes.begin(), bytes.end());
}
}  // namespace

Loader::Loader(Pool* destination, pg::Pool* journal, kafka::Consumer* source, kafka::Producer* dlq,
               LoaderOptions options, Converter converter, Metrics* metrics, LoadObserver observer)
    : destination_(destination),
      journal_(journal),
      source_(source),
      dlq_(dlq),
      options_(std::move(options)),
      converter_(std::move(converter)),
      metrics_(metrics),
      observer_(std::move(observer)) {}

asio::awaitable<Result<void>> Loader::initialize(pg::Pool* journal) {
  if (journal == nullptr) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "ClickHouse loader journal is required");
  }
  auto lease = co_await journal->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto schema = co_await (*lease)->query({.sql = "CREATE SCHEMA IF NOT EXISTS psim_clickhouse"});
  if (!schema) {
    co_return std::unexpected(schema.error());
  }
  auto table = co_await (*lease)->query(
      {.sql = "CREATE TABLE IF NOT EXISTS psim_clickhouse.pending_batches (loader text NOT NULL, topic text NOT NULL, "
              "partition integer NOT NULL, first_offset bigint NOT NULL, next_offset bigint NOT NULL, token text NOT "
              "NULL, "
              "payload bytea NOT NULL, applied boolean NOT NULL DEFAULT false, PRIMARY KEY (loader, topic, partition), "
              "CHECK (first_offset >= 0 AND next_offset > first_offset))"});
  if (!table) {
    co_return std::unexpected(table.error());
  }
  co_return Result<void>{};
}

asio::awaitable<Result<void>> Loader::deliver(kafka::TopicPartition partition) {
  const auto name = source_->group() + "/" + options_.name;
  auto lease = co_await journal_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto transaction = co_await pg::Transaction::begin(&**lease);
  if (!transaction) {
    co_return std::unexpected(transaction.error());
  }
  auto existing = co_await (*lease)->query(
      {.sql = "SELECT first_offset, next_offset, token, encode(payload, 'hex'), applied::integer "
              "FROM psim_clickhouse.pending_batches WHERE loader=$1 AND topic=$2 AND partition=$3 FOR UPDATE",
       .parameters = key(name, partition)});
  if (!existing) {
    co_return std::unexpected(existing.error());
  }
  if (existing->size() == 0) {
    co_return co_await transaction->commit();
  }
  const auto next = existing->integer(0, 1);
  const auto applied = existing->integer(0, 4);
  if (!next || !applied) {
    co_return fail(ErrorCode::kCommonInternal, "invalid ClickHouse journal offsets");
  }
  const std::string token(existing->value(0, 2).value_or(""));
  // Avoid re-emitting already committed DLQ transactions after a crash before journal cleanup.
  auto committed = co_await source_->committed({partition}, options_.kafka_timeout);
  if (!committed) {
    co_return std::unexpected(committed.error());
  }
  const bool already_committed = committed->contains(partition) && committed->at(partition) >= *next;
  Json document;
  if (!already_committed) {
    try {
      const auto bytes = unhex(existing->value(0, 3).value_or(""));
      document = Json::from_cbor(bytes);
      if (document.at("format") != 1 || document.at("table") != options_.table || document.at("dlq") != options_.dlq) {
        co_return fail(ErrorCode::kCommonConcurrentModification,
                       "loader configuration differs from its prepared journal");
      }
      if (*applied == 0) {
        auto block = detail::decode(unbinary(document.at("block")), options_.max_bytes);
        if (!block) {
          co_return std::unexpected(block.error());
        }
        if (block->GetRowCount() != 0) {
          auto inserted = co_await destination_->insert(options_.table, *block, token);
          if (!inserted) {
            co_return std::unexpected(inserted.error());
          }
        }
        if (observer_) {
          observer_(LoadStage::kInserted);
        }
        auto marked = co_await (*lease)->query({.sql = "UPDATE psim_clickhouse.pending_batches SET applied=true WHERE "
                                                       "loader=$1 AND topic=$2 AND partition=$3",
                                                .parameters = key(name, partition)});
        if (!marked) {
          co_return std::unexpected(marked.error());
        }
      }
    } catch (const std::exception&) {
      co_return fail(ErrorCode::kCommonInternal, "invalid prepared ClickHouse batch");
    }
    auto applied_commit = co_await transaction->commit();
    if (!applied_commit) {
      co_return std::unexpected(applied_commit.error());
    }
    if (observer_) {
      observer_(LoadStage::kApplied);
    }
    auto begun = dlq_->begin_transaction();
    if (!begun) {
      co_return std::unexpected(begun.error());
    }
    for (const auto& record : document.at("rejected")) {
      kafka::Headers headers;
      for (const auto& header : record.at("headers")) {
        headers.emplace_back(unbinary(header.at(0)), unbinary(header.at(1)));
      }
      auto sent = co_await dlq_->enqueue({.topic = options_.dlq,
                                          .key = unbinary(record.at("key")),
                                          .value = unbinary(record.at("value")),
                                          .headers = std::move(headers)});
      if (!sent) {
        (void)co_await dlq_->abort_transaction(options_.kafka_timeout);
        co_return std::unexpected(sent.error());
      }
      if (metrics_ != nullptr) {
        metrics_->dead_letter();
      }
    }
    auto offset_commit = co_await dlq_->commit_transaction(source_, {{partition, *next}}, options_.kafka_timeout);
    if (!offset_commit) {
      (void)co_await dlq_->abort_transaction(options_.kafka_timeout);
      co_return std::unexpected(offset_commit.error());
    }
    if (observer_) {
      observer_(LoadStage::kCommitted);
    }
  } else {
    auto release = co_await transaction->commit();
    if (!release) {
      co_return std::unexpected(release.error());
    }
  }
  auto parameters = key(name, partition);
  parameters.push_back(pg::Parameter::text(token));
  auto removed = co_await (*lease)->query(
      {.sql = "DELETE FROM psim_clickhouse.pending_batches WHERE loader=$1 AND topic=$2 AND partition=$3 AND token=$4",
       .parameters = std::move(parameters)});
  if (!removed) {
    co_return std::unexpected(removed.error());
  }
  co_return co_await source_->seek_and_wait({{partition, already_committed ? committed->at(partition) : *next}},
                                            options_.kafka_timeout);
}

asio::awaitable<Result<std::size_t>> Loader::run_once() {
  if (destination_ == nullptr || journal_ == nullptr || source_ == nullptr || dlq_ == nullptr || !converter_
      || !detail::identifier(options_.name) || !detail::identifier(options_.table) || options_.dlq.empty()
      || options_.max_messages == 0 || options_.max_messages > 1000000 || options_.max_bytes == 0
      || options_.flush_after.count() <= 0 || options_.kafka_timeout.count() <= 0) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse loader configuration");
  }
  kafka::Offsets retry;
  for (const auto& [partition, offset] : retry_offsets_) {
    if (source_->assignment().contains(partition)) {
      retry.emplace(partition, offset);
    }
  }
  if (!retry.empty()) {
    auto rewound = co_await source_->seek_and_wait(retry, options_.kafka_timeout);
    if (!rewound) {
      co_return std::unexpected(rewound.error());
    }
  }
  retry_offsets_.clear();
  std::vector<kafka::Message> messages;
  std::size_t bytes = 0;
  const auto deadline = std::chrono::steady_clock::now() + options_.flush_after;
  while (messages.size() < options_.max_messages && bytes < options_.max_bytes) {
    const auto left =
        std::max(Duration(1), std::chrono::duration_cast<Duration>(deadline - std::chrono::steady_clock::now()));
    auto batch = co_await source_->poll(1, left);
    if (!batch) {
      co_return std::unexpected(batch.error());
    }
    for (auto& message : *batch) {
      retry_offsets_.try_emplace(message.topic_partition(), message.offset());
      const auto size = message.value().size() + message.key().size();
      if (!messages.empty() && size > options_.max_bytes - bytes) {
        auto rewind_extra =
            co_await source_->seek_and_wait({{message.topic_partition(), message.offset()}}, options_.kafka_timeout);
        if (!rewind_extra) {
          co_return std::unexpected(rewind_extra.error());
        }
        bytes = options_.max_bytes;
        break;
      }
      bytes += size;
      messages.push_back(std::move(message));
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
  }
  // Rewind immediately: any conversion/insert/commit error is retryable in the same process.
  kafka::Offsets first;
  std::map<kafka::TopicPartition, std::vector<const kafka::Message*>> groups;
  for (const auto& message : messages) {
    if (!source_->assignment().contains(message.topic_partition())) {
      continue;
    }
    first.try_emplace(message.topic_partition(), message.offset());
    groups[message.topic_partition()].push_back(&message);
  }
  if (!first.empty()) {
    auto rewind = co_await source_->seek_and_wait(first, options_.kafka_timeout);
    if (!rewind) {
      co_return std::unexpected(rewind.error());
    }
  }
  auto lease = co_await journal_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  retry_offsets_.clear();
  auto pending = co_await (*lease)->query(
      {.sql = "SELECT topic, partition FROM psim_clickhouse.pending_batches WHERE loader=$1 ORDER BY topic, partition",
       .parameters = {pg::Parameter::text(source_->group() + "/" + options_.name)}});
  if (!pending) {
    co_return std::unexpected(pending.error());
  }
  std::vector<kafka::TopicPartition> recovery;
  for (std::size_t index = 0; index < pending->size(); ++index) {
    auto partition = pending->integer(index, 1);
    if (!partition || *partition < 0 || *partition > std::numeric_limits<std::int32_t>::max()) {
      co_return fail(ErrorCode::kCommonInternal, "invalid ClickHouse journal partition");
    }
    kafka::TopicPartition entry{std::string(pending->value(index, 0).value_or("")),
                                static_cast<std::int32_t>(*partition)};
    if (source_->assignment().contains(entry)) {
      recovery.push_back(std::move(entry));
    }
  }
  // Release the lease before deliver() so a journal pool of size one also works.
  lease = fail(ErrorCode::kCommonInternal, "journal lease released");
  for (const auto& partition : recovery) {
    auto result = co_await deliver(partition);
    if (!result) {
      co_return std::unexpected(result.error());
    }
  }
  if (!recovery.empty()) {
    co_return std::size_t{0};
  }
  if (bytes > options_.max_bytes) {
    co_return fail(ErrorCode::kCommonRateLimited, "source batch exceeds configured byte limit");
  }
  std::size_t processed = 0;
  for (const auto& [partition, records] : groups) {
    auto converted = converter_(records);
    if (!converted) {
      co_return std::unexpected(converted.error());
    }
    auto payload = pack(*converted, records, options_);
    if (!payload) {
      co_return std::unexpected(payload.error());
    }
    if (records.back()->offset() == std::numeric_limits<std::int64_t>::max()) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "Kafka source offset overflows");
    }
    const auto next = records.back()->offset() + 1;
    const auto name = source_->group() + "/" + options_.name;
    const auto token = detail::fingerprint(name + "/" + partition.topic + "/" + std::to_string(partition.partition)
                                           + "/" + std::to_string(records.front()->offset()) + "/"
                                           + std::to_string(next) + "/" + detail::fingerprint(*payload));
    auto store = co_await journal_->acquire();
    if (!store) {
      co_return std::unexpected(store.error());
    }
    auto parameters = key(name, partition);
    parameters.push_back(pg::Parameter::integer(records.front()->offset()));
    parameters.push_back(pg::Parameter::integer(next));
    parameters.push_back(pg::Parameter::text(token));
    parameters.push_back(pg::Parameter::bytes(std::move(*payload)));
    auto staged = co_await (*store)->query({.sql = "INSERT INTO psim_clickhouse.pending_batches (loader, topic, "
                                                   "partition, first_offset, next_offset, token, payload) "
                                                   "VALUES ($1,$2,$3,$4,$5,$6,$7) ON CONFLICT DO NOTHING",
                                            .parameters = std::move(parameters)});
    if (!staged) {
      co_return std::unexpected(staged.error());
    }
    store = fail(ErrorCode::kCommonInternal, "journal lease released");
    if (observer_) {
      observer_(LoadStage::kPrepared);
    }
    auto delivered = co_await deliver(partition);
    if (!delivered) {
      co_return std::unexpected(delivered.error());
    }
    processed += records.size();
    if (metrics_ != nullptr && records.back()->timestamp()) {
      const auto lag =
          std::chrono::system_clock::now() - records.back()->timestamp().value_or(std::chrono::system_clock::now());
      metrics_->lag(std::max(0.0, std::chrono::duration<double>(lag).count()));
    }
  }
  co_return processed;
}
}  // namespace psim::platform::clickhouse
