// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines): in_transaction owns each closure until awaited.
#include "repository.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/audit/v1/audit.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/common/v1/envelope.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/response/v1/events.pb.h"
#include "psim/response/v1/notification.pb.h"
#include "psim/response/v1/plan.pb.h"
#include "transport.hpp"

namespace psim::notification_service {
namespace {
using Param = p::pg::Parameter;
using Code = p::ErrorCode;
using Time = google::protobuf::util::TimeUtil;

std::int64_t now() {
  return Time::TimestampToMilliseconds(Time::GetCurrentTime());
}

Json json(const google::protobuf::Message& value) {
  return Json::parse(p::stream::json(value));
}

template <typename T>
T parse(const Json& value) {
  T result;
  if (!google::protobuf::util::JsonStringToMessage(value.dump(), &result).ok()) {
    throw std::invalid_argument("fields");
  }
  return result;
}

void envelope(common::v1::Envelope* value, const std::string& tenant, std::uint64_t version = 1) {
  value->set_message_id(p::stream::uuid7());
  value->set_tenant_id(tenant);
  value->set_schema_version("1");
  value->set_sequence(version);
  value->set_correlation_id(value->message_id());
  *value->mutable_occurred_at() = Time::GetCurrentTime();
  *value->mutable_produced_at() = value->occurred_at();
  value->mutable_actor()->set_id("notification-service");
  value->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
}

asio::awaitable<p::Result<void>> execute(p::pg::Transaction* tx, std::string sql, std::vector<Param> args = {}) {
  auto result = co_await tx->connection().query({.sql = std::move(sql), .parameters = std::move(args)});
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return p::Result<void>{};
}

Json payload(const response::v1::Notification& value) {
  return Json{{"notificationId", value.notification_id()},
              {"recipientUserId", value.recipient_user_id()},
              {"incidentId", value.incident_id()},
              {"template", value.template_()},
              {"data", json(value).value("data", Json::object())},
              {"aggregatedNotificationIds", json(value).value("aggregatedNotificationIds", Json::array())}};
}
}  // namespace

asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto initialized = co_await messages_.initialize(&**lease);
  if (!initialized) {
    co_return initialized;
  }
  co_return co_await p::pg::Migrator("notification")
      .apply(
          &**lease,
          {{.version = 1,
            .name = "durable notifications and delivery receipts",
            .expand = {
                R"sql(CREATE TABLE notification.contacts(tenant uuid NOT NULL,id uuid NOT NULL,site uuid NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id,site)))sql",
                (R"sql(CREATE TABLE notification.notifications(tenant uuid NOT NULL,id uuid NOT NULL,recipient uuid NOT NULL,site uuid NOT NULL,channel integer NOT NULL,body jsonb NOT NULL,status text NOT NULL,destination text NOT NULL,due_ms bigint NOT NULL,PRIMARY KEY(tenant,id)))sql"),
                (R"sql(CREATE INDEX notifications_due ON notification.notifications(due_ms) WHERE status IN ('pending','sending'))sql"),
                R"sql(CREATE INDEX notifications_owner ON notification.notifications(tenant,recipient,channel,id))sql",
                (R"sql(CREATE TABLE notification.rates(tenant uuid NOT NULL,recipient uuid NOT NULL,site uuid NOT NULL,channel integer NOT NULL,bucket bigint NOT NULL,count integer NOT NULL,PRIMARY KEY(tenant,recipient,site,channel,bucket)))sql"),
                R"sql(CREATE TABLE notification.requests(tenant uuid NOT NULL,actor text NOT NULL,key uuid NOT NULL,fingerprint text NOT NULL,result jsonb NOT NULL,PRIMARY KEY(tenant,actor,key)))sql"}}});
}

