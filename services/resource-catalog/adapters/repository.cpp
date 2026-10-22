#include "repository.hpp"

#include <google/protobuf/util/json_util.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/catalog/v1/maintenance.pb.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/config/v1/config.pb.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::resource_catalog {
namespace pg = p::pg;
using Param = pg::Parameter;
using Code = p::ErrorCode;

namespace {
void envelope(common::v1::Envelope* out, Mutation mutation, Time now, std::uint64_t version = 0) {
  out->set_message_id(p::stream::uuid7());
  out->set_tenant_id(mutation.principal.tenant);
  out->set_correlation_id(mutation.key.empty() ? out->message_id() : mutation.key);
  out->set_sequence(version);
  out->set_schema_version("1");
  (void)google::protobuf::util::JsonStringToMessage('"' + timestamp(now) + '"', out->mutable_occurred_at());
  *out->mutable_produced_at() = out->occurred_at();
  out->mutable_actor()->set_kind(mutation.principal.subject == "resource-catalog" ? common::v1::ACTOR_KIND_SERVICE
                                                                                  : common::v1::ACTOR_KIND_USER);
  out->mutable_actor()->set_id(mutation.principal.subject);
}

Mutation system_mutation(std::string tenant) {
  Mutation mutation;
  mutation.principal = {.subject = "resource-catalog",
                        .tenant = std::move(tenant),
                        .roles = {"admin"},
                        .scopes = {"*"},
                        .expires_at = Time::max()};
  return mutation;
}

std::vector<Param> identity(std::string tenant, std::string kind, std::string id) {
  return {Param::text(std::move(tenant)), Param::text(std::move(kind)), Param::text(std::move(id))};
}
}  // namespace

asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto messages = co_await messages_.initialize(&**lease);
  if (!messages) {
    co_return messages;
  }
  co_return co_await pg::Migrator("catalog").apply(
      &**lease,
      {{.version = 1,
        .name = "resources files and maintenance",
        .expand =
            {("CREATE TABLE catalog.resources (tenant uuid NOT NULL, kind text NOT NULL, id uuid NOT NULL, site text "
              "NOT "
              "NULL, parent text NOT NULL, code text NOT NULL, connector text NOT NULL, external text NOT NULL, "
              "version "
              "bigint NOT NULL CHECK(version>0), body jsonb NOT NULL, due timestamptz, PRIMARY KEY(tenant,kind,id))"),
             "CREATE UNIQUE INDEX catalog_location_code ON catalog.resources(tenant,parent,code) WHERE kind='location'",
             "CREATE UNIQUE INDEX catalog_type_code ON catalog.resources(tenant,code) WHERE kind='device_type'",
             ("CREATE UNIQUE INDEX catalog_device_external ON catalog.resources(tenant,connector,external) WHERE "
              "kind='device'"),
             "CREATE INDEX catalog_parent ON catalog.resources(tenant,kind,parent,id)",
             "CREATE INDEX catalog_site ON catalog.resources(tenant,kind,site,id)",
             "CREATE INDEX catalog_due ON catalog.resources(due) WHERE due IS NOT NULL",
             ("CREATE TABLE catalog.sources (tenant uuid NOT NULL, connector uuid NOT NULL, ref text NOT NULL, device "
              "uuid NOT NULL, source uuid NOT NULL, PRIMARY KEY(tenant,connector,ref))"),
             "CREATE INDEX catalog_sources_device ON catalog.sources(tenant,device)",
             ("CREATE TABLE catalog.requests (tenant uuid NOT NULL, actor text NOT NULL, key uuid NOT NULL, "
              "fingerprint "
              "text NOT NULL, response jsonb NOT NULL, PRIMARY KEY(tenant,actor,key))"),
             ("CREATE TABLE catalog.files (tenant uuid NOT NULL, id uuid NOT NULL, floor uuid NOT NULL, site text NOT "
              "NULL, key text NOT NULL UNIQUE, body jsonb NOT NULL, linked boolean NOT NULL DEFAULT false, expires "
              "timestamptz NOT NULL DEFAULT clock_timestamp()+interval '10 minutes', PRIMARY KEY(tenant,id))"),
             "CREATE INDEX catalog_files_floor ON catalog.files(tenant,floor,id) WHERE linked",
             "CREATE INDEX catalog_files_cleanup ON catalog.files(expires) WHERE NOT linked",
             ("CREATE TABLE catalog.imports (tenant uuid NOT NULL, id uuid NOT NULL, principal jsonb NOT NULL, report "
              "jsonb NOT NULL, processed integer NOT NULL DEFAULT 0, dry_run boolean NOT NULL, PRIMARY "
              "KEY(tenant,id))"),
             ("CREATE TABLE catalog.import_rows (tenant uuid NOT NULL, job uuid NOT NULL, row integer NOT NULL, body "
              "jsonb NOT NULL, PRIMARY KEY(tenant,job,row), FOREIGN KEY(tenant,job) REFERENCES "
              "catalog.imports(tenant,id))"),
             ("CREATE TABLE catalog.import_seen (tenant uuid NOT NULL, job uuid NOT NULL, connector text NOT NULL, "
              "external text NOT NULL, PRIMARY KEY(tenant,job,connector,external))")}},
       {.version = 2,
        .name = "global resource identities",
        .expand = {"CREATE UNIQUE INDEX catalog_resource_identity ON catalog.resources(kind,id)"}},
       {.version = 3,
        .name = "versioned tenant configuration",
        .expand = {"CREATE TABLE catalog.configuration(tenant uuid NOT NULL, kind text NOT NULL, id text NOT NULL, "
                   "version bigint NOT NULL, body jsonb NOT NULL, PRIMARY KEY(tenant,kind,id))"}}});
}

