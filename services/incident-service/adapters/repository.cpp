#include "repository.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/incident/v1/incident.pb.h>
#include <psim/processing/v1/signal.pb.h>

#include <expected>
#include <string>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::incident_service {
namespace p = platform;
namespace pg = p::pg;

boost::asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto messages = co_await messages_.initialize(&**lease);
  if (!messages) {
    co_return messages;
  }
  co_return co_await pg::Migrator("incident")
      .apply(
          &**lease,
          {{.version = 1,
            .name = "minimal incident aggregate",
            .expand = {"CREATE TABLE incident.incidents (tenant_id uuid NOT NULL, incident_id uuid PRIMARY KEY, "
                       "grouping_key text NOT NULL, group_until_ns bigint NOT NULL, body jsonb NOT NULL)",
                       "CREATE INDEX incident_group ON incident.incidents(tenant_id, grouping_key, group_until_ns)"}}});
}

boost::asio::awaitable<p::Result<void>> Repository::apply(processing::v1::SignalRecord signal, Encode encode) {
  co_return co_await pg::in_transaction<void>(
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned closure
      pool_, [this, &signal, &encode](pg::Transaction* transaction) -> boost::asio::awaitable<p::Result<void>> {
        const auto& input = signal.envelope();
        const auto& value = signal.signal();
        auto accepted = co_await messages_.accept(transaction, "signals", input.tenant_id() + ":" + input.message_id(),
                                                  p::stream::sha256(input.tenant_id() + value.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        if (!*accepted) {
          co_return p::Result<void>{};
        }
        auto& connection = transaction->connection();
        auto locked = co_await connection.query(
            {.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
             .parameters = {pg::Parameter::text(input.tenant_id() + ":" + value.grouping_key())}});
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        const auto time = google::protobuf::util::TimeUtil::TimestampToNanoseconds(input.occurred_at());
        auto existing = co_await connection.query(
            {.sql = "SELECT body::text FROM incident.incidents WHERE tenant_id=$1::uuid AND grouping_key=$2 AND "
                    "group_until_ns>$3 ORDER BY group_until_ns DESC LIMIT 1 FOR UPDATE",
             .parameters = {pg::Parameter::text(input.tenant_id()), pg::Parameter::text(value.grouping_key()),
                            pg::Parameter::integer(time)}});
        if (!existing) {
          co_return std::unexpected(existing.error());
        }
        incident::v1::Incident incident;
        const bool created = existing->size() == 0;
        if (created) {
          incident.set_incident_id(p::stream::uuid7());
          incident.set_incident_type_id(value.incident_type_id());
          incident.set_site_id(value.site_id());
          incident.set_zone_id(value.zone_id());
          incident.set_state(incident::v1::INCIDENT_STATE_NEW);
          incident.set_priority(value.priority());
          incident.set_priority_source(incident::v1::PRIORITY_SOURCE_COMPUTED);
          incident.set_grouping_key(value.grouping_key());
          *incident.mutable_created_at() = input.occurred_at();
          incident.set_title(value.summary());
          *incident.mutable_group_until() =
              google::protobuf::util::TimeUtil::NanosecondsToTimestamp(time + 60000000000LL);
        } else if (!google::protobuf::util::JsonStringToMessage(std::string(*existing->value(0, 0)), &incident).ok()) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "stored incident is invalid");
        }
        if (incident.signal_ids_size() >= 1000) {
          co_return p::fail(p::ErrorCode::kCommonUnavailable, "fixture grouping capacity reached");
        }
        incident.add_signal_ids(input.message_id());
        incident.set_version(incident.version() + 1);
        auto saved = co_await connection.query(
            {.sql =
                 "INSERT INTO incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body) "
                 "VALUES($1::uuid,$2::uuid,$3,$4,$5::jsonb) ON CONFLICT(incident_id) DO UPDATE SET body=EXCLUDED.body",
             .parameters = {pg::Parameter::text(input.tenant_id()), pg::Parameter::text(incident.incident_id()),
                            pg::Parameter::text(incident.grouping_key()),
                            pg::Parameter::integer(
                                google::protobuf::util::TimeUtil::TimestampToNanoseconds(incident.group_until())),
                            pg::Parameter::text(p::stream::json(incident))}});
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        incident::v1::IncidentEventRecord event;
        auto* envelope = event.mutable_envelope();
        *envelope = input;
        envelope->set_message_id(p::stream::uuid7());
        envelope->set_causation_id(input.message_id());
        envelope->set_sequence(incident.version());
        envelope->mutable_actor()->set_id("incident-service");
        envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
        *envelope->mutable_produced_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
        if (created) {
          *event.mutable_incident_created()->mutable_incident() = incident;
          event.mutable_incident_created()->set_signal_id(input.message_id());
        } else {
          *event.mutable_signal_attached()->mutable_incident() = incident;
          event.mutable_signal_attached()->set_signal_id(input.message_id());
        }
        co_return co_await messages_.append(transaction, {envelope->message_id(), encode(event)});
      });
}
}  // namespace psim::incident_service