asio::awaitable<p::Result<void>> Repository::audit(p::pg::Transaction* tx, std::string tenant, std::string actor,
                                                   std::string action, std::string id, std::string site, bool denied,
                                                   Json details, std::string cause) {
  audit::v1::AuditEventRecord event;
  envelope(event.mutable_envelope(), tenant);
  event.mutable_envelope()->set_causation_id(std::move(cause));
  auto* entry = event.mutable_audit_entry();
  entry->mutable_actor()->set_id(actor);
  entry->mutable_actor()->set_kind(actor == "notification-service" ? common::v1::ACTOR_KIND_SERVICE
                                                                   : common::v1::ACTOR_KIND_USER);
  entry->set_action(denied ? "access.denied" : action);
  if (denied) {
    details["requested_action"] = std::move(action);
  }
  entry->set_object_type("notification");
  entry->set_object_id(id);
  entry->set_site_id(site);
  entry->set_outcome(audit::v1::AUDIT_OUTCOME_SUCCESS);
  if (denied) {
    entry->set_outcome(audit::v1::AUDIT_OUTCOME_DENIED);
  }
  if (details.value("state", std::string{}) == "NOTIFICATION_STATE_FAILED") {
    entry->set_outcome(audit::v1::AUDIT_OUTCOME_FAILED);
  }
  for (const auto& item : details.items()) {
    (*entry->mutable_details())[item.key()] = item.value().get<std::string>();
  }
  co_return co_await messages_.append(tx, {event.envelope().message_id(), encode_("psim.audit.v1", tenant, event)});
}

asio::awaitable<p::Result<void>> Repository::save(p::pg::Transaction* tx, std::string tenant, std::string site,
                                                  response::v1::Notification value, std::string status,
                                                  std::string destination, std::int64_t due) {
  auto stored = co_await execute(
      tx,
      ("INSERT INTO notification.notifications "
       "VALUES($1::uuid,$2::uuid,$3::uuid,$4::uuid,$5::integer,$6::jsonb,$7,$8,$9::bigint) ON CONFLICT(tenant,id) DO "
       "UPDATE SET body=EXCLUDED.body,status=EXCLUDED.status,due_ms=EXCLUDED.due_ms"),
      {Param::text(tenant), Param::text(value.notification_id()), Param::text(value.recipient_user_id()),
       Param::text(site), Param::text(std::to_string(value.channel())), Param::text(json(value).dump()),
       Param::text(status), Param::text(destination), Param::text(std::to_string(due))});
  if (!stored) {
    co_return stored;
  }
  response::v1::ResponseEventRecord event;
  envelope(event.mutable_envelope(), tenant, value.version());
  *event.mutable_notification_updated()->mutable_notification() = value;
  auto published = co_await messages_.append(
      tx, {event.envelope().message_id(),
           encode_("psim.response.events.v1",
                   value.incident_id().empty() ? value.notification_id() : value.incident_id(), event)});
  if (!published) {
    co_return published;
  }
  co_return co_await audit(tx, tenant, "notification-service", "notification.state." + std::to_string(value.state()),
                           value.notification_id(), site, false,
                           Json{{"state", response::v1::NotificationState_Name(value.state())},
                                {"channel", response::v1::NotificationChannel_Name(value.channel())},
                                {"attempts", std::to_string(value.attempts())},
                                {"version", std::to_string(value.version())},
                                {"receipt_id", value.receipt_id()},
                                {"failure_code", value.failure_code()}},
                           event.envelope().message_id());
}