asio::awaitable<p::Result<void>> Repository::lock(pg::Transaction* tx, std::string tenant) {
  auto result = co_await tx->connection().query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                                 .parameters = {Param::text("catalog:" + tenant)}});
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<Json>> Repository::load(pg::Transaction* tx, std::string tenant, std::string kind,
                                                  std::string id) {
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT body::text FROM catalog.resources WHERE tenant=$1::uuid AND kind=$2 AND id=$3::uuid",
       .parameters = identity(std::move(tenant), std::move(kind), std::move(id))});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "resource not found");
  }
  co_return Json::parse(rows->value(0, 0).value_or(""));
}

asio::awaitable<p::Result<Json>> Repository::get(p::security::Principal principal, std::string kind, std::string id) {
  if (!canonical_uuid(id) || !canonical_uuid(principal.tenant)) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid resource identity");
  }
  co_return co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [&principal, &kind, &id](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        auto result = co_await load(tx, principal.tenant, kind, id);
        if (!result) {
          co_return result;
        }
        if (!allowed(principal, result->value("siteId", ""))) {
          co_return p::fail(Code::kAuthScopeDenied, "resource outside access scope");
        }
        co_return result;
      });
}

asio::awaitable<p::Result<Page>> Repository::list(p::security::Principal principal, std::string kind, Json filters,
                                                  std::size_t size, std::string after) {
  if (!canonical_uuid(principal.tenant) || size == 0 || size > 1000 || (!after.empty() && !canonical_uuid(after))) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid pagination");
  }
  std::vector<std::string> const sites(principal.scopes.begin(), principal.scopes.end());
  if (!principal.scopes.contains("*") && sites.empty()) {
    co_return Page{};
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  pg::Query query{
      .sql =
          "WITH RECURSIVE subtree AS (SELECT id::text FROM catalog.resources WHERE tenant=$1::uuid AND kind='location' "
          "AND id::text=$5 UNION ALL SELECT r.id::text FROM catalog.resources r JOIN subtree s ON r.parent=s.id WHERE "
          "r.tenant=$1::uuid AND r.kind='location') SELECT body::text,id::text FROM catalog.resources WHERE "
          "tenant=$1::uuid AND kind=$2 AND ($3='' OR id::text>$3) AND ($4::boolean OR site IN (SELECT "
          "jsonb_array_elements_text($6::jsonb))) AND ($5='' OR parent IN (SELECT id FROM subtree) OR id::text=$5) AND "
          "($7='' OR parent=$7) AND ($8='' OR connector=$8) AND ($9='' OR body->>'deviceTypeId'=$9) AND ($10='' OR "
          "body->>'kind'=$10) AND ($11='' OR body->>'state'=$11) AND ($12::boolean OR "
          "COALESCE(body->>'state','')<>'RESOURCE_STATE_ARCHIVED') AND ($13='' OR site=$13) ORDER BY id LIMIT "
          "$14::integer",
      .parameters = {Param::text(principal.tenant), Param::text(kind), Param::text(after),
                     Param::boolean(principal.scopes.contains("*")), Param::text(filters.value("locationId", "")),
                     Param::text(Json(sites).dump()), Param::text(filters.value("parentId", "")),
                     Param::text(filters.value("connectorId", "")), Param::text(filters.value("deviceTypeId", "")),
                     Param::text(filters.value("kind", "")), Param::text(filters.value("state", "")),
                     Param::boolean(filters.value("includeArchived", "false") == "true"),
                     Param::text(filters.value("siteId", "")), Param::integer(static_cast<std::int64_t>(size + 1))}};
  auto rows = co_await (*lease)->query(std::move(query));
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  Page page;
  for (std::size_t i = 0; i < std::min(size, rows->size()); ++i) {
    auto body = Json::parse(rows->value(i, 0).value_or(""));
    if (!allowed(principal, body.value("siteId", ""))) {
      continue;
    }
    page.items.push_back(std::move(body));
  }
  if (rows->size() > size) {
    page.next = std::string(rows->value(size - 1, 1).value_or(""));
  }
  co_return page;
}

