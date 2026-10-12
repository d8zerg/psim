#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "support.hpp"
// Reuse the Kafka component fixture for topic lifecycle and acknowledged message reads.
#include "../../kafka/tests/component_support.hpp"

namespace psim::platform::pg::testing {

namespace {
constexpr int kNoHang = WNOHANG;  // NOLINT(misc-include-cleaner): POSIX macro from sys/wait.h

int start_worker(const std::string& schema, const std::string& topic, const std::string& stage,
                 const std::string& marker) {
  std::array<std::string, 5> values{PSIM_PG_TEST_WORKER, schema, topic, stage, marker};
  std::array<char*, 6> arguments{values[0].data(), values[1].data(), values[2].data(),
                                 values[3].data(), values[4].data(), nullptr};
  const int pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("cannot fork PostgreSQL crash-test worker");
  }
  if (pid == 0) {
    // Prepare all arguments before fork; only async-signal-safe calls run in the child.
    ::execv(arguments[0], arguments.data());
    std::_Exit(127);
  }
  return pid;
}

class Child {
 public:
  Child(const std::string& schema, const std::string& topic, const std::string& stage)
      : marker_("/tmp/" + schema + ".ready"), pid_(start_worker(schema, topic, stage, marker_)) {}

  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  Child(Child&&) = delete;
  Child& operator=(Child&&) = delete;

  ~Child() {
    if (pid_ > 0) {
      ::kill(pid_, SIGKILL);  // NOLINT(misc-include-cleaner): POSIX kill/SIGKILL from csignal
      ::waitpid(pid_, nullptr, 0);
    }
    std::error_code ignored;
    std::filesystem::remove(marker_, ignored);
  }

  void kill_at_checkpoint() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (!std::filesystem::exists(marker_) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!std::filesystem::exists(marker_)) {
      throw std::runtime_error("PostgreSQL worker did not reach its crash checkpoint");
    }
    ::kill(pid_, SIGKILL);  // NOLINT(misc-include-cleaner): POSIX kill/SIGKILL from csignal
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
    EXPECT_TRUE(WIFSIGNALED(status));      // NOLINT(misc-include-cleaner): POSIX macros provided by sys/wait.h
    EXPECT_EQ(WTERMSIG(status), SIGKILL);  // NOLINT(misc-include-cleaner): POSIX macros provided by sys/wait.h
    std::filesystem::remove(marker_);
  }

  void finish() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
    int status = 0;
    while (::waitpid(pid_, &status, kNoHang)
           == 0) {  // NOLINT(misc-include-cleaner): POSIX macros provided by sys/wait.h
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error("PostgreSQL worker recovery timed out");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    pid_ = -1;
    EXPECT_TRUE(WIFEXITED(status));     // NOLINT(misc-include-cleaner): POSIX macros provided by sys/wait.h
    EXPECT_EQ(WEXITSTATUS(status), 0);  // NOLINT(misc-include-cleaner): POSIX macros provided by sys/wait.h
  }

 private:
  std::string marker_;
  int pid_;
};

asio::awaitable<void> setup(std::string schema) {
  auto connection = co_await connect();
  check(co_await MessageStore(schema).initialize(connection.get()));
  take(co_await connection->query({.sql = sql(schema, "CREATE TABLE @schema@.effects (id text PRIMARY KEY)")}));
}

asio::awaitable<void> verify(std::string schema, std::int64_t effects, std::int64_t pending) {
  auto connection = co_await connect();
  auto rows =
      take(co_await connection->query({.sql = sql(schema,
                                                  "SELECT (SELECT count(*) FROM @schema@.effects), (SELECT count(*) "
                                                  "FROM @schema@.psim_outbox WHERE published_at IS NULL)")}));
  EXPECT_EQ(take(rows.integer(0, 0)), effects);
  EXPECT_EQ(take(rows.integer(0, 1)), pending);
}

asio::awaitable<std::size_t> commit_once(std::string schema, kafka::testing::Values messages) {
  auto connection = co_await connect();
  MessageStore store(schema);
  std::size_t unique = 0;
  for (const auto& [id, payload] : messages) {
    auto transaction = take(co_await Transaction::begin(connection.get()));
    if (take(co_await store.accept(&transaction, "downstream", id, payload))) {
      ++unique;
    }
    check(co_await transaction.commit());
  }
  co_return unique;
}
}  // namespace

TEST(PgCrashRecovery, CommittedOutboxSurvivesEveryPublicationBoundaryAndInboxDeduplicates) {
  for (const auto* stage : {"before_commit", "after_commit", "after_publish"}) {
    SCOPED_TRACE(stage);
    const auto schema = unique_schema();
    kafka::testing::Topics topics;
    const auto topic = topics.create("pg.outbox", 1);
    run(setup(schema));
    {
      Child child(schema, topic, stage);
      child.kill_at_checkpoint();
    }
    const bool uncommitted = std::string_view(stage) == "before_commit";
    run(verify(schema, uncommitted ? 0 : 25, uncommitted ? 0 : 25));
    {
      Child recovery(schema, topic, "finish");
      recovery.finish();
    }
    run(verify(schema, 25, 0));
    const std::size_t expected = std::string_view(stage) == "after_publish" ? 50 : 25;
    auto messages = kafka::testing::read_all(topic, expected, std::chrono::seconds{30}, false);
    EXPECT_EQ(messages.size(), expected);
    EXPECT_EQ(run(commit_once(schema, messages)), 25);
    EXPECT_EQ(run(commit_once(schema, messages)), 0);
    run(drop(schema));
  }
}
}  // namespace psim::platform::pg::testing
