#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <utility>

#include "model.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "repository.hpp"

namespace psim::resource_catalog {
namespace pg = p::pg;
using Param = pg::Parameter;
using Code = p::ErrorCode;

asio::awaitable<p::Result<void>> Repository::reserve_file(p::security::Principal principal, Json file) {
  auto floor = co_await get(principal, "location", file.at("ownerId"));
  if (!floor) {
    co_return std::unexpected(floor.error());
  }
  if (!allowed(principal, floor->at("siteId").get<std::string>(), true)
      || floor->value("kind", "") != "LOCATION_KIND_FLOOR" || floor->value("state", "") != "RESOURCE_STATE_ACTIVE") {
    co_return p::fail(Code::kAuthScopeDenied, "active floor outside write scope");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto reserved = co_await (*lease)->query(
      {.sql = "INSERT INTO catalog.files(tenant,id,floor,site,key,body) "
              "VALUES($1::uuid,$2::uuid,$3::uuid,$4,$5,$6::jsonb)",
       .parameters = {Param::text(principal.tenant), Param::text(file.at("fileId")), Param::text(file.at("ownerId")),
                      Param::text(floor->at("siteId")), Param::text(file.at("objectKey")), Param::text(file.dump())}});
  if (!reserved) {
    co_return std::unexpected(reserved.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<Json>> Repository::attach_file(Json file, double width, double height, Mutation mutation) {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 || width > 1000000
      || height > 1000000) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid plan dimensions");
  }
  co_return co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this, &file, width, height, &mutation](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        auto locked = co_await lock(tx, mutation.principal.tenant);
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        auto old = co_await load(tx, mutation.principal.tenant, "location", file.at("ownerId"));
        if (!old) {
          co_return old;
        }
        if (!allowed(mutation.principal, old->at("siteId").get<std::string>(), true)) {
          co_return p::fail(Code::kAuthScopeDenied, "floor outside write scope");
        }
        if (old->value("kind", "") != "LOCATION_KIND_FLOOR" || old->value("state", "") != "RESOURCE_STATE_ACTIVE") {
          co_return p::fail(Code::kCommonInvalidArgument, "active floor is required");
        }
        auto valid_positions = co_await tx->connection().query(
            {.sql = "SELECT 1 FROM catalog.resources d JOIN catalog.resources z ON z.tenant=d.tenant AND "
                    "z.kind='location' "
                    "AND z.id::text=d.parent WHERE d.tenant=$1::uuid AND d.kind='device' AND z.parent=$2 AND "
                    "d.body->>'state'<>'RESOURCE_STATE_ARCHIVED' AND ((d.body->'position'->>'x')::double "
                    "precision>$3::double "
                    "precision OR (d.body->'position'->>'y')::double precision>$4::double precision) LIMIT 1",
             .parameters = {Param::text(mutation.principal.tenant), Param::text(file.at("ownerId")),
                            Param::text(std::to_string(width)), Param::text(std::to_string(height))}});
        if (!valid_positions) {
          co_return std::unexpected(valid_positions.error());
        }
        if (valid_positions->size()) {
          co_return p::fail(Code::kCatalogPositionInvalid, "new dimensions exclude existing device positions");
        }
        const auto previous_version =
            old->value("floorPlan", Json::object()).value("file", Json::object()).value("version", "0");
        file["version"] = std::to_string(std::stoull(previous_version) + 1);
        file["siteId"] = old->at("siteId");
        auto linked = co_await tx->connection().query(
            {.sql = "UPDATE catalog.files SET body=$4::jsonb,linked=true WHERE tenant=$1::uuid AND id=$2::uuid AND "
                    "floor=$3::uuid AND NOT linked AND expires>clock_timestamp() RETURNING id",
             .parameters = {Param::text(mutation.principal.tenant), Param::text(file.at("fileId")),
                            Param::text(file.at("ownerId")), Param::text(file.dump())}});
        if (!linked) {
          co_return std::unexpected(linked.error());
        }
        if (linked->size() != 1) {
          co_return p::fail(Code::kCommonConcurrentModification, "upload reservation expired");
        }
        (*old)["floorPlan"] = {{"imageRef", file.at("fileId")},
                               {"contentType", file.at("contentType")},
                               {"width", width},
                               {"height", height},
                               {"file", file}};
        auto resource = parse("location", *old);
        if (!resource) {
          co_return std::unexpected(resource.error());
        }
        auto saved = co_await apply(tx, std::move(*resource), mutation, std::chrono::system_clock::now());
        if (!saved) {
          co_return saved;
        }
        Resource const metadata{.kind = "stored_file",
                                .id = file.at("fileId"),
                                .site = file.at("siteId"),
                                .version = std::stoull(file.at("version").get<std::string>()),
                                .body = file};
        auto published = co_await publish(tx, metadata, mutation, std::chrono::system_clock::now());
        if (!published) {
          co_return std::unexpected(published.error());
        }
        co_return saved;
      });
}

asio::awaitable<p::Result<Json>> Repository::file(p::security::Principal principal, std::string floor,
                                                  std::uint64_t version) {
  auto location = co_await get(principal, "location", floor);
  if (!location) {
    co_return std::unexpected(location.error());
  }
  if (!location->contains("floorPlan")) {
    co_return p::fail(Code::kCommonNotFound, "floor has no plan");
  }
  if (version == 0) {
    co_return location->at("floorPlan").at("file");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query(
      {.sql = "SELECT body::text FROM catalog.files WHERE tenant=$1::uuid AND floor=$2::uuid AND linked AND "
              "body->>'version'=$3",
       .parameters = {Param::text(principal.tenant), Param::text(floor), Param::text(std::to_string(version))}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0U) {
    co_return p::fail(Code::kCommonNotFound, "floor plan version not found");
  }
  co_return Json::parse(rows->value(0, 0).value_or(""));
}

asio::awaitable<p::Result<Page>> Repository::files(p::security::Principal principal, std::string floor,
                                                   std::size_t size, std::string after) {
  auto location = co_await get(principal, "location", floor);
  if (!location) {
    co_return std::unexpected(location.error());
  }
  if (size == 0 || size > 1000 || (!after.empty() && !canonical_uuid(after))) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid pagination");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query(
      {.sql = "SELECT body::text,id::text FROM catalog.files WHERE tenant=$1::uuid AND floor=$2::uuid AND linked AND "
              "($3='' OR id::text>$3) ORDER BY id LIMIT $4::integer",
       .parameters = {Param::text(principal.tenant), Param::text(floor), Param::text(after),
                      Param::integer(static_cast<std::int64_t>(size + 1))}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  Page page;
  for (std::size_t i = 0; i < std::min(size, rows->size()); ++i) {
    page.items.push_back(Json::parse(rows->value(i, 0).value_or("")));
  }
  if (rows->size() > size) {
    page.next = std::string(rows->value(size - 1, 1).value_or(""));
  }
  co_return page;
}

asio::awaitable<p::Result<void>> Repository::cleanup_files(
    std::function<asio::awaitable<p::Result<void>>(std::string)> remove) {
  // The row lock prevents linking a reservation while its object is being removed.
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [&remove](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT tenant::text,id::text,key FROM catalog.files WHERE NOT linked AND "
                    "expires<=clock_timestamp() ORDER BY expires FOR UPDATE SKIP LOCKED LIMIT 16"});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        for (std::size_t i = 0; i < rows->size(); ++i) {
          auto removed = co_await remove(std::string(rows->value(i, 2).value_or("")));
          if (!removed) {
            continue;
          }
          auto erased = co_await tx->connection().query(
              {.sql = "DELETE FROM catalog.files WHERE tenant=$1::uuid AND id=$2::uuid AND NOT linked",
               .parameters = {Param::text(std::string(rows->value(i, 0).value_or(""))),
                              Param::text(std::string(rows->value(i, 1).value_or("")))}});
          if (!erased) {
            co_return std::unexpected(erased.error());
          }
        }
        co_return p::Result<void>{};
      });
}
}  // namespace psim::resource_catalog
