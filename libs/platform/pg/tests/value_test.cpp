#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "../src/sql.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "support.hpp"

namespace psim::platform::pg::testing {
TEST(PgValues, ParametersKeepBinaryZeroAndNullDistinctFromEmptyText) {
  EXPECT_EQ(Parameter::integer(-42).value, "-42");
  EXPECT_EQ(Parameter::boolean(true).value, "true");
  EXPECT_FALSE(Parameter::null(2950).value);
  EXPECT_EQ(Parameter::text("").value, "");
  const auto binary = Parameter::bytes(std::string("a\0b", 3));
  ASSERT_TRUE(binary.value);
  EXPECT_EQ(binary.value, std::string("a\0b", 3));
  EXPECT_TRUE(Parameter::bytes("x").binary);
}

TEST(PgValues, EmptyAndMovedResultsAreSafe) {
  Rows empty(nullptr);
  Rows moved(std::move(empty));
  moved = Rows(nullptr);
  EXPECT_EQ(moved.columns(), 0);
  EXPECT_EQ(moved.column_type(0), 0);
  EXPECT_TRUE(moved.column_name(0).empty());
  EXPECT_TRUE(moved.command().empty());
  EXPECT_EQ(moved.affected(), 0);
  EXPECT_FALSE(moved.value(0, 0));
  EXPECT_FALSE(moved.integer(0, 0));
}

TEST(PgSql, SchemaIdentifiersRejectInjectionAndOnlyReplaceIdentifierMarkers) {
  EXPECT_FALSE(detail::identifier("schema; DROP TABLE users"));
  EXPECT_FALSE(detail::identifier("UPPER"));
  EXPECT_FALSE(detail::identifier(""));
  EXPECT_FALSE(detail::identifier(std::string(64, 'a')));
  EXPECT_TRUE(detail::identifier("service_1"));
  EXPECT_FALSE(detail::schema_sql("schema-name", "SELECT 1"));
  EXPECT_EQ(take(detail::schema_sql("service_1", "SELECT $1 FROM @schema@.a JOIN @schema@.b ON TRUE")),
            "SELECT $1 FROM \"service_1\".a JOIN \"service_1\".b ON TRUE");
  EXPECT_EQ(detail::fingerprint("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(PgValidation, InvalidAndClosedResourcesFailBeforeNetworkAccess) {
  auto scenario = []() -> asio::awaitable<void> {
    auto opened = co_await Connection::connect({.host = ""});
    EXPECT_FALSE(opened);
    EXPECT_EQ(opened.error().code(), ErrorCode::kCommonInvalidArgument);
    Pool invalid({.size = 0});
    EXPECT_FALSE(co_await invalid.acquire());
    Pool closed({});
    closed.close();
    EXPECT_EQ(closed.idle(), 0);
    EXPECT_FALSE(co_await closed.acquire());
    EXPECT_FALSE(co_await Transaction::begin(nullptr));
    EXPECT_FALSE(co_await Migrator("bad").apply(nullptr, {}));
    MessageStore store("bad");
    EXPECT_FALSE(co_await store.initialize(nullptr));
    EXPECT_FALSE(co_await store.accept(nullptr, "", "", ""));
    EXPECT_FALSE(co_await store.append(nullptr, {}));
    OutboxRelay relay(nullptr, store, {});
    EXPECT_FALSE(co_await relay.drain_once());
    EXPECT_FALSE(co_await relay.run(nullptr));
    const auto publisher = kafka_publisher(nullptr);
    EXPECT_FALSE(co_await publisher({}));
  };
  run(scenario());
}
}  // namespace psim::platform::pg::testing
