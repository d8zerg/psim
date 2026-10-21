#include <clickhouse/columns/string.h>
#include <gtest/gtest.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <string>

#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/metrics.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "support.hpp"

namespace psim::platform::clickhouse::testing {
TEST(ClickHouseValues, QueryRequiresTenantTimeIdentifiersAndBounds) {
  const PoolOptions config;
  auto query = select("events");
  EXPECT_TRUE(detail::select_query(query, config));
  query.tenant.clear();
  EXPECT_FALSE(detail::select_query(query, config));
  query = select("events");
  query.from_ms = query.to_ms;
  EXPECT_FALSE(detail::select_query(query, config));
  query.from_ms = -1;
  EXPECT_FALSE(detail::select_query(query, config));
  query = select("events; DROP DATABASE default");
  EXPECT_FALSE(detail::select_query(query, config));
  query = select("events");
  query.columns = {"count(*)"};
  EXPECT_FALSE(detail::select_query(query, config));
  query = select("events");
  query.time_column = "occurred_at OR 1=1";
  EXPECT_FALSE(detail::select_query(query, config));
  query = select("events");
  query.limit = 100001;
  EXPECT_FALSE(detail::select_query(query, config));
  query.limit = 0;
  EXPECT_FALSE(detail::select_query(query, config));
}

TEST(ClickHouseValues, NativeParametersKeepBinaryAndSqlInjectionOutOfText) {
  auto query = select("events");
  query.tenant = "' OR 1=1 --";
  query.equals.emplace("event_id", std::string("a\0b'\\", 6));
  auto native = take(detail::select_query(query, {}));
  EXPECT_EQ(native.GetText().find(query.tenant), std::string::npos);
  EXPECT_EQ(native.GetParams().at("psim_tenant"), query.tenant);
  EXPECT_EQ(native.GetParams().at("psim_value_0"), query.equals.at("event_id"));
  EXPECT_NE(native.GetText().find(" FINAL WHERE tenant_id"), std::string::npos);
  query.equals.emplace("event_id OR 1=1", "ignored");
  EXPECT_FALSE(detail::select_query(query, {}));
  EXPECT_FALSE(detail::identifier(""));
  EXPECT_FALSE(detail::identifier("1column"));
  EXPECT_FALSE(detail::identifier(std::string(129, 'x')));
  EXPECT_TRUE(detail::identifier("_event_1"));
}

TEST(ClickHouseValues, ScopedReadReservesScopeParametersAndRestrictsStatements) {
  Read query{.statement =
                 // psim-arch: allow-sql (negative test of statement validation)
             "SELECT tenant_id FROM events WHERE tenant_id={psim_tenant:String} AND "
             "occurred_at>=fromUnixTimestamp64Milli({psim_from:Int64}) AND "
             "occurred_at<fromUnixTimestamp64Milli({psim_to:Int64}) AND event_id={id:String}",
             .tenant = "tenant",
             .from_ms = 1,
             .to_ms = 2,
             .parameters = {{"id", "' OR 1=1 --"}}};
  auto native = detail::read_query(query, {});
  ASSERT_TRUE(native);
  EXPECT_EQ(native->GetText().find("' OR 1=1"), std::string::npos);
  EXPECT_EQ(native->GetParams().at("psim_tenant"), "tenant");
  query.parameters["psim_tenant"] = "other";
  EXPECT_FALSE(detail::read_query(query, {}));
  query.parameters.erase("psim_tenant");
  query.statement += "; DROP TABLE events";
  EXPECT_FALSE(detail::read_query(query, {}));
  // psim-arch: allow-sql (negative test of missing mandatory scope)
  query.statement = "SELECT 1";
  EXPECT_FALSE(detail::read_query(query, {}));
}

TEST(ClickHouseValues, NativeJournalRoundTripAndCorruptionBounds) {
  const auto data = block();
  const auto bytes = take(detail::encode(data, 4096));
  auto restored = take(detail::decode(bytes, 4096));
  EXPECT_EQ(restored.GetRowCount(), 2);
  EXPECT_EQ(restored[3]->AsStrict<::clickhouse::ColumnString>()->At(1), std::string("a\0b", 3));
  EXPECT_EQ(take(detail::encode(restored, 4096)), bytes);
  EXPECT_FALSE(detail::encode(data, 1));
  EXPECT_FALSE(detail::decode(bytes, 1));
  EXPECT_FALSE(detail::decode(bytes.substr(0, bytes.size() / 2), 4096));
  EXPECT_FALSE(detail::decode(bytes + "extra", 4096));
  EXPECT_FALSE(detail::decode("", 4096));
  EXPECT_EQ(detail::fingerprint("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(ClickHouseValues, ClosedAndInvalidPoolsFailBeforeNativeIo) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(1, 1);
    Pool pool(&blocking, {});
    EXPECT_FALSE(co_await pool.select(select("events")));
    EXPECT_FALSE(co_await pool.insert("events", block(), "bad-token"));
    EXPECT_FALSE(co_await pool.insert("bad;table", block(), detail::fingerprint("x")));
    EXPECT_FALSE(co_await pool.insert("events", {}, detail::fingerprint("x")));
    pool.close();
    EXPECT_FALSE(co_await pool.execute_ddl("SELECT 1"));
    Loader loader(nullptr, nullptr, nullptr, nullptr, {}, {});
    EXPECT_FALSE(co_await loader.run_once());
    EXPECT_FALSE(co_await Loader::initialize(nullptr));
    Migrator migrations(nullptr, nullptr, "");
    EXPECT_FALSE(co_await migrations.apply({}));
  };
  run(scenario());
}

TEST(ClickHouseValues, MetricsRegisterBoundedFamilies) {
  prometheus::Registry registry;
  Metrics metrics(registry);
  metrics.query(0.1, true);
  metrics.query(0.2, false);
  metrics.batch(5, 0.3, true);
  metrics.batch(5, 0.4, false);
  metrics.lag(1.5);
  metrics.dead_letter();
  const auto text = prometheus::TextSerializer{}.Serialize(registry.Collect());
  EXPECT_NE(text.find("psim_clickhouse_errors_total 2"), std::string::npos);
  EXPECT_NE(text.find("psim_clickhouse_inserted_rows_total 5"), std::string::npos);
  EXPECT_NE(text.find("psim_clickhouse_ingest_lag_seconds 1.5"), std::string::npos);
}
}  // namespace psim::platform::clickhouse::testing