asio::awaitable<p::Result<void>> Repository::apply(response::v1::ResponseEventRecord input) {
  if (!input.has_notification_requested()) {
    co_return p::Result<void>{};
  }
  const auto& request = input.notification_requested();
  const auto tenant = input.envelope().tenant_id();
  if (input.envelope().actor().kind() != common::v1::ACTOR_KIND_SERVICE
      || input.envelope().actor().id() != "response-engine" || !p::stream::uuid(tenant)
      || !p::stream::uuid(request.request_id()) || !p::stream::uuid(request.site_id())
      || (!request.incident_id().empty() && !p::stream::uuid(request.incident_id()))
      || request.params().recipients().empty() || request.params().recipients_size() > 100
      || request.params().channels().empty() || request.params().template_().size() > 8000) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid notification request");
  }
  std::set<int> channels;
  for (const int channel : request.params().channels()) {
    if (channel < 1 || channel > 3) {
      co_return p::fail(Code::kProcessingPayloadInvalid, "invalid notification channel");
    }
    channels.insert(channel);
  }
  co_return co_await p::pg::in_transaction<
      void>(pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
    auto accepted = co_await messages_.accept(tx, "requests", tenant + ":" + request.request_id(),
                                              p::stream::sha256(request.SerializeAsString()));
    if (!accepted) {
      co_return std::unexpected(accepted.error());
    }
    if (!*accepted) {
      co_return p::Result<void>{};
    }
    std::set<std::string> recipients;
    for (const auto& target : request.params().recipients()) {
      if (!target.user_id().empty()) {
        if (!p::stream::uuid(target.user_id())) {
          co_return p::fail(Code::kProcessingPayloadInvalid, "invalid recipient");
        }
        recipients.insert(target.user_id());
        continue;
      }
      if (target.role() != "supervisor" && target.role() != "operator") {
        co_return p::fail(Code::kProcessingPayloadInvalid, "unresolved recipient");
      }
      auto rows =
          co_await tx->connection().query({.sql = "SELECT id::text FROM notification.contacts WHERE tenant=$1::uuid "
                                                  "AND site=$2::uuid ORDER BY id LIMIT 101",
                                           .parameters = {Param::text(tenant), Param::text(request.site_id())}});
      if (!rows) {
        co_return std::unexpected(rows.error());
      }
      if (rows->size() > 100) {
        co_return p::fail(Code::kCommonUnavailable, "recipient fan-out exceeds limit");
      }
      for (std::size_t index = 0; index < rows->size(); ++index) {
        const auto user = std::string(rows->value(index, 0).value_or(""));
        auto person = co_await lookup_(user);
        if (!person && person.error().code() == Code::kCommonUnavailable) {
          co_return std::unexpected(person.error());
        }
        if (person && person->roles.contains(target.role())
            && p::authz::scoped(*person, {tenant, request.site_id(), request.zone_id()})) {
          recipients.insert(user);
        }
      }
    }
    if (recipients.empty()) {
      co_return co_await audit(tx, tenant, "notification-service", "notification.no_recipients", request.request_id(),
                               request.site_id());
    }
    if (recipients.size() > 100) {
      co_return p::fail(Code::kCommonUnavailable, "recipient fan-out exceeds limit");
    }
    // Deterministic order avoids lock inversions when two requests share recipients.
    for (const auto& user : recipients) {
      auto person = co_await lookup_(user);
      if (!person && person.error().code() == Code::kCommonUnavailable) {
        co_return std::unexpected(person.error());
      }
      const bool eligible = person && p::authz::scoped(*person, {tenant, request.site_id(), request.zone_id()})
                            && (person->roles.contains("operator") || person->roles.contains("supervisor")
                                || person->roles.contains("admin"));
      auto contact = co_await tx->connection().query(
          {.sql =
               "SELECT body::text FROM notification.contacts WHERE tenant=$1::uuid AND id=$2::uuid AND site=$3::uuid",
           .parameters = {Param::text(tenant), Param::text(user), Param::text(request.site_id())}});
      if (!contact) {
        co_return std::unexpected(contact.error());
      }
      const auto destinations = contact->size() ? Json::parse(contact->value(0, 0).value_or("")) : Json::object();
      for (const int channel : channels) {
        const auto id = p::stream::uuid5(request.request_id(), {user, std::to_string(channel)});
        auto recipient_lock = tenant;
        recipient_lock.append(":").append(user).append(":").append(std::to_string(channel));
        auto lock =
            co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))", {Param::text(recipient_lock)});
        if (!lock) {
          co_return lock;
        }
        response::v1::Notification value;
        value.set_notification_id(id);
        value.set_request_id(request.request_id());
        value.set_recipient_user_id(user);
        value.set_incident_id(request.incident_id());
        value.set_channel(static_cast<response::v1::NotificationChannel>(channel));
        value.set_template_(request.params().template_());
        value.set_version(1);
        *value.mutable_created_at() = Time::GetCurrentTime();
        value.set_state(response::v1::NOTIFICATION_STATE_PENDING);
        (*value.mutable_data())["siteId"] = request.site_id();
        (*value.mutable_data())["zoneId"] = request.zone_id();
        std::string destination;
        if (channel == 2) {
          destination = destinations.value("email", std::string{});
        }
        if (channel == 3) {
          destination = destinations.value("webhookUrl", std::string{});
        }
        if (!eligible || (channel == 2 && !valid_email(destination))
            || (channel == 3 && !transport_->validate_webhook(destination))) {
          value.set_state(response::v1::NOTIFICATION_STATE_FAILED);
          value.set_failure_code("recipient_unavailable");
          auto stored = co_await save(tx, tenant, request.site_id(), value, "complete", destination, 0);
          if (!stored) {
            co_return stored;
          }
          continue;
        }
        const auto bucket = now() / policy_.window_ms;
        auto rate = co_await tx->connection().query(
            {.sql = ("INSERT INTO notification.rates VALUES($1::uuid,$2::uuid,$3::uuid,$4::integer,$5::bigint,1) ON "
                     "CONFLICT(tenant,recipient,site,channel,bucket) DO UPDATE SET count=notification.rates.count+1 "
                     "RETURNING count"),
             .parameters = {Param::text(tenant), Param::text(user), Param::text(request.site_id()),
                            Param::text(std::to_string(channel)), Param::text(std::to_string(bucket))}});
        if (!rate) {
          co_return std::unexpected(rate.error());
        }
        const auto count = std::stoi(std::string(rate->value(0, 0).value_or("")));
        if (count > policy_.rate_limit) {
          auto summary_id =
              p::stream::uuid5(user, {tenant, request.site_id(), std::to_string(channel), std::to_string(bucket),
                                      std::to_string((count - policy_.rate_limit - 1) / 500)});
          auto previous =
              co_await tx->connection().query({.sql = "SELECT body::text,status FROM notification.notifications WHERE "
                                                      "tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
                                               .parameters = {Param::text(tenant), Param::text(summary_id)}});
          if (!previous) {
            co_return std::unexpected(previous.error());
          }
          const bool append = previous->size() && previous->value(0, 1) == "pending";
          if (previous->size() && !append) {
            summary_id = p::stream::uuid5(id, {"late-summary"});
          }
          auto summary =
              append ? parse<response::v1::Notification>(Json::parse(previous->value(0, 0).value_or(""))) : value;
          if (!append) {
            summary.set_notification_id(summary_id);
            summary.clear_incident_id();
            (*summary.mutable_data())["firstTemplate"] = value.template_();
            (*summary.mutable_data())["firstIncidentId"] = value.incident_id();
            summary.clear_request_id();
          } else {
            summary.set_version(summary.version() + 1);
          }
          summary.add_aggregated_notification_ids(id);
          (*summary.mutable_data())["count"] = std::to_string(summary.aggregated_notification_ids_size());
          summary.set_template_("Notification summary: " + std::to_string(summary.aggregated_notification_ids_size()));
          auto stored = co_await save(tx, tenant, request.site_id(), summary, "pending", destination,
                                      (bucket + 1) * policy_.window_ms);
          if (!stored) {
            co_return stored;
          }
          value.set_state(response::v1::NOTIFICATION_STATE_AGGREGATED);
          (*value.mutable_data())["summaryId"] = summary_id;
        }
        auto stored = co_await save(
            tx, tenant, request.site_id(), value,
            value.state() == response::v1::NOTIFICATION_STATE_AGGREGATED ? "complete" : "pending", destination, now());
        if (!stored) {
          co_return stored;
        }
      }
    }
    co_return p::Result<void>{};
  });
}