asio::awaitable<p::Result<std::optional<Json>>> Repository::previous(pg::Transaction* tx, Mutation mutation) {
  if (mutation.key.empty()) {
    co_return std::optional<Json>{};
  }
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT fingerprint,response::text FROM catalog.requests WHERE tenant=$1::uuid AND actor=$2 AND "
              "key=$3::uuid",
       .parameters = {Param::text(mutation.principal.tenant), Param::text(mutation.principal.subject),
                      Param::text(mutation.key)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return std::optional<Json>{};
  }
  if (rows->value(0, 0) != mutation.fingerprint) {
    co_return p::fail(Code::kCommonIdempotencyKeyReused, "request identity has different content");
  }
  co_return std::optional<Json>{Json::parse(rows->value(0, 1).value_or(""))};
}

asio::awaitable<p::Result<void>> Repository::remember(pg::Transaction* tx, Mutation mutation, Json response) {
  if (mutation.key.empty()) {
    co_return p::Result<void>{};
  }
  auto stored = co_await tx->connection().query(
      {.sql = "INSERT INTO catalog.requests VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb)",
       .parameters = {Param::text(mutation.principal.tenant), Param::text(mutation.principal.subject),
                      Param::text(mutation.key), Param::text(mutation.fingerprint), Param::text(response.dump())}});
  if (!stored) {
    co_return std::unexpected(stored.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::audit(pg::Transaction* tx, Mutation mutation, std::string kind,
                                                   std::string id, std::string site, Time now, bool denied,
                                                   Json snapshot, std::string cause) {
  audit::v1::AuditEventRecord record;
  envelope(record.mutable_envelope(), mutation, now);
  auto* entry = record.mutable_audit_entry();
  *entry->mutable_actor() = record.envelope().actor();
  entry->set_action(denied ? "access.denied" : "catalog." + kind + "." + mutation.action);
  entry->set_object_type(std::move(kind));
  entry->set_object_id(std::move(id));
  entry->set_site_id(std::move(site));
  entry->set_outcome(denied ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
  if (!snapshot.is_null()) {
    const auto version = snapshot.at("version").is_string()
                             ? snapshot.at("version").get<std::string>()
                             : std::to_string(snapshot.at("version").get<std::uint64_t>());
    (*entry->mutable_details())["version"] = version;
    (*entry->mutable_details())["snapshot"] = snapshot.dump();
    record.mutable_envelope()->set_sequence(std::stoull(version));
    record.mutable_envelope()->set_causation_id(std::move(cause));
  }
  co_return co_await messages_.append(tx, {.id = record.envelope().message_id(),
                                           .record = encode_("psim.audit.v1", mutation.principal.tenant, record)});
}

asio::awaitable<p::Result<void>> Repository::publish(pg::Transaction* tx, Resource resource, Mutation mutation,
                                                     Time now) {
  std::string topic;
  std::string key;
  std::unique_ptr<google::protobuf::Message> output;
  common::v1::Envelope* env = nullptr;
  google::protobuf::Message* value = nullptr;
  if (resource.kind == "maintenance") {
    auto record = std::make_unique<config::v1::ConfigRecord>();
    env = record->mutable_envelope();
    value = record->mutable_maintenance_window();
    topic = "psim.config.v1";
    key = "maintenance/" + mutation.principal.tenant + "/" + resource.id;
    output = std::move(record);
  } else {
    auto record = std::make_unique<catalog::v1::CatalogRecord>();
    env = record->mutable_envelope();
    if (resource.kind == "location") {
      value = record->mutable_location();
    } else if (resource.kind == "device_type") {
      value = record->mutable_device_type();
    } else if (resource.kind == "device") {
      value = record->mutable_device();
    } else {
      value = record->mutable_stored_file();
    }
    topic = "psim.catalog.v1";
    key = resource.kind + ":" + resource.id;
    output = std::move(record);
  }
  if (!google::protobuf::util::JsonStringToMessage(resource.body.dump(), value).ok()) {
    co_return p::fail(Code::kCommonInternal, "cannot serialize stored resource");
  }
  envelope(env, mutation, now, resource.version);
  auto result = co_await messages_.append(tx, {.id = env->message_id(), .record = encode_(topic, key, *output)});
  if (!result) {
    co_return result;
  }
  co_return co_await audit(tx, mutation, resource.kind, resource.id, resource.site, now, false, resource.body,
                           env->message_id());
}

asio::awaitable<p::Result<Json>> Repository::apply(pg::Transaction* tx, Resource resource, Mutation mutation,
                                                   Time now) {
  auto old = co_await load(tx, mutation.principal.tenant, resource.kind, resource.id);
  if (!old && old.error().code() != Code::kCommonNotFound) {
    co_return std::unexpected(old.error());
  }
  if ((old && std::stoull(old->at("version").get<std::string>()) != mutation.expected)
      || (!old && mutation.expected != 0)) {
    co_return p::fail(Code::kCommonConcurrentModification, "resource version changed");
  }
  if (old && mutation.expected == 0) {
    co_return p::fail(Code::kCommonConcurrentModification, "resource already exists");
  }
  if (resource.kind == "connector") {
    co_return p::fail(Code::kCommonInvalidArgument, "connector credentials are managed by the gateway");
  }
  if (old && !allowed(mutation.principal, old->value("siteId", ""), true)) {
    co_return p::fail(Code::kAuthScopeDenied, "existing resource outside access scope");
  }
  if (mutation.action == "archive" || mutation.action == "cancel") {
    if (!old) {
      co_return p::fail(Code::kCommonNotFound, "resource not found");
    }
    resource.body = *old;
    resource.site = old->value("siteId", "");
    resource.parent = old->value("parentId", "");
    resource.code = old->value("code", "");
    resource.body["state"] = mutation.action == "archive" ? "RESOURCE_STATE_ARCHIVED" : "MAINTENANCE_STATE_CANCELLED";
  }
  if (resource.kind == "location") {
    const auto kind = resource.body.value("kind", "");
    if (kind == "LOCATION_KIND_SITE") {
      if (!resource.parent.empty() || (!resource.site.empty() && resource.site != resource.id)) {
        co_return p::fail(Code::kCatalogInvalidParent, "site cannot have a parent");
      }
      resource.site = resource.id;
      const auto zone = resource.body.value("timezone", "");
      if (zone.empty() || zone.size() > 128 || zone.find("..") != std::string::npos) {
        co_return p::fail(Code::kCommonInvalidArgument, "site timezone is required");
      }
      try {
        (void)std::chrono::locate_zone(zone);
      } catch (const std::exception&) {
        co_return p::fail(Code::kCommonInvalidArgument, "unknown site timezone");
      }
    } else {
      if (!canonical_uuid(resource.parent)) {
        co_return p::fail(Code::kCatalogInvalidParent, "location parent is required");
      }
      auto parent = co_await load(tx, mutation.principal.tenant, "location", resource.parent);
      if (!parent || parent->value("state", "") == "RESOURCE_STATE_ARCHIVED") {
        co_return p::fail(Code::kCatalogInvalidParent, "active parent not found");
      }
      const auto parent_kind = parent->value("kind", "");
      if (!((kind == "LOCATION_KIND_BUILDING" && parent_kind == "LOCATION_KIND_SITE")
            || (kind == "LOCATION_KIND_FLOOR" && parent_kind == "LOCATION_KIND_BUILDING")
            || (kind == "LOCATION_KIND_ZONE"
                && (parent_kind == "LOCATION_KIND_FLOOR" || parent_kind == "LOCATION_KIND_BUILDING"
                    || parent_kind == "LOCATION_KIND_SITE")))) {
        co_return p::fail(Code::kCatalogInvalidParent, "invalid hierarchy level");
      }
      if (!resource.site.empty() && resource.site != parent->at("siteId").get<std::string>()) {
        co_return p::fail(Code::kCatalogSiteChangeForbidden, "location site does not match parent");
      }
      resource.site = parent->at("siteId");
      if (!resource.body.value("timezone", "").empty()) {
        co_return p::fail(Code::kCommonInvalidArgument, "timezone belongs to the site");
      }
    }
    if (old && (old->at("siteId") != resource.site || old->at("kind") != kind)) {
      co_return p::fail(Code::kCatalogSiteChangeForbidden, "location kind and site are immutable");
    }
    if (resource.body.value("state", "") == "RESOURCE_STATE_ARCHIVED") {
      auto children = co_await tx->connection().query(
          {.sql = "SELECT 1 FROM catalog.resources WHERE tenant=$1::uuid AND parent=$2 AND kind IN "
                  "('location','device') AND body->>'state'<>'RESOURCE_STATE_ARCHIVED' LIMIT 1",
           .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.id)}});
      if (!children) {
        co_return std::unexpected(children.error());
      }
      if (children->size() != 0) {
        co_return p::fail(Code::kCatalogLocationNotEmpty, "location has active children or devices");
      }
    }
    if (kind == "LOCATION_KIND_ZONE" && old && old->value("parentId", "") != resource.parent) {
      auto parent = co_await load(tx, mutation.principal.tenant, "location", resource.parent);
      if (!parent) {
        co_return std::unexpected(parent.error());
      }
      const auto plan = parent->value("floorPlan", Json::object());
      auto invalid = co_await tx->connection().query(
          {.sql = "SELECT 1 FROM catalog.resources WHERE tenant=$1::uuid AND kind='device' AND parent=$2 AND body ? "
                  "'position' AND body->>'state'<>'RESOURCE_STATE_ARCHIVED' AND (NOT $3::boolean OR "
                  "(body->'position'->>'x')::double precision>$4::double precision OR (body->'position'->>'y')::double "
                  "precision>$5::double precision) LIMIT 1",
           .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.id),
                          Param::boolean(parent->contains("floorPlan")),
                          Param::text(std::to_string(plan.value("width", 0.0))),
                          Param::text(std::to_string(plan.value("height", 0.0)))}});
      if (!invalid) {
        co_return std::unexpected(invalid.error());
      }
      if (invalid->size() != 0U) {
        co_return p::fail(Code::kCatalogPositionInvalid, "zone move invalidates device positions");
      }
    }
    if (mutation.action != "upload"
        && resource.body.value("floorPlan", Json()) != (old ? old->value("floorPlan", Json()) : Json())) {
      co_return p::fail(Code::kCommonInvalidArgument, "floor plan is changed through the image endpoint");
    }
  }
  if (resource.kind == "device_type" && old && resource.body != *old) {
    auto used = co_await tx->connection().query(
        {.sql = "SELECT 1 FROM catalog.resources WHERE tenant=$1::uuid AND kind='device' AND body->>'deviceTypeId'=$2 "
                "AND body->>'state'<>'RESOURCE_STATE_ARCHIVED' LIMIT 1",
         .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.id)}});
    if (!used) {
      co_return std::unexpected(used.error());
    }
    if ((used->size() != 0U)
        && (resource.body.value("sourceTemplates", Json::array()) != old->value("sourceTemplates", Json::array())
            || resource.body.value("state", "") == "RESOURCE_STATE_ARCHIVED")) {
      co_return p::fail(Code::kCatalogDeviceTypeInUse, "used device type source templates cannot change");
    }
  }
  if (resource.kind == "device") {
    auto zone = co_await load(tx, mutation.principal.tenant, "location", resource.parent);
    auto type = co_await load(tx, mutation.principal.tenant, "device_type", resource.body.at("deviceTypeId"));
    auto connector = co_await load(tx, mutation.principal.tenant, "connector", resource.connector);
    const bool retiring = old
                          && (resource.body.at("state") == "RESOURCE_STATE_DISABLED"
                              || resource.body.at("state") == "RESOURCE_STATE_ARCHIVED")
                          && old->at("zoneId") == resource.parent
                          && old->at("deviceTypeId") == resource.body.at("deviceTypeId")
                          && old->at("connectorId") == resource.connector;
    if (!zone || zone->value("kind", "") != "LOCATION_KIND_ZONE" || zone->value("siteId", "") != resource.site || !type
        || !connector
        || (!retiring
            && (zone->value("state", "") != "RESOURCE_STATE_ACTIVE"
                || type->value("state", "") != "RESOURCE_STATE_ACTIVE"
                || connector->value("state", "") != "RESOURCE_STATE_ACTIVE"))) {
      co_return p::fail(Code::kCommonInvalidArgument,
                        "device requires active zone, type and registered connector in its scope");
    }
    std::set<std::string> kinds;
    for (const auto& source : resource.body.value("sources", Json::array())) {
      kinds.insert(source.at("kind"));
    }
    for (const auto& source : type->value("sourceTemplates", Json::array())) {
      if (!kinds.contains(source.at("kind"))) {
        co_return p::fail(Code::kCommonInvalidArgument, "device is missing a source template");
      }
    }
    if (resource.body.contains("position")) {
      auto floor = co_await load(tx, mutation.principal.tenant, "location", zone->at("parentId"));
      const auto position = resource.body.at("position");
      const auto x = position.value("x", 0.0);
      const auto y = position.value("y", 0.0);
      if (!floor || !floor->contains("floorPlan") || x < 0 || y < 0 || x > (*floor)["floorPlan"].value("width", 0.0)
          || y > (*floor)["floorPlan"].value("height", 0.0)) {
        co_return p::fail(Code::kCatalogPositionInvalid, "position is outside the floor plan");
      }
    }
  }
  if (resource.kind == "maintenance") {
    const auto* const target_kind = resource.body.contains("deviceId") ? "device" : "location";
    const auto target_id = resource.body.value("deviceId", resource.body.value("zoneId", ""));
    if (!canonical_uuid(target_id)) {
      co_return p::fail(Code::kCommonInvalidArgument, "maintenance target is required");
    }
    auto target = co_await load(tx, mutation.principal.tenant, target_kind, target_id);
    if (!target || (mutation.action != "cancel" && target->value("state", "") == "RESOURCE_STATE_ARCHIVED")
        || (target_kind == std::string("location") && target->value("kind", "") != "LOCATION_KIND_ZONE")) {
      co_return p::fail(Code::kCommonInvalidArgument, "maintenance target is unavailable");
    }
    resource.site = mutation.action == "cancel" && old ? old->at("siteId").get<std::string>()
                                                       : target->at("siteId").get<std::string>();
    if (old
        && (old->value("deviceId", "") != resource.body.value("deviceId", "")
            || old->value("zoneId", "") != resource.body.value("zoneId", ""))) {
      co_return p::fail(Code::kCommonInvalidArgument, "maintenance target is immutable");
    }
    if (old
        && (old->at("state") == "MAINTENANCE_STATE_COMPLETED" || old->at("state") == "MAINTENANCE_STATE_CANCELLED")) {
      co_return p::fail(Code::kCommonConcurrentModification, "maintenance interval is terminal");
    }
    if (mutation.action != "cancel" && mutation.action != "complete" && mutation.action != "activate") {
      auto valid = validate_maintenance(resource.body, now);
      if (!valid) {
        co_return std::unexpected(valid.error());
      }
      if (old && old->at("state") == "MAINTENANCE_STATE_ACTIVE") {
        resource.body["startsAt"] = old->at("startsAt");
        resource.body["activatedAt"] = old->at("activatedAt");
        resource.body["state"] = "MAINTENANCE_STATE_ACTIVE";
      } else {
        resource.body["state"] = "MAINTENANCE_STATE_SCHEDULED";
        resource.body.erase("activatedAt");
        resource.body.erase("completedAt");
      }
      auto transitioned = maintenance_transition(resource.body, now);
      if (!transitioned) {
        co_return std::unexpected(transitioned.error());
      }
    }
    if (mutation.action == "cancel") {
      resource.body["completedAt"] = timestamp(now);
    }
  }
  if (!allowed(mutation.principal, resource.site, true)) {
    co_return p::fail(Code::kAuthScopeDenied, "resource outside write scope");
  }
  if (resource.kind == "location" || resource.kind == "device_type") {
    auto duplicate = co_await tx->connection().query(
        {.sql = "SELECT 1 FROM catalog.resources WHERE tenant=$1::uuid AND kind=$2 AND code=$3 AND ($2='device_type' "
                "OR parent=$4) AND id<>$5::uuid LIMIT 1",
         .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.kind), Param::text(resource.code),
                        Param::text(resource.parent), Param::text(resource.id)}});
    if (!duplicate) {
      co_return std::unexpected(duplicate.error());
    }
    if (duplicate->size() != 0U) {
      co_return p::fail(Code::kCatalogDuplicateCode, "resource code already exists");
    }
  }
  if (resource.kind == "device") {
    auto duplicate = co_await tx->connection().query(
        {.sql = "SELECT 1 FROM catalog.resources WHERE tenant=$1::uuid AND kind='device' AND connector=$2 AND "
                "external=$3 AND id<>$4::uuid LIMIT 1",
         .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.connector),
                        Param::text(resource.external), Param::text(resource.id)}});
    if (!duplicate) {
      co_return std::unexpected(duplicate.error());
    }
    if (duplicate->size() != 0U) {
      co_return p::fail(Code::kCatalogDuplicateExternalId, "external device identity already exists");
    }
    for (const auto& source : resource.body.value("sources", Json::array())) {
      auto duplicate_source = co_await tx->connection().query(
          {.sql = "SELECT 1 FROM catalog.sources WHERE tenant=$1::uuid AND connector=$2::uuid AND ref=$3 AND "
                  "device<>$4::uuid LIMIT 1",
           .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.connector),
                          Param::text(source.at("externalRef")), Param::text(resource.id)}});
      if (!duplicate_source) {
        co_return std::unexpected(duplicate_source.error());
      }
      if (duplicate_source->size() != 0U) {
        co_return p::fail(Code::kCatalogDuplicateSourceRef, "source reference already exists");
      }
    }
    auto removed = co_await tx->connection().query(
        {.sql = "DELETE FROM catalog.sources WHERE tenant=$1::uuid AND device=$2::uuid",
         .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.id)}});
    if (!removed) {
      co_return std::unexpected(removed.error());
    }
    for (const auto& source : resource.body.value("sources", Json::array())) {
      auto inserted = co_await tx->connection().query(
          {.sql = "INSERT INTO catalog.sources VALUES($1::uuid,$2::uuid,$3,$4::uuid,$5::uuid)",
           .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.connector),
                          Param::text(source.at("externalRef")), Param::text(resource.id),
                          Param::text(source.at("sourceId"))}});
      if (!inserted) {
        co_return std::unexpected(inserted.error());
      }
    }
  }
  resource.version = mutation.expected + 1;
  resource.body["version"] = std::to_string(resource.version);
  if (resource.kind != "device_type") {
    resource.body["siteId"] = resource.site;
    resource.body["updatedAt"] = timestamp(now);
  }
  std::optional<std::string> due;
  if (resource.kind == "maintenance") {
    const auto state = resource.body.at("state");
    if (state == "MAINTENANCE_STATE_SCHEDULED") {
      due = resource.body.at("startsAt");
    }
    if (state == "MAINTENANCE_STATE_ACTIVE") {
      due = resource.body.at("endsAt");
    }
  }
  auto stored = co_await tx->connection().query(
      {.sql = "INSERT INTO catalog.resources "
              "VALUES($1::uuid,$2,$3::uuid,$4,$5,$6,$7,$8,$9::bigint,$10::jsonb,$11::timestamptz) ON "
              "CONFLICT(tenant,kind,id) DO UPDATE SET "
              "site=EXCLUDED.site,parent=EXCLUDED.parent,code=EXCLUDED.code,connector=EXCLUDED.connector,external="
              "EXCLUDED.external,version=EXCLUDED.version,body=EXCLUDED.body,due=EXCLUDED.due",
       .parameters = {Param::text(mutation.principal.tenant), Param::text(resource.kind), Param::text(resource.id),
                      Param::text(resource.site), Param::text(resource.parent), Param::text(resource.code),
                      Param::text(resource.connector), Param::text(resource.external),
                      Param::integer(static_cast<std::int64_t>(resource.version)), Param::text(resource.body.dump()),
                      due ? Param::text(*due) : Param::null()}});
  if (!stored) {
    co_return std::unexpected(stored.error());
  }
  auto published = co_await publish(tx, resource, mutation, now);
  if (!published) {
    co_return std::unexpected(published.error());
  }
  co_return resource.body;
}

