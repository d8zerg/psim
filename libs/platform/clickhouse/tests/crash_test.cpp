#include <boost/asio/this_coro.hpp>
#include <clickhouse/columns/string.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../../kafka/tests/component_support.hpp"
#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/processing/v1/event.pb.h"
#include "support.hpp"

namespace psim::platform::clickhouse::testing {
namespace {
void worker(const std::string& table, const std::string& topic, const std::string& dlq, const std::string& group,
            int stage) {
  std::array<std::string, 6> values{PSIM_CH_TEST_WORKER, table, topic, dlq, group, std::to_string(stage)};
  std::array<char*, 7> arguments{values[0].data(), values[1].data(), values[2].data(), values[3].data(),
                                 values[4].data(), values[5].data(), nullptr};
  const int pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("cannot fork ClickHouse loader worker");
  }
  if (pid == 0) {
    ::execv(arguments[0], arguments.data());
    std::_Exit(127);
  }
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
  for (;;) {
    int status = 0;
    const int changed = ::waitpid(pid, &status, WNOHANG | WUNTRACED);  // NOLINT(misc-include-cleaner): POSIX wait flags
    if (changed == pid) {
      if (stage >= 0 && WIFSTOPPED(status)) {  // NOLINT(misc-include-cleaner): POSIX status macro
        EXPECT_EQ(::kill(pid, SIGKILL), 0);    // NOLINT(misc-include-cleaner): POSIX signal
        EXPECT_EQ(::waitpid(pid, &status, 0), pid);
        EXPECT_TRUE(WIFSIGNALED(status));  // NOLINT(misc-include-cleaner): POSIX status macro
        return;
      }
      EXPECT_LT(stage, 0) << "worker exited before checkpoint";
      EXPECT_TRUE(WIFEXITED(status));     // NOLINT(misc-include-cleaner): POSIX status macro
      EXPECT_EQ(WEXITSTATUS(status), 0);  // NOLINT(misc-include-cleaner): POSIX status macro
      return;
    }
    if (std::chrono::steady_clock::now() >= end || changed < 0) {
      ::kill(pid, SIGKILL);  // NOLINT(misc-include-cleaner): POSIX signal
      ::waitpid(pid, nullptr, 0);
      throw std::runtime_error("ClickHouse worker checkpoint timed out");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
}  // namespace

TEST(ClickHouseCrashRecovery, SigkillAtEveryBoundaryReplaysExactBatchAndCommitsDlqOnce) {
  for (int stage = 0; stage < 8; ++stage) {
    SCOPED_TRACE(stage);
    kafka::testing::Topics topics;
    const auto topic = topics.create("ch.crash.input", 1);
    const auto dlq = topics.create("ch.crash.dlq", 1);
    const auto group = kafka::testing::unique("ch.crash.group");
    const auto table = "chcrash_" + detail::fingerprint(group).substr(0, 20);
    auto setup = [](std::string table, std::string topic, bool rejected) -> asio::awaitable<void> {
      async::BlockingPool blocking(2, 8);
      Pool pool(&blocking, options());
      pg::Pool journal(journal_options());
      check(co_await Loader::initialize(&journal));
      co_await create(&pool, table);
      auto producer =
          take(kafka::Producer::create(co_await asio::this_coro::executor, {.client = kafka::testing::client()}));
      const kafka::ProtobufSerde<psim::processing::v1::EventRecord> serde(1);
      for (int index = 0; index < 25; ++index) {
        psim::processing::v1::EventRecord record;
        record.mutable_envelope()->set_tenant_id("tenant-a");
        record.mutable_envelope()->set_message_id(std::to_string(index));
        record.mutable_envelope()->mutable_occurred_at()->set_seconds(1);
        record.mutable_event()->set_raw_code(std::string("a\0b", 3));
        check(co_await producer->enqueue(
            {.topic = topic, .key = std::to_string(index), .value = serde.serialize(record), .partition = 0}));
      }
      if (rejected) {
        check(co_await producer->enqueue({.topic = topic, .key = "bad", .value = "invalid-protobuf", .partition = 0}));
      }
      check(co_await producer->flush(Duration(30000)));
    };
    run(setup(table, topic, stage < 4));
    worker(table, topic, dlq, group, stage);
    worker(table, topic, dlq, group, stage < 4 ? -1 : -2);
    auto verify = [](std::string table) -> asio::awaitable<void> {
      async::BlockingPool blocking(2, 8);
      Pool pool(&blocking, options());
      const auto rows = take(co_await pool.select(select(table, false)));
      EXPECT_EQ(rows.rows, 25);
      std::set<std::string> ids;
      for (const auto& data : rows.blocks) {
        for (std::size_t index = 0; index < data.GetRowCount(); ++index) {
          ids.emplace(data[1]->AsStrict<::clickhouse::ColumnString>()->At(index));
        }
      }
      EXPECT_EQ(ids.size(), 25);
      co_await drop(&pool, table);
    };
    run(verify(table));
    const auto rejected = kafka::testing::read_all(dlq, 2, std::chrono::seconds(5), true);
    ASSERT_EQ(rejected.size(), stage < 4 ? 1 : 0);
    if (!rejected.empty()) {
      EXPECT_EQ(rejected[0].second, "invalid-protobuf");
    }
  }
}
}  // namespace psim::platform::clickhouse::testing
