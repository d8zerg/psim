#include "projection.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/incident/v1/events.pb.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::api_gateway {
namespace p = platform;
namespace pg = p::pg;

boost::asio::awaitable<p::Result<void>> Projection::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto messages = co_await messages_.initialize(&**lease);
  if (!messages) {
    co_return messages;
  }
  co_return co_await pg::Migrator("access").apply(
      &**lease,
      {{.version = 1,
        .name = "minimal replica feed projection",
        .expand = {"CREATE TABLE access.incident_feed(replica text NOT NULL,tenant_id uuid NOT NULL,incident_id uuid "
                   "NOT NULL,version bigint NOT NULL,body jsonb NOT NULL,PRIMARY KEY(replica,tenant_id,incident_id))",
                   "CREATE TABLE access.feed_offsets(replica text NOT NULL,partition integer NOT NULL,next_offset "
                   "bigint NOT NULL,PRIMARY KEY(replica,partition))"}}});
}

boost::asio::awaitable<p::Result<void>> Projection::apply(incident::v1::IncidentEventRecord event,
                                                          const p::kafka::Message* message) {
  co_return co_await pg::in_transaction<void>(
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned closure
      pool_, [this, &event, &message](pg::Transaction* transaction) -> boost::asio::awaitable<p::Result<void>> {
        auto accepted = co_await messages_.accept(transaction, replica_,
                                                  event.envelope().tenant_id() + ":" + event.envelope().message_id(),
                                                  p::stream::sha256(event.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        auto& connection = transaction->connection();
        if (*accepted) {
          const auto* payload = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
              event, incident::v1::IncidentEventRecord::descriptor()->FindOneofByName("payload"));
          if (payload == nullptr) {
            co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "incident event payload required");
          }
          const auto& change = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(event, payload);
          const auto* field = change.GetDescriptor()->FindFieldByName("incident");
          if (field == nullptr) {
            co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "incident event snapshot required");
          }
          incident::v1::Incident value;
          value.CopyFrom(change.GetReflection()->GetMessage(change, field));
          auto updated = co_await connection.query(
              {.sql =
                   "INSERT INTO access.incident_feed(replica,tenant_id,incident_id,version,body) "
                   "VALUES($1,$2::uuid,$3::uuid,$4,$5::jsonb) ON CONFLICT(replica,tenant_id,incident_id) DO UPDATE SET "
                   "version=EXCLUDED.version,body=EXCLUDED.body WHERE access.incident_feed.version<EXCLUDED.version",
               .parameters = {pg::Parameter::text(replica_), pg::Parameter::text(event.envelope().tenant_id()),
                              pg::Parameter::text(value.incident_id()),
                              pg::Parameter::integer(static_cast<std::int64_t>(value.version())),
                              pg::Parameter::text(p::stream::json(value))}});
          if (!updated) {
            co_return std::unexpected(updated.error());
          }
        }
        auto positioned = co_await connection.query(
            {.sql = "INSERT INTO access.feed_offsets(replica,partition,next_offset) VALUES($1,$2,$3) ON "
                    "CONFLICT(replica,partition) DO UPDATE SET "
                    "next_offset=GREATEST(access.feed_offsets.next_offset,EXCLUDED.next_offset)",
             .parameters = {pg::Parameter::text(replica_), pg::Parameter::integer(message->partition()),
                            pg::Parameter::integer(message->offset() + 1)}});
        if (!positioned) {
          co_return std::unexpected(positioned.error());
        }
        co_return p::Result<void>{};
      });
}

boost::asio::awaitable<p::Result<Snapshot>> Projection::snapshot(std::string tenant) {
  co_return co_await pg::in_transaction<Snapshot>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned closure
      [this, &tenant](pg::Transaction* transaction) -> boost::asio::awaitable<p::Result<Snapshot>> {
        Snapshot result;
        auto rows = co_await transaction->connection().query(
            {.sql = "SELECT body::text FROM access.incident_feed WHERE replica=$1 AND tenant_id=$2::uuid ORDER BY "
                    "incident_id DESC LIMIT 1001",
             .parameters = {pg::Parameter::text(replica_), pg::Parameter::text(tenant)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() > 1000) {
          co_return p::fail(p::ErrorCode::kCommonUnavailable, "fixture feed limit reached");
        }
        for (std::size_t i = 0; i < rows->size(); ++i) {
          if (!google::protobuf::util::JsonStringToMessage(std::string(*rows->value(i, 0)), result.feed.add_incidents())
                   .ok()) {
            co_return p::fail(p::ErrorCode::kCommonInternal, "invalid stored projection");
          }
        }
        auto offsets = co_await transaction->connection().query(
            {.sql = "SELECT partition,next_offset FROM access.feed_offsets WHERE replica=$1",
             .parameters = {pg::Parameter::text(replica_)}});
        if (!offsets) {
          co_return std::unexpected(offsets.error());
        }
        for (std::size_t i = 0; i < offsets->size(); ++i) {
          result.offsets[std::to_string(*offsets->integer(i, 0))] = *offsets->integer(i, 1);
        }
        *result.feed.mutable_snapshot_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
        co_return result;
      },
      pg::Isolation::kRepeatableRead);
}
}  // namespace psim::api_gateway