asio::awaitable<p::Result<Json>> Repository::change(Resource resource, Mutation mutation) {
  co_return co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this, &resource, &mutation](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        auto locked = co_await lock(tx, mutation.principal.tenant);
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        auto replay = co_await previous(tx, mutation);
        if (!replay) {
          co_return std::unexpected(replay.error());
        }
        if (*replay) {
          if (!allowed(mutation.principal, (**replay).value("siteId", ""), true)) {
            co_return p::fail(Code::kAuthScopeDenied, "request result outside access scope");
          }
          co_return **replay;
        }
        auto result = co_await apply(tx, resource, mutation, std::chrono::system_clock::now());
        if (!result) {
          co_return result;
        }
        auto saved = co_await remember(tx, mutation, *result);
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        co_return result;
      });
}

asio::awaitable<p::Result<void>> Repository::project(catalog::v1::CatalogRecord record) {
  if (!record.has_connector()) {
    co_return p::Result<void>{};
  }
  const auto& connector = record.connector();
  if (!canonical_uuid(record.envelope().tenant_id()) || !canonical_uuid(connector.connector_id())
      || connector.version() == 0) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid connector record");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto stored = co_await (*lease)->query(
      {.sql =
           "INSERT INTO catalog.resources "
           "VALUES($1::uuid,'connector',$2::uuid,'','','','','',$3::bigint,$4::jsonb,NULL) ON CONFLICT(tenant,kind,id) "
           "DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body WHERE catalog.resources.version<EXCLUDED.version",
       .parameters = {Param::text(record.envelope().tenant_id()), Param::text(connector.connector_id()),
                      Param::integer(static_cast<std::int64_t>(connector.version())),
                      Param::text(p::stream::json(connector))}});
  if (!stored) {
    co_return std::unexpected(stored.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::tick(Time now) {
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this, now](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        // Tenant locks precede row locks throughout the repository.
        auto tenants =
            co_await tx->connection().query({.sql = "SELECT DISTINCT tenant::text FROM catalog.resources WHERE "
                                                    "due<=$1::timestamptz ORDER BY tenant::text LIMIT 100",
                                             .parameters = {Param::text(timestamp(now))}});
        if (!tenants) {
          co_return std::unexpected(tenants.error());
        }
        for (std::size_t i = 0; i < tenants->size(); ++i) {
          const std::string tenant(tenants->value(i, 0).value_or(""));
          auto locked = co_await lock(tx, tenant);
          if (!locked) {
            co_return locked;
          }
          auto due = co_await tx->connection().query(
              {.sql = "SELECT id::text,body::text,version FROM catalog.resources WHERE tenant=$1::uuid AND "
                      "due<=$2::timestamptz ORDER BY due,id LIMIT 1000",
               .parameters = {Param::text(tenant), Param::text(timestamp(now))}});
          if (!due) {
            co_return std::unexpected(due.error());
          }
          for (std::size_t row = 0; row < due->size(); ++row) {
            auto body = Json::parse(due->value(row, 1).value_or(""));
            auto transition = maintenance_transition(body, now);
            if (!transition) {
              co_return std::unexpected(transition.error());
            }
            if (transition->empty()) {
              continue;
            }
            auto resource = parse("maintenance", body);
            if (!resource) {
              co_return std::unexpected(resource.error());
            }
            auto mutation = system_mutation(tenant);
            mutation.expected = static_cast<std::uint64_t>(*due->integer(row, 2));
            mutation.action = *transition;
            resource->version = mutation.expected + 1;
            resource->body["version"] = std::to_string(resource->version);
            resource->body["updatedAt"] = timestamp(now);
            const bool active = resource->body.at("state") == "MAINTENANCE_STATE_ACTIVE";
            auto saved = co_await tx->connection().query(
                {.sql = "UPDATE catalog.resources SET version=$4::bigint,body=$3::jsonb,due=$5::timestamptz WHERE "
                        "tenant=$1::uuid AND kind='maintenance' AND id=$2::uuid",
                 .parameters = {Param::text(tenant), Param::text(resource->id), Param::text(resource->body.dump()),
                                Param::integer(static_cast<std::int64_t>(resource->version)),
                                active ? Param::text(resource->body.at("endsAt")) : Param::null()}});
            if (!saved) {
              co_return std::unexpected(saved.error());
            }
            auto published = co_await publish(tx, *resource, mutation, now);
            if (!published) {
              co_return published;
            }
          }
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<void>> Repository::denied(p::security::Principal principal, std::string object) {
  Mutation mutation{.principal = principal};
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this, &mutation, &object](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        co_return co_await audit(tx, mutation, "request", object, "", std::chrono::system_clock::now(), true);
      });
}
}  // namespace psim::resource_catalog
