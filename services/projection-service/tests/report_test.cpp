#include "report.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include <psim/api/v1/reports.pb.h>
#include <psim/common/v1/types.pb.h>
#include <psim/incident/v1/incident.pb.h>
#include <psim/response/v1/events.pb.h>

#include <chrono>

#include "facts.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::projection {
namespace {
constexpr auto kId = "11111111-1111-4111-8111-111111111111";

TEST(Reports, ValidatesSnapshotsAndPinnedTargets) {
  incident::v1::Incident i;
  EXPECT_FALSE(fact(kId, i));
  i.set_incident_id(kId);
  i.set_site_id(kId);
  i.set_incident_type_id(kId);
  i.set_version(2);
  i.mutable_created_at()->set_seconds(1000);
  i.set_priority(common::v1::PRIORITY_P2);
  i.set_state(incident::v1::INCIDENT_STATE_NEW);
  const auto f = fact(kId, i);
  ASSERT_TRUE(f);
  EXPECT_EQ(f->created, 1000000);
  i.mutable_acknowledged_at()->set_seconds(999);
  EXPECT_FALSE(fact(kId, i));
  response::v1::SlaPolicyPinned t;
  t.set_incident_id(kId);
  t.mutable_created_at()->set_seconds(1000);
  t.mutable_acknowledge_within()->set_seconds(10);
  t.mutable_resolve_within()->set_seconds(20);
  auto sla = fact(kId, t);
  ASSERT_TRUE(sla);
  EXPECT_EQ(sla->ack_target, 10000);
  EXPECT_EQ(sla->kind, 2);
  t.mutable_resolve_within()->set_seconds(-1);
  EXPECT_FALSE(fact(kId, t));
  EXPECT_TRUE(migration("bad;DROP").expand.empty());
  EXPECT_EQ(migration("psim").expand.size(), 4);
}

TEST(Reports, ParsesFiltersAndBindsScopeWithoutSqlInterpolation) {
  auto request = parse_request(
      "/v1/reports/"
      "incidents?filter.from=2020-01-01T00%3A00%3A00Z&filter.to=2020-01-02T00%3A00%3A00Z&grouping=REPORT_GROUPING_"
      "SITE");
  ASSERT_TRUE(request);
  const p::security::Principal principal{.subject = "user", .tenant = kId, .scopes = {std::string("zone:") + kId}};
  auto query = report_query(principal, *request);
  ASSERT_TRUE(query);
  EXPECT_EQ(query->tenant, kId);
  EXPECT_EQ(query->parameters.at("all"), "0");
  EXPECT_NE(query->parameters.at("zones").find(kId), std::string::npos);
  EXPECT_EQ(query->statement.find(kId), std::string::npos);
  EXPECT_FALSE(parse_request("/?filter.from=%00"));
  EXPECT_FALSE(parse_request("/?grouping=x&grouping=y"));
  EXPECT_FALSE(parse_request("/?tenant=evil"));
  request->mutable_filter()->add_site_ids("' OR 1=1");
  EXPECT_FALSE(report_query(principal, *request));
}

TEST(Reports, CsvQuotesAndNeutralizesSpreadsheetFormulas) {
  api::v1::GetIncidentReportResponse r;
  r.mutable_totals()->set_incidents_created(1);
  r.add_rows()->set_group("=1+1,\"x\"");
  auto text = csv(r);
  EXPECT_NE(text.find("\"'=1+1,\"\"x\"\"\""), std::string::npos);
  EXPECT_NE(text.find("\r\n"), std::string::npos);
}
}  // namespace

TEST(Reports, RejectsInvalidIntervalsAndExpiredIdentitiesBeforeDatabaseAccess) {
  auto request = parse_request("/?filter.from=2020-01-01T00:00:00Z&filter.to=2020-01-02T00:00:00Z");
  ASSERT_TRUE(request);
  p::security::Principal principal{.subject = "user",
                                   .tenant = kId,
                                   .roles = {"supervisor"},
                                   .scopes = {"*"},
                                   .expires_at = std::chrono::system_clock::now() + std::chrono::hours(1)};
  request->mutable_filter()->mutable_from()->set_seconds(-1);
  EXPECT_FALSE(report_query(principal, *request));
  request->mutable_filter()->mutable_from()->set_seconds(0);
  request->mutable_filter()->mutable_to()->set_seconds(10000000001LL);
  EXPECT_FALSE(report_query(principal, *request));
  EXPECT_FALSE(parse_request("/?filter.from=%zz"));
  EXPECT_FALSE(parse_request("/?filter.from"));
  EXPECT_FALSE(parse_request("/?grouping=INVALID"));
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure lives through context.run().
  const auto check = [&]() -> boost::asio::awaitable<void> {
    int denials = 0;
    Api api(nullptr,
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure owned by Api.
            [&denials](p::security::Principal, std::string, bool denied) -> boost::asio::awaitable<p::Result<void>> {
              if (denied) {
                ++denials;
              }
              co_return p::Result<void>{};
            });
    Request bad(http::verb::get, "/v1/reports/incidents?filter.from=invalid", 11);
    EXPECT_FALSE(co_await api.handle(bad, principal));
    principal.roles = {"operator"};
    auto refused = co_await api.handle(bad, principal);
    EXPECT_FALSE(refused);
    if (refused) {
      co_return;
    }
    EXPECT_EQ(Api::problem(refused.error()).result_int(), 403);
    EXPECT_EQ(denials, 1);
    principal.expires_at = std::chrono::system_clock::now() - std::chrono::hours(1);
    EXPECT_FALSE(co_await api.handle(bad, principal));
    bad.method(http::verb::post);
    EXPECT_FALSE(co_await api.handle(bad, principal));
  };
  boost::asio::io_context context;
  auto result = boost::asio::co_spawn(context, check(), boost::asio::use_future);
  context.run();
  result.get();
  EXPECT_FALSE(report(p::clickhouse::ResultSet{.rows = 10002}, true));
}
}  // namespace psim::projection
