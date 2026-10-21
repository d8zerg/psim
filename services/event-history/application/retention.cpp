#include "retention.hpp"

#include <clickhouse/columns/date.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "facts.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace psim::event_history {
namespace {
using Time = google::protobuf::util::TimeUtil;
using Param = p::pg::Parameter;
}  // namespace

boost::asio::awaitable<p::Result<void>> initialize_history(p::pg::Pool* pool) {
  auto lease = co_await pool->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto messages = co_await p::pg::MessageStore("history").initialize(&**lease);
  if (!messages) {
    co_return messages;
  }
  co_return co_await p::pg::Migrator("history").apply(
      &**lease,
      {{.version = 1,
        .name = "evidence retention requests",
        .expand = {"CREATE TABLE history.retentions(tenant uuid NOT NULL,event uuid NOT NULL,seconds bigint NOT "
                   "NULL,from_ms bigint NOT NULL,to_ms bigint NOT NULL,next_attempt timestamptz NOT NULL DEFAULT "
                   "now(),PRIMARY KEY(tenant,event))",
                   "CREATE INDEX history_retention_pending ON history.retentions(next_attempt,tenant,event)"}}});
}

boost::asio::awaitable<p::Result<void>> Retention::accept(processing::v1::SignalRecord record, std::int64_t seconds) {
  if (!valid_signal(record) || seconds < 1 || seconds > 315360000) {
    co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid evidence retention input");
  }
  if (record.signal().event_ids().empty()) {
    co_return p::Result<void>{};
  }
  nlohmann::json const ids = record.signal().event_ids();
  auto from = Time::TimestampToMilliseconds(record.signal().has_first_event_at() ? record.signal().first_event_at()
                                                                                 : record.envelope().occurred_at());
  auto to = Time::TimestampToMilliseconds(record.signal().has_last_event_at() ? record.signal().last_event_at()
                                                                              : record.envelope().occurred_at())
            + 1;
  if (!record.signal().has_first_event_at() || !record.signal().has_last_event_at()) {
    const auto occurred = Time::TimestampToMilliseconds(record.envelope().occurred_at());
    const auto first = from;
    from = std::max<std::int64_t>(0, std::min(from, occurred) - (seconds * 1000));
    to = std::max({to, first + 1, occurred + 1});
  }
  auto lease = co_await journal_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows =
      co_await (*lease)->query({.sql = R"sql(INSERT INTO history.retentions(tenant,event,seconds,from_ms,to_ms)
SELECT $1::uuid,value::uuid,$2,$3,$4 FROM (SELECT DISTINCT jsonb_array_elements_text($5::jsonb) AS value) AS ids
ON CONFLICT(tenant,event) DO UPDATE SET seconds=GREATEST(history.retentions.seconds,EXCLUDED.seconds),
from_ms=LEAST(history.retentions.from_ms,EXCLUDED.from_ms),to_ms=GREATEST(history.retentions.to_ms,EXCLUDED.to_ms),next_attempt=now())sql",
                                .parameters = {Param::text(record.envelope().tenant_id()), Param::integer(seconds),
                                               Param::integer(from), Param::integer(to), Param::text(ids.dump())}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return p::Result<void>{};
}

boost::asio::awaitable<p::Result<void>> Retention::tick() {
  co_return co_await p::pg::in_transaction<void>(
      journal_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this](p::pg::Transaction* tx) -> boost::asio::awaitable<p::Result<void>> {
        auto first =
            co_await tx->connection().query({.sql = "SELECT tenant::text FROM history.retentions WHERE "
                                                    "next_attempt<=now() ORDER BY next_attempt,tenant,event LIMIT 1"});
        if (!first) {
          co_return std::unexpected(first.error());
        }
        if (first->size() == 0) {
          co_return p::Result<void>{};
        }
        const std::string tenant(first->value(0, 0).value_or(""));
        auto jobs = co_await tx->connection().query(
            {.sql = "SELECT event::text,seconds,from_ms,to_ms FROM history.retentions WHERE tenant=$1::uuid AND "
                    "next_attempt<=now() ORDER BY event LIMIT $2 FOR UPDATE SKIP LOCKED",
             .parameters = {Param::text(tenant), Param::integer(static_cast<std::int64_t>(batch_size_))}});
        if (!jobs) {
          co_return std::unexpected(jobs.error());
        }
        if (jobs->size() == 0) {
          co_return p::Result<void>{};
        }
        std::map<std::string, std::int64_t> durations;
        std::int64_t from = 10000000000000LL;
        std::int64_t to = 0;
        nlohmann::json ids = nlohmann::json::array();
        for (std::size_t i = 0; i < jobs->size(); ++i) {
          const std::string id(jobs->value(i, 0).value_or(""));
          ids.push_back(id);
          durations[id] = *jobs->integer(i, 1);
          from = std::min(from, *jobs->integer(i, 2));
          to = std::max(to, *jobs->integer(i, 3));
        }
        auto rows = co_await destination_->read(
            {.statement =
                 R"sql(SELECT event_id,argMax(payload,expires_at) AS payload,toUnixTimestamp64Milli(max(expires_at)) AS expiration
FROM (SELECT tenant_id,event_id,payload,occurred_at,expires_at FROM events FINAL UNION ALL SELECT tenant_id,event_id,payload,occurred_at,expires_at FROM retained_events FINAL)
WHERE tenant_id={psim_tenant:String} AND occurred_at>=fromUnixTimestamp64Milli({psim_from:Int64}) AND occurred_at<fromUnixTimestamp64Milli({psim_to:Int64})
AND expires_at>now64(3) AND has(JSONExtract({ids:String},'Array(String)'),event_id) GROUP BY event_id LIMIT 128)sql",
             .tenant = tenant,
             .from_ms = from,
             .to_ms = to,
             .parameters = {{"ids", ids.dump()}}});
        if (!rows) {
          if (rows.error().code() == p::ErrorCode::kCommonRateLimited) {
            batch_size_ = std::max<std::size_t>(1, batch_size_ / 2);
          }
          co_return std::unexpected(rows.error());
        }
        std::vector<processing::v1::EventRecord> records;
        std::vector<std::int64_t> expirations;
        std::set<std::string> found;
        for (const auto& block : rows->blocks) {
          for (std::size_t i = 0; i < block.GetRowCount(); ++i) {
            const std::string id(block[0]->AsStrict<::clickhouse::ColumnString>()->At(i));
            processing::v1::EventRecord event;
            if (!event.ParseFromString(std::string(block[1]->AsStrict<::clickhouse::ColumnString>()->At(i)))) {
              co_return p::fail(p::ErrorCode::kCommonInternal, "invalid retained event payload");
            }
            event.mutable_event()->set_significant(true);
            found.insert(id);
            expirations.push_back(
                std::max(block[2]->AsStrict<::clickhouse::ColumnInt64>()->At(i),
                         Time::TimestampToMilliseconds(event.envelope().occurred_at()) + durations.at(id) * 1000));
            records.push_back(std::move(event));
          }
        }
        if (!records.empty()) {
          auto converted = Events{}.convert_records(records);
          if (!converted) {
            co_return std::unexpected(converted.error());
          }
          auto expiration = converted->block[13]->AsStrict<::clickhouse::ColumnDateTime64>();
          expiration->Clear();
          auto version = std::make_shared<::clickhouse::ColumnUInt64>();
          for (const auto ms : expirations) {
            expiration->Append(ms);
            version->Append(static_cast<std::uint64_t>(ms));
          }
          converted->block.AppendColumn("retention_version", version);
          const nlohmann::json versions = expirations;
          auto inserted = co_await destination_->insert("retained_events", converted->block,
                                                        p::stream::sha256(tenant + ids.dump() + versions.dump()));
          if (!inserted) {
            if (inserted.error().code() == p::ErrorCode::kCommonRateLimited) {
              batch_size_ = std::max<std::size_t>(1, batch_size_ / 2);
            }
            co_return inserted;
          }
        }
        nlohmann::json const done = found;
        auto removed = co_await tx->connection().query(
            {.sql = "DELETE FROM history.retentions WHERE tenant=$1::uuid AND (event::text IN(SELECT "
                    "jsonb_array_elements_text($2::jsonb)) OR to_ms+seconds*1000<(extract(epoch FROM "
                    "clock_timestamp())*1000)::bigint)",
             .parameters = {Param::text(tenant), Param::text(done.dump())}});
        if (!removed) {
          co_return std::unexpected(removed.error());
        }
        auto delayed = co_await tx->connection().query(
            {.sql = "UPDATE history.retentions SET next_attempt=now()+interval '2 seconds' WHERE tenant=$1::uuid AND "
                    "event::text IN(SELECT jsonb_array_elements_text($2::jsonb))",
             .parameters = {Param::text(tenant), Param::text(ids.dump())}});
        if (!delayed) {
          co_return std::unexpected(delayed.error());
        }
        co_return p::Result<void>{};
      });
}
}  // namespace psim::event_history
