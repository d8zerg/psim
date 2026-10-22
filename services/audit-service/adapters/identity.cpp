// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines): transaction owns the awaited closure.
#include "identity.hpp"

#include <google/protobuf/util/time_util.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/common/v1/envelope.pb.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <string>
#include <string_view>

#include "chain.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::audit_service {
namespace {
using Time = google::protobuf::util::TimeUtil;
using Param = p::pg::Parameter;

std::string form(std::string_view value) {
  constexpr std::string_view kDigits = "0123456789ABCDEF";
  std::string output;
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || ch == '_'
        || ch == '-' || ch == '.') {
      output += ch;
    } else {
      output += '%';
      output += kDigits[byte >> 4];
      output += kDigits[byte & 15];
    }
  }
  return output;
}
}  // namespace

asio::awaitable<p::Result<void>> IdentitySource::poll() {
  if (options_.base.empty()) {
    co_return p::Result<void>{};
  }
  if (options_.client.empty() || options_.secret.empty() || !p::stream::uuid(options_.security_tenant)) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "identity audit configuration incomplete");
  }
  try {
    co_return co_await p::pg::in_transaction<
        void>(pool_, [this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
      auto locked = co_await tx->connection().query({.sql = "SELECT pg_try_advisory_xact_lock(hashtextextended($1,18))",
                                                     .parameters = {Param::text(options_.base)}});
      if (!locked) {
        co_return std::unexpected(locked.error());
      }
      if (locked->value(0, 0) != "t") {
        co_return p::Result<void>{};
      }
      auto token = co_await p::security::http_request(options_.base + "/realms/psim/protocol/openid-connect/token",
                                                      options_.http,
                                                      "grant_type=client_credentials&client_id=" + form(options_.client)
                                                          + "&client_secret=" + form(options_.secret),
                                                      "application/x-www-form-urlencoded");
      if (!token || token->status != 200) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity audit authentication unavailable");
      }
      auto http = options_.http;
      http.bearer_token = Json::parse(token->body).at("access_token").get<std::string>();
      auto configuration = co_await p::security::http_request(options_.base + "/admin/realms/psim/events/config", http);
      if (!configuration || configuration->status != 200) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity event configuration unavailable");
      }
      const auto config = Json::parse(configuration->body);
      if (!config.value("eventsEnabled", false) || !config.value("adminEventsEnabled", false)) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity security journals must be enabled");
      }
      const auto current = Time::TimestampToMilliseconds(Time::GetCurrentTime()) - 2000;
      for (const bool admin : {false, true}) {
        const auto source = options_.base + (admin ? ":admin" : ":user");
        auto cursor = co_await tx->connection().query(
            {.sql = "SELECT watermark,page,cutoff FROM audit.identity_cursors WHERE source=$1",
             .parameters = {Param::text(source)}});
        if (!cursor) {
          co_return std::unexpected(cursor.error());
        }
        const auto from = cursor->size() == 0 ? 0 : std::max<std::int64_t>(0, *cursor->integer(0, 0) - 60000);
        int offset = cursor->size() == 0 ? 0 : static_cast<int>(*cursor->integer(0, 1));
        const auto until = cursor->size() != 0 && *cursor->integer(0, 2) != 0 ? *cursor->integer(0, 2) : current;
        bool more = false;
        {
          auto url = options_.base + "/admin/realms/psim/" + (admin ? "admin-events" : "events")
                     + "?dateFrom=" + std::to_string(from) + "&dateTo=" + std::to_string(until)
                     + "&direction=asc&max=100&first=" + std::to_string(offset);
          if (!admin) {
            url += "&type=LOGIN&type=LOGIN_ERROR&type=LOGOUT&type=REFRESH_TOKEN_ERROR";
          }
          auto response = co_await p::security::http_request(url, http);
          if (!response || response->status != 200) {
            co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity security journal unavailable");
          }
          const auto records = Json::parse(response->body);
          if (!records.is_array()) {
            co_return p::fail(p::ErrorCode::kCommonUnavailable, "invalid identity security journal");
          }
          for (const auto& record : records) {
            const auto id = record.at("id").get<std::string>();
            if (!p::stream::uuid(id)) {
              co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity event id required");
            }
            auto identifier = source;
            identifier += ':';
            identifier += id;
            auto accepted = co_await p::pg::MessageStore("audit").accept(tx, "identity", identifier,
                                                                         p::stream::sha256(record.dump()));
            if (!accepted) {
              co_return std::unexpected(accepted.error());
            }
            if (!*accepted) {
              continue;
            }
            const auto user =
                admin ? record.at("authDetails").value("userId", std::string{}) : record.value("userId", std::string{});
            std::string tenant = options_.security_tenant;
            if (p::stream::uuid(user)) {
              auto profile =
                  co_await p::security::http_request(options_.base + "/admin/realms/psim/users/" + user, http);
              if (!profile || (profile->status != 200 && profile->status != 404)) {
                co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity audit user lookup unavailable");
              }
              if (profile->status == 200) {
                const auto attributes = Json::parse(profile->body).value("attributes", Json::object());
                if (attributes.contains("psim_tenant") && attributes.at("psim_tenant").size() == 1
                    && p::stream::uuid(attributes.at("psim_tenant")[0].get<std::string>())) {
                  tenant = attributes.at("psim_tenant")[0].get<std::string>();
                }
              }
            }
            audit::v1::AuditEventRecord value;
            auto* origin = value.mutable_envelope();
            origin->set_message_id(p::stream::uuid5(tenant, {"identity-event", source, id}));
            origin->set_causation_id(origin->message_id());
            origin->set_tenant_id(tenant);
            origin->set_schema_version("1");
            origin->set_correlation_id(origin->message_id());
            *origin->mutable_occurred_at() = Time::MillisecondsToTimestamp(record.at("time").get<std::int64_t>());
            *origin->mutable_produced_at() = origin->occurred_at();
            origin->mutable_actor()->set_id("audit-service");
            origin->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
            auto* entry = value.mutable_audit_entry();
            entry->mutable_actor()->set_id(user.empty() ? "anonymous" : user);
            entry->mutable_actor()->set_kind(common::v1::ACTOR_KIND_USER);
            entry->set_action(admin ? "identity.admin." + record.at("operationType").get<std::string>()
                                    : "identity." + record.at("type").get<std::string>());
            entry->set_object_type("identity");
            entry->set_object_id(admin ? record.value("resourcePath", std::string{}) : user);
            const auto error = record.value("error", std::string{});
            entry->set_outcome(error.empty() ? audit::v1::AUDIT_OUTCOME_SUCCESS : audit::v1::AUDIT_OUTCOME_DENIED);
            entry->set_client_address(admin ? record.at("authDetails").value("ipAddress", std::string{})
                                            : record.value("ipAddress", std::string{}));
            (*entry->mutable_details())["event_id"] = id;
            (*entry->mutable_details())["error"] = error;
            (*entry->mutable_details())["client_id"] = admin ? record.at("authDetails").value("clientId", std::string{})
                                                             : record.value("clientId", std::string{});
            if (admin) {
              (*entry->mutable_details())["resource_type"] = record.value("resourceType", std::string{});
            }
            auto queued = co_await p::pg::MessageStore("audit").append(
                tx, {origin->message_id(), encode_("psim.audit.v1", tenant, value)});
            if (!queued) {
              co_return queued;
            }
          }
          offset += static_cast<int>(records.size());
          more = records.size() == 100;
        }
        auto advanced = co_await tx->connection().query(
            {.sql =
                 "INSERT INTO audit.identity_cursors VALUES($1,$2::bigint,$3::integer,$4::bigint) ON CONFLICT(source) "
                 "DO UPDATE SET watermark=EXCLUDED.watermark,page=EXCLUDED.page,cutoff=EXCLUDED.cutoff",
             .parameters = {Param::text(source), Param::text(std::to_string(more ? from + 60000 : until)),
                            Param::text(std::to_string(more ? offset : 0)),
                            Param::text(std::to_string(more ? until : 0))}});
        if (!advanced) {
          co_return std::unexpected(advanced.error());
        }
      }
      co_return p::Result<void>{};
    });
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "invalid identity audit response");
  }
}
}  // namespace psim::audit_service

// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)
