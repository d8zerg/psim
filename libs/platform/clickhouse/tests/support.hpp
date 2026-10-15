#pragma once
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "../src/detail.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/processing/v1/event.pb.h"

namespace psim::platform::clickhouse::testing {
template <typename T>
T run(asio::awaitable<T> operation) {
  asio::io_context context;
  auto result = asio::co_spawn(context, std::move(operation), asio::use_future);
  context.run();
  return result.get();
}

template <typename T>
T take(Result<T> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

inline void check(Result<void> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
}

inline std::string environment(const char* name) {
  const char* value =
      std::getenv(name);  // NOLINT(concurrency-mt-unsafe): test endpoints are fixed before threads start
  if (value == nullptr) {
    throw std::runtime_error(std::string("missing component endpoint: ") + name);
  }
  return value;
}

inline PoolOptions options(std::string user = "psim", const std::string& password = "admin") {
  return {.endpoints = {{environment("PSIM_TEST_CLICKHOUSE"), 9440}},
          .user = std::move(user),
          .password_file = "/repo/.cache/clickhouse-tls/" + password + ".password",
          .tls = {.ca_file = "/repo/.cache/clickhouse-tls/ca.pem",
                  .certificate_file = "/repo/.cache/clickhouse-tls/client.pem",
                  .private_key_file = "/repo/.cache/clickhouse-tls/client.key"}};
}

inline pg::PoolOptions journal_options() {
  return {.connection = {.host = environment("PSIM_TEST_POSTGRES"),
                         .password = "psim-dev-psim",
                         .properties = {{"sslmode", "disable"}}},
          .size = 1};
}

inline ::clickhouse::Block block(std::size_t count = 2, const std::string& tenant = "tenant-a", std::size_t start = 0) {
  auto tenants = std::make_shared<::clickhouse::ColumnString>();
  auto ids = std::make_shared<::clickhouse::ColumnString>();
  auto times = std::make_shared<::clickhouse::ColumnDateTime64>(3);
  auto values = std::make_shared<::clickhouse::ColumnString>();
  for (std::size_t index = start; index < start + count; ++index) {
    tenants->Append(tenant);
    ids->Append(std::to_string(index));
    times->Append(1000);
    values->Append(std::string("a\0b", 3));
  }
  ::clickhouse::Block data;
  data.AppendColumn("tenant_id", tenants);
  data.AppendColumn("event_id", ids);
  data.AppendColumn("occurred_at", times);
  data.AppendColumn("value", values);
  return data;
}

inline Select select(std::string table, bool final = true) {
  return {.table = std::move(table),
          .columns = {"tenant_id", "event_id", "occurred_at", "value"},
          .tenant = "tenant-a",
          .from_ms = 0,
          .to_ms = 2000,
          .limit = 100000,
          .final = final};
}

inline asio::awaitable<void> create(Pool* pool, std::string table) {
  check(
      co_await pool->execute_ddl(
          // psim-arch: allow-sql (validated column identifiers or generated test table names only; values are bound)
          "CREATE TABLE IF NOT EXISTS " + table
          + " ON CLUSTER psim_clickhouse "
            "(tenant_id String, event_id String, occurred_at DateTime64(3), value String) "
            "ENGINE=ReplicatedReplacingMergeTree('/clickhouse/ch-component/{shard}/{database}/"
          + table
          + "', '{replica}') "
            "ORDER BY (tenant_id, occurred_at, event_id) SETTINGS replicated_deduplication_window=100000, "
            "replicated_deduplication_window_seconds=604800"));
}

inline asio::awaitable<void> drop(Pool* pool, std::string table) {
  // psim-arch: allow-sql (validated column identifiers or generated test table names only; values are bound)
  check(co_await pool->execute_ddl("DROP TABLE IF EXISTS " + table + " ON CLUSTER psim_clickhouse SYNC"));
}

inline Result<Converted> convert(std::span<const kafka::Message* const> messages) {
  const kafka::ProtobufSerde<psim::processing::v1::EventRecord> serde(1);
  auto tenants = std::make_shared<::clickhouse::ColumnString>();
  auto ids = std::make_shared<::clickhouse::ColumnString>();
  auto times = std::make_shared<::clickhouse::ColumnDateTime64>(3);
  auto values = std::make_shared<::clickhouse::ColumnString>();
  Converted result;
  for (std::size_t index = 0; index < messages.size(); ++index) {
    auto record = serde.deserialize(messages[index]->value());
    if (!record || !record->has_event() || record->envelope().tenant_id().empty()
        || record->envelope().message_id().empty()) {
      result.rejected.push_back({index});
      continue;
    }
    tenants->Append(record->envelope().tenant_id());
    ids->Append(record->envelope().message_id());
    times->Append((record->envelope().occurred_at().seconds() * 1000)
                  + (record->envelope().occurred_at().nanos() / 1000000));
    values->Append(record->event().raw_code());
  }
  result.block.AppendColumn("tenant_id", tenants);
  result.block.AppendColumn("event_id", ids);
  result.block.AppendColumn("occurred_at", times);
  result.block.AppendColumn("value", values);
  return result;
}
}  // namespace psim::platform::clickhouse::testing