asio::awaitable<p::Result<void>> Repository::tick() {
  for (int index = 0; index < 32; ++index) {
    auto claimed = co_await p::pg::in_transaction<Json>(
        pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
          auto rows = co_await tx->connection().query(
              {.sql =
                   "SELECT tenant::text,site::text,body::text,destination,status FROM notification.notifications WHERE "
                   "status IN ('pending','sending') AND due_ms<=extract(epoch FROM clock_timestamp())*1000 ORDER BY "
                   "due_ms,id LIMIT 1 FOR UPDATE SKIP LOCKED"});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          if (!rows->size()) {
            co_return Json();
          }
          const auto tenant = std::string(rows->value(0, 0).value_or(""));
          const auto site = std::string(rows->value(0, 1).value_or(""));
          const auto destination = std::string(rows->value(0, 3).value_or(""));
          auto value = parse<response::v1::Notification>(Json::parse(rows->value(0, 2).value_or("")));
          const auto content = payload(value).dump();
          const bool reconciling =
              rows->value(0, 4) == "sending" && value.attempts() >= static_cast<unsigned int>(policy_.attempts);
          if (value.attempts() >= static_cast<unsigned int>(policy_.attempts) && !reconciling) {
            value.set_state(response::v1::NOTIFICATION_STATE_FAILED);
            value.set_failure_code("attempts_exhausted");
            value.set_version(value.version() + 1);
            auto saved = co_await save(tx, tenant, site, value, "complete", destination, 0);
            if (!saved) {
              co_return std::unexpected(saved.error());
            }
            co_return Json{{"skip", true}};
          }
          if (!reconciling) {
            value.set_attempts(value.attempts() + 1);
          }
          value.set_version(value.version() + 1);
          if (value.channel() == response::v1::NOTIFICATION_CHANNEL_IN_APP) {
            value.set_state(response::v1::NOTIFICATION_STATE_DELIVERED);
            value.set_receipt_id(value.notification_id());
            *value.mutable_delivered_at() = Time::GetCurrentTime();
          } else if (value.channel() == response::v1::NOTIFICATION_CHANNEL_EMAIL) {
            value.set_state(response::v1::NOTIFICATION_STATE_UNKNOWN);
            value.set_failure_code("acceptance_unknown");
          }
          std::string status = "sending";
          if (value.channel() == 1) {
            status = "complete";
          }
          if (value.channel() == 2) {
            status = "unknown";
          }
          auto saved = co_await save(tx, tenant, site, value, status, destination, now() + 60000);
          if (!saved) {
            co_return std::unexpected(saved.error());
          }
          co_return Json{{"tenant", tenant},           {"site", site},       {"body", json(value)},
                         {"destination", destination}, {"payload", content}, {"reconciling", reconciling}};
        });
    if (!claimed) {
      co_return std::unexpected(claimed.error());
    }
    if (claimed->is_null()) {
      break;
    }
    if (claimed->contains("skip")) {
      continue;
    }
    auto value = parse<response::v1::Notification>(claimed->at("body"));
    if (value.channel() == 1) {
      continue;
    }
    Receipt receipt{Outcome::kFailed};
    auto person = co_await lookup_(value.recipient_user_id());
    if (!person && person.error().code() == Code::kCommonUnavailable) {
      receipt.outcome = Outcome::kRetry;
    } else if (person
               && (person->roles.contains("operator") || person->roles.contains("supervisor")
                   || person->roles.contains("admin"))
               && p::authz::scoped(*person, {claimed->at("tenant").get<std::string>(),
                                             claimed->at("site").get<std::string>(), value.data().at("zoneId")})) {
      auto sent = co_await transport_->send(value.channel(), value.notification_id(),
                                            claimed->at("destination").get<std::string>(),
                                            claimed->at("payload").get<std::string>());
      receipt = sent ? *sent : Receipt{value.channel() == 2 ? Outcome::kUnknown : Outcome::kRetry};
    }
    auto completed = co_await p::pg::in_transaction<void>(
        pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          const auto tenant = claimed->at("tenant").get<std::string>();
          const auto site = claimed->at("site").get<std::string>();
          auto rows = co_await tx->connection().query(
              {.sql =
                   "SELECT body::text FROM notification.notifications WHERE tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
               .parameters = {Param::text(tenant), Param::text(value.notification_id())}});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          auto current = parse<response::v1::Notification>(Json::parse(rows->value(0, 0).value_or("")));
          if (current.version() != value.version()) {
            co_return p::Result<void>{};
          }
          value.set_version(value.version() + 1);
          value.clear_failure_code();
          std::string status = "complete";
          if (receipt.outcome == Outcome::kAccepted) {
            value.set_state(value.channel() == 2 ? response::v1::NOTIFICATION_STATE_SENT
                                                 : response::v1::NOTIFICATION_STATE_DELIVERED);
            value.set_receipt_id(receipt.id);
            if (value.channel() != 2) {
              *value.mutable_delivered_at() = Time::GetCurrentTime();
            }
          } else if (receipt.outcome == Outcome::kUnknown || claimed->value("reconciling", false)
                     || (receipt.outcome == Outcome::kRetryUnknown
                         && value.attempts() >= static_cast<unsigned int>(policy_.attempts))) {
            value.set_state(response::v1::NOTIFICATION_STATE_UNKNOWN);
            value.set_failure_code("acceptance_unknown");
            status = "unknown";
          } else if (receipt.outcome == Outcome::kFailed
                     || value.attempts() >= static_cast<unsigned int>(policy_.attempts)) {
            value.set_state(response::v1::NOTIFICATION_STATE_FAILED);
            value.set_failure_code(receipt.outcome == Outcome::kFailed ? "delivery_rejected" : "attempts_exhausted");
          } else {
            value.set_state(response::v1::NOTIFICATION_STATE_PENDING);
            status = "pending";
          }
          co_return co_await save(tx, tenant, site, value, status, claimed->at("destination").get<std::string>(),
                                  now() + (policy_.retry_ms * (1LL << std::min(value.attempts(), 6U))));
        });
    if (!completed) {
      co_return completed;
    }
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto cleaned =
      co_await (**lease).query({.sql = "DELETE FROM notification.rates WHERE bucket<$1::bigint",
                                .parameters = {Param::text(std::to_string((now() / policy_.window_ms) - 2))}});
  if (!cleaned) {
    co_return std::unexpected(cleaned.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<Json>> Repository::request(p::security::Principal principal, std::string operation, Json body,
                                                     std::string key, std::optional<std::uint64_t> expected) {
  const bool read = operation == "list";
  const bool contact = operation == "contact";
  const auto site = contact ? body.value("siteId", std::string{}) : std::string{};
  std::string action = "notification.read";
  if (contact) {
    action = "notification.contact.write";
  }
  if (operation == "resolve") {
    action = "notification.delivery.resolve";
  }
  auto access = p::authz::decide(principal, action, {principal.tenant, site}, std::chrono::system_clock::now());
  // Personal notification operations check each persisted resource below.
  if (!contact) {
    access = p::authz::decide_somewhere(principal, action, std::chrono::system_clock::now());
  }
  if (!access) {
    auto recorded = co_await p::pg::in_transaction<void>(pool_, [&, this](p::pg::Transaction* tx) {
      return audit(tx, principal.tenant, principal.subject, "notification." + operation,
                   body.value("notificationId", std::string{}), site, true);
    });
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
    co_return std::unexpected(access.error());
  }
  if (!read && !p::stream::uuid(key)) {
    co_return p::fail(Code::kCommonInvalidArgument, "idempotency key required");
  }
  co_return co_await p::pg::in_transaction<
      Json>(pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
    const auto fingerprint = p::stream::sha256(operation + body.dump() + std::to_string(expected.value_or(0)));
    if (!read) {
      auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                   {Param::text(principal.tenant + ":" + principal.subject + ":" + key)});
      if (!lock) {
        co_return std::unexpected(lock.error());
      }
      auto previous = co_await tx->connection().query(
          {.sql = "SELECT fingerprint,result::text FROM notification.requests WHERE tenant=$1::uuid AND actor=$2 AND "
                  "key=$3::uuid",
           .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key)}});
      if (!previous) {
        co_return std::unexpected(previous.error());
      }
      if (previous->size()) {
        if (previous->value(0, 0) != fingerprint) {
          co_return p::fail(Code::kCommonIdempotencyKeyReused, "request key reused");
        }
        auto output = Json::parse(previous->value(0, 1).value_or(""));
        const auto& value = output.at(contact ? "contact" : "notification");
        const auto scope =
            contact ? value.at("siteId").get<std::string>() : value.at("data").at("siteId").get<std::string>();
        const auto zone = contact ? std::string{} : value.at("data").value("zoneId", std::string{});
        if (!p::authz::scoped(principal, {principal.tenant, scope, zone})) {
          co_return p::fail(Code::kAuthScopeDenied, "notification outside scope");
        }
        co_return output;
      }
    }
    Json output;
    if (read) {
      int size = body.value("pageSize", 100);
      if (!size) {
        size = 100;
      }
      const auto cursor = body.value("pageToken", std::string{});
      if (size < 1 || size > 1000 || (!cursor.empty() && !p::stream::uuid(cursor))) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid page");
      }
      const auto scope = p::authz::scope_filter(principal);
      auto rows = co_await tx->connection().query(
          {.sql = ("SELECT site::text,body::text FROM notification.notifications WHERE tenant=$1::uuid AND "
                   "recipient=$2::uuid AND channel=1 AND status='complete' AND "
                   "body->>'state'='NOTIFICATION_STATE_DELIVERED' AND ($3='' OR "
                   "((body->>'createdAt')::timestamptz,id)<(SELECT (body->>'createdAt')::timestamptz,id FROM "
                   "notification.notifications WHERE tenant=$1::uuid AND recipient=$2::uuid AND "
                   "id=NULLIF($3,'')::uuid)) AND (NOT $4::boolean OR NOT COALESCE((body->>'read')::boolean,false)) "
                   "AND ($7::boolean OR $5::jsonb ? site::text OR $8::jsonb ? COALESCE(body->'data'->>'zoneId','')) "
                   "ORDER BY (body->>'createdAt')::timestamptz DESC,id DESC LIMIT $6::integer"),
           .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(cursor),
                          Param::text(body.value("unreadOnly", false) ? "true" : "false"),
                          Param::text(Json(scope.sites).dump()), Param::text(std::to_string(size + 1)),
                          Param::boolean(scope.all), Param::text(Json(scope.zones).dump())}});
      if (!rows) {
        co_return std::unexpected(rows.error());
      }
      output["notifications"] = Json::array();
      for (std::size_t i = 0; i < rows->size(); ++i) {
        if (std::cmp_equal(i, size)) {
          output["nextPageToken"] = output["notifications"].back()["notificationId"];
          break;
        }
        output["notifications"].push_back(Json::parse(rows->value(i, 1).value_or("")));
      }
      auto unread = co_await tx->connection().query(
          {.sql = ("SELECT count(*) FROM notification.notifications WHERE tenant=$1::uuid AND recipient=$2::uuid AND "
                   "channel=1 AND status='complete' AND body->>'state'='NOTIFICATION_STATE_DELIVERED' AND NOT "
                   "COALESCE((body->>'read')::boolean,false) AND ($4::boolean OR $3::jsonb ? site::text OR "
                   "$5::jsonb ? COALESCE(body->'data'->>'zoneId',''))"),
           .parameters = {Param::text(principal.tenant), Param::text(principal.subject),
                          Param::text(Json(scope.sites).dump()), Param::boolean(scope.all),
                          Param::text(Json(scope.zones).dump())}});
      if (!unread) {
        co_return std::unexpected(unread.error());
      }
      output["unreadCount"] = std::stoull(std::string(unread->value(0, 0).value_or("0")));
      co_return output;
    }
    if (contact) {
      const auto user = body.value("userId", std::string{});
      if (!p::stream::uuid(user) || !p::stream::uuid(site)
          || (!body.value("email", std::string{}).empty() && !valid_email(body.at("email").get<std::string>()))) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid contact");
      }
      if (!body.value("webhookUrl", std::string{}).empty()) {
        auto valid = co_await transport_->check_webhook(body.at("webhookUrl").get<std::string>());
        if (!valid) {
          co_return std::unexpected(valid.error());
        }
      }
      auto person = co_await lookup_(user);
      if (!person) {
        co_return std::unexpected(person.error());
      }
      if (!p::authz::scoped(*person, {principal.tenant, site})) {
        co_return p::fail(Code::kAuthScopeDenied, "recipient outside site");
      }
      auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                   {Param::text(principal.tenant + ":contact:" + site + ":" + user)});
      if (!lock) {
        co_return std::unexpected(lock.error());
      }
      auto previous = co_await tx->connection().query(
          {.sql = "SELECT body::text FROM notification.contacts WHERE tenant=$1::uuid AND id=$2::uuid AND "
                  "site=$3::uuid FOR UPDATE",
           .parameters = {Param::text(principal.tenant), Param::text(user), Param::text(site)}});
      if (!previous) {
        co_return std::unexpected(previous.error());
      }
      const auto version =
          previous->size()
              ? std::stoull(Json::parse(previous->value(0, 0).value_or("")).value("version", std::string("0")))
              : 0;
      if (version && !expected) {
        co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
      }
      if (expected && *expected != version) {
        co_return p::fail(Code::kCommonConcurrentModification, "contact version changed");
      }
      body["version"] = std::to_string(version + 1);
      auto saved = co_await execute(
          tx,
          ("INSERT INTO notification.contacts VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb) ON CONFLICT(tenant,id,site) "
           "DO UPDATE SET body=EXCLUDED.body"),
          {Param::text(principal.tenant), Param::text(user), Param::text(site), Param::text(body.dump())});
      if (!saved) {
        co_return std::unexpected(saved.error());
      }
      output["contact"] = body;
    } else {
      const auto id = body.value("notificationId", std::string{});
      if (!p::stream::uuid(id)) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid notification identity");
      }
      auto rows = co_await tx->connection().query(
          {.sql = "SELECT site::text,body::text,status,destination FROM notification.notifications WHERE "
                  "tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
           .parameters = {Param::text(principal.tenant), Param::text(id)}});
      if (!rows) {
        co_return std::unexpected(rows.error());
      }
      if (!rows->size()) {
        co_return p::fail(Code::kCommonNotFound, "notification unavailable");
      }
      const auto resource = std::string(rows->value(0, 0).value_or(""));
      auto value = parse<response::v1::Notification>(Json::parse(rows->value(0, 1).value_or("")));
      if (!p::authz::scoped(principal, {principal.tenant, resource, value.data().at("zoneId")})
          || (operation == "read" && (value.recipient_user_id() != principal.subject || value.channel() != 1))) {
        co_return p::fail(Code::kAuthScopeDenied, "notification outside scope");
      }
      if (operation == "read") {
        value.set_read(true);
      } else {
        if (rows->value(0, 2) != "unknown" || (value.channel() != 2 && value.channel() != 3)) {
          co_return p::fail(Code::kCommonConcurrentModification, "delivery is not unknown");
        }
        if (!expected) {
          co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
        }
        if (*expected != value.version()) {
          co_return p::fail(Code::kCommonConcurrentModification, "notification version changed");
        }
        const auto receipt = body.value("receiptId", std::string{});
        if (receipt.empty() || receipt.size() > 200) {
          co_return p::fail(Code::kCommonInvalidArgument, "verified receipt required");
        }
        if (!body.value("accepted", false)) {
          value.set_state(response::v1::NOTIFICATION_STATE_FAILED);
        } else if (value.channel() == 2) {
          value.set_state(response::v1::NOTIFICATION_STATE_SENT);
        } else {
          value.set_state(response::v1::NOTIFICATION_STATE_DELIVERED);
          *value.mutable_delivered_at() = Time::GetCurrentTime();
        }
        value.set_receipt_id(receipt);
        value.clear_failure_code();
      }
      value.set_version(value.version() + 1);
      auto saved = co_await save(tx, principal.tenant, resource, value, "complete",
                                 std::string(rows->value(0, 3).value_or("")), 0);
      if (!saved) {
        co_return std::unexpected(saved.error());
      }
      output["notification"] = json(value);
    }
    auto logged = co_await audit(
        tx, principal.tenant, principal.subject, "notification." + operation,
        contact ? body.at("userId").get<std::string>() : body.at("notificationId").get<std::string>(), site);
    if (!logged) {
      co_return std::unexpected(logged.error());
    }
    auto cached = co_await execute(tx, "INSERT INTO notification.requests VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb)",
                                   {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key),
                                    Param::text(fingerprint), Param::text(output.dump())});
    if (!cached) {
      co_return std::unexpected(cached.error());
    }
    co_return output;
  });
}
}  // namespace psim::notification_service

// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)
