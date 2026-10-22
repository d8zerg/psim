#include "rebuild.hpp"

#include <google/protobuf/util/json_util.h>
#include <nlohmann/json.hpp>
#include <psim/incident/v1/incident.pb.h>
#include <psim/response/v1/events.pb.h>
#include <psim/response/v1/plan.pb.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "facts.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::projection {
namespace asio = boost::asio;
using Param = p::pg::Parameter;

asio::awaitable<p::Result<std::size_t>> rebuild(p::pg::Pool* source, p::clickhouse::Pool* destination,
                                                std::string tenant, std::int64_t from, std::int64_t to) {
  if (!p::stream::uuid(tenant) || from < 0 || from >= to || to > 10000000000000LL) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid rebuild scope");
  }
  auto lease = co_await source->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto tx = co_await p::pg::Transaction::begin(&**lease, p::pg::Isolation::kRepeatableRead);
  if (!tx) {
    co_return std::unexpected(tx.error());
  }
  auto read_only = co_await (*lease)->query({.sql = "SET TRANSACTION READ ONLY"});
  if (!read_only) {
    co_return std::unexpected(read_only.error());
  }
  std::string cursor = "00000000-0000-0000-0000-000000000000";
  std::size_t count = 0;
  const auto generation = p::stream::uuid7();
  for (;;) {
    auto rows = co_await (*lease)->query(
        {.sql = R"sql(SELECT i.incident_id::text,i.body::text,e.policy::text,e.priority
FROM incident.incidents i LEFT JOIN escalation.incidents e ON e.tenant=i.tenant_id AND e.id=i.incident_id
WHERE i.tenant_id=$1::uuid AND i.incident_id>$2::uuid
AND (i.body->>'createdAt')::timestamptz>=to_timestamp($3::double precision/1000)
AND (i.body->>'createdAt')::timestamptz<to_timestamp($4::double precision/1000)
ORDER BY i.incident_id LIMIT 200)sql",
         .parameters = {Param::text(tenant), Param::text(cursor), Param::integer(from), Param::integer(to)}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    if (rows->size() == 0) {
      break;
    }
    std::vector<Fact> facts;
    std::string token_input = generation + tenant;
    for (std::size_t row = 0; row < rows->size(); ++row) {
      token_input +=
          std::string(rows->value(row, 1).value_or("")) + ":" + std::string(rows->value(row, 2).value_or(""));
      incident::v1::Incident i;
      if (!google::protobuf::util::JsonStringToMessage(std::string(rows->value(row, 1).value_or("")), &i).ok()) {
        co_return p::fail(p::ErrorCode::kCommonInternal, "invalid stored incident");
      }
      auto f = fact(tenant, i);
      if (!f) {
        co_return std::unexpected(f.error());
      }
      facts.push_back(std::move(*f));
      const auto policy_body = rows->value(row, 2);
      if (policy_body) {
        response::v1::EscalationPolicy policy;
        if (!google::protobuf::util::JsonStringToMessage(std::string(*policy_body), &policy).ok()) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "invalid pinned SLA policy");
        }
        const auto priority = rows->integer(row, 3);
        if (!priority) {
          co_return std::unexpected(priority.error());
        }
        const auto target = std::ranges::find(policy.sla(), *priority, &response::v1::SlaTarget::priority);
        if (target == policy.sla().end()) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "missing pinned SLA target");
        }
        response::v1::SlaPolicyPinned pinned;
        pinned.set_incident_id(i.incident_id());
        *pinned.mutable_created_at() = i.created_at();
        *pinned.mutable_acknowledge_within() = target->acknowledge_within();
        *pinned.mutable_resolve_within() = target->resolve_within();
        auto sla = fact(tenant, pinned);
        if (!sla) {
          co_return std::unexpected(sla.error());
        }
        facts.push_back(std::move(*sla));
      } else if (!i.escalation_policy_id().empty()) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable, "pinned SLA has not been persisted yet");
      }
    }
    auto data = block(facts);
    // A new rebuild repopulates lost materialized state; aggregate versions make repeated rows idempotent.
    auto token = p::stream::sha256(token_input);
    auto saved = co_await destination->insert("incident_facts", data, std::move(token));
    if (!saved) {
      co_return std::unexpected(saved.error());
    }
    count += rows->size();
    cursor = rows->value(rows->size() - 1, 0).value_or("");
  }
  auto committed = co_await tx->commit();
  if (!committed) {
    co_return std::unexpected(committed.error());
  }
  co_return count;
}
}  // namespace psim::projection
