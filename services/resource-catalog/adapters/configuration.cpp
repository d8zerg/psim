#include "configuration.hpp"

#include <google/protobuf/message.h>
#include <google/protobuf/util/time_util.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "model.hpp"
#include "psim/common/v1/envelope.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"

namespace psim::resource_catalog {
namespace {
std::string action(std::string_view kind, bool write) {
  return std::string(kind == "mapping" ? "mapping." : "settings.") + (write ? "write" : "read");
}

google::protobuf::Message* value(config::v1::ConfigRecord& record) {
  if (record.has_mapping_rule_set()) {
    return record.mutable_mapping_rule_set();
  }
  if (record.has_retention()) {
    return record.mutable_retention();
  }
  return record.mutable_processing();
}
}  // namespace

asio::awaitable<p::Result<Json>> Repository::configurations(p::security::Principal principal, std::string kind,
                                                            std::string id) {
  auto access = p::authz::decide(principal, action(kind, false), {principal.tenant}, std::chrono::system_clock::now());
  if (!access) {
    co_return std::unexpected(access.error());
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query({.sql = "SELECT body::text FROM catalog.configuration WHERE tenant=$1::uuid AND "
                                               "kind=$2 AND ($3='' OR id=$3) ORDER BY id LIMIT 10001",
                                        .parameters = {p::pg::Parameter::text(principal.tenant),
                                                       p::pg::Parameter::text(kind), p::pg::Parameter::text(id)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (kind == "mapping" && id.empty()) {
    if (rows->size() > 10000) {
      co_return p::fail(p::ErrorCode::kCommonRateLimited, "mapping list limit reached");
    }
    Json list = Json::array();
    for (std::size_t i = 0; i < rows->size(); ++i) {
      list.push_back(Json::parse(rows->value(i, 0).value_or("")));
    }
    co_return list;
  }
  if (rows->size() == 0) {
    if (kind == "mapping") {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "mapping not found");
    }
    co_return default_configuration(kind);
  }
  co_return Json::parse(rows->value(0, 0).value_or(""));
}

asio::awaitable<p::Result<Json>> Repository::configure(config::v1::ConfigRecord record, std::string kind,
                                                       std::string id, Mutation mutation) {
  auto access = p::authz::decide(mutation.principal, action(kind, true), {mutation.principal.tenant},
                                 std::chrono::system_clock::now());
  if (!access) {
    co_return std::unexpected(access.error());
  }
  co_return co_await p::pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, &record, &kind, &id, &mutation](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        auto locked = co_await lock(tx, mutation.principal.tenant);
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        auto prior = co_await previous(tx, mutation);
        if (!prior) {
          co_return std::unexpected(prior.error());
        }
        if (*prior) {
          co_return **prior;
        }
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT version,body::text FROM catalog.configuration WHERE tenant=$1::uuid AND kind=$2 AND id=$3",
             .parameters = {p::pg::Parameter::text(mutation.principal.tenant), p::pg::Parameter::text(kind),
                            p::pg::Parameter::text(id)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        const auto current = rows->size() == 0 ? 0 : *rows->integer(0, 0);
        if (!std::cmp_equal(mutation.expected, current)) {
          co_return p::fail(p::ErrorCode::kCommonConcurrentModification, "configuration version changed");
        }
        if (current >= std::numeric_limits<std::int32_t>::max()) {
          co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "configuration version exhausted");
        }
        const auto next = static_cast<std::uint64_t>(current + 1);
        auto* payload = value(record);
        const auto* version = payload->GetDescriptor()->FindFieldByName("version");
        if (version->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_UINT32) {
          payload->GetReflection()->SetUInt32(payload, version, static_cast<std::uint32_t>(next));
        } else {
          payload->GetReflection()->SetUInt64(payload, version, next);
        }
        if (record.has_mapping_rule_set()) {
          auto* mapping = record.mutable_mapping_rule_set();
          if (rows->size() != 0) {
            const auto old = Json::parse(rows->value(0, 1).value_or(""));
            mapping->set_mapping_rule_set_id(old.at("mappingRuleSetId").get<std::string>());
          } else if (mapping->mapping_rule_set_id().empty()) {
            mapping->set_mapping_rule_set_id(p::stream::uuid7());
          }
          *mapping->mutable_published_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
          mapping->mutable_published_by()->set_kind(common::v1::ACTOR_KIND_USER);
          mapping->mutable_published_by()->set_id(mutation.principal.subject);
        }
        auto* env = record.mutable_envelope();
        env->set_message_id(p::stream::uuid7());
        env->set_correlation_id(mutation.key.empty() ? env->message_id() : mutation.key);
        env->set_tenant_id(mutation.principal.tenant);
        env->set_schema_version("1");
        env->set_sequence(next);
        *env->mutable_occurred_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
        *env->mutable_produced_at() = env->occurred_at();
        env->mutable_actor()->set_kind(common::v1::ACTOR_KIND_USER);
        env->mutable_actor()->set_id(mutation.principal.subject);
        Json body = Json::parse(p::stream::json(*payload));
        auto stored = co_await tx->connection().query(
            {.sql = "INSERT INTO catalog.configuration VALUES($1::uuid,$2,$3,$4,$5::jsonb) ON CONFLICT(tenant,kind,id) "
                    "DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body",
             .parameters = {p::pg::Parameter::text(mutation.principal.tenant), p::pg::Parameter::text(kind),
                            p::pg::Parameter::text(id), p::pg::Parameter::integer(current + 1),
                            p::pg::Parameter::text(body.dump())}});
        if (!stored) {
          co_return std::unexpected(stored.error());
        }
        const auto key = kind + "/" + mutation.principal.tenant + (kind == "mapping" ? "/" + id : "");
        auto published =
            co_await messages_.append(tx, {.id = env->message_id(), .record = encode_("psim.config.v1", key, record)});
        if (!published) {
          co_return std::unexpected(published.error());
        }
        auto audited = co_await audit(tx, mutation, kind, id, "", std::chrono::system_clock::now(), false, body,
                                      env->message_id());
        if (!audited) {
          co_return std::unexpected(audited.error());
        }
        auto remembered = co_await remember(tx, mutation, body);
        if (!remembered) {
          co_return std::unexpected(remembered.error());
        }
        co_return body;
      });
}

asio::awaitable<p::Result<void>> Repository::project_configuration(config::v1::ConfigRecord record) {
  if (!record.has_mapping_rule_set() && !record.has_retention() && !record.has_processing()) {
    co_return p::Result<void>{};
  }
  if (!canonical_uuid(record.envelope().tenant_id())) {
    co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "configuration tenant required");
  }
  auto* payload = value(record);
  auto body = Json::parse(p::stream::json(*payload));
  std::uint64_t version = record.processing().version();
  std::string kind = "processing";
  if (record.has_mapping_rule_set()) {
    version = record.mapping_rule_set().version();
    kind = "mapping";
  } else if (record.has_retention()) {
    version = record.retention().version();
    kind = "retention";
  }
  if (version == 0 || version > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "configuration version required");
  }
  const auto id = record.has_mapping_rule_set() ? record.mapping_rule_set().connector_type() : kind;
  co_return co_await p::pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [&record, &body, &kind, &id, version](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto locked = co_await lock(tx, record.envelope().tenant_id());
        if (!locked) {
          co_return locked;
        }
        auto stored = co_await tx->connection().query(
            {.sql = "INSERT INTO catalog.configuration VALUES($1::uuid,$2,$3,$4,$5::jsonb) ON CONFLICT(tenant,kind,id) "
                    "DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body WHERE "
                    "catalog.configuration.version<EXCLUDED.version",
             .parameters = {p::pg::Parameter::text(record.envelope().tenant_id()), p::pg::Parameter::text(kind),
                            p::pg::Parameter::text(id), p::pg::Parameter::integer(static_cast<std::int64_t>(version)),
                            p::pg::Parameter::text(body.dump())}});
        if (!stored) {
          co_return std::unexpected(stored.error());
        }
        co_return p::Result<void>{};
      });
}
}  // namespace psim::resource_catalog
