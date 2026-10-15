#include "../src/store.hpp"

#include <gtest/gtest.h>
#include <psim/connector/v1/connector.pb.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <vector>

namespace psim::platform::connector {
namespace {
class ConnectorStore : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() / detail::uuid7();
    options().buffer_directory = root_.string();
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  static wire::ConnectorEvent event() {
    wire::ConnectorEvent value;
    value.set_source_ref("door-1");
    value.set_raw_code("ALARM");
    return value;
  }

  Options& options() { return options_; }

 private:
  Options options_;
  std::filesystem::path root_;
};

TEST_F(ConnectorStore, RecoversIdentityOrderingAndAcknowledgementsAcrossRestart) {
  std::vector<wire::ConnectorEvent> events;
  {
    detail::Store store(options());
    for (int i = 0; i < 10; ++i) {
      events.push_back(store.append(event()));
      EXPECT_EQ(events.back().source_seq(), static_cast<std::uint64_t>(i + 1));
      EXPECT_EQ(events.back().event_id().at(14), '7');
    }
    store.acknowledge({events[0].event_id(), events[1].event_id()});
  }
  detail::Store store(options());
  const auto pending = store.pending(20, 65536);
  ASSERT_EQ(pending.size(), 8);
  for (std::size_t i = 0; i < pending.size(); ++i) {
    EXPECT_EQ(pending[i].SerializeAsString(), events[i + 2].SerializeAsString());
  }
  EXPECT_EQ(store.append(event()).source_seq(), 11);
  EXPECT_THROW(const detail::Store second(options()), std::runtime_error);
}

TEST_F(ConnectorStore, RecoversFsyncedEventsAfterProcessExitWithoutDestructors) {
  options().buffer_directory += "/nested/child";
  const auto child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    detail::Store store(options());
    for (int i = 0; i < 30; ++i) {
      (void)store.append(event());
    }
    ::_exit(0);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_EQ(status, 0);
  detail::Store store(options());
  EXPECT_EQ(store.count(), 30);
  EXPECT_EQ(store.append(event()).source_seq(), 31);
}

TEST_F(ConnectorStore, IncompleteTailIsRecoveredButChecksumCorruptionIsFatal) {
  {
    detail::Store store(options());
    (void)store.append(event());
  }
  const auto path = std::filesystem::path(options().buffer_directory) / "journal";
  {
    std::ofstream output(path, std::ios::app);
    output << "incomplete tail";
  }
  {
    detail::Store store(options());
    EXPECT_EQ(store.count(), 1);
    (void)store.append(event());
  }
  {
    std::fstream output(path, std::ios::in | std::ios::out);
    output.put('X');
  }
  EXPECT_THROW(const detail::Store store(options()), std::runtime_error);
}

TEST_F(ConnectorStore, BoundedBufferCompactsAndNeverOverwritesUnacknowledgedEvents) {
  options().max_buffer_bytes = 16384;
  options().max_pending_events = 3;
  {
    detail::Store store(options());
    for (int cycle = 0; cycle < 100; ++cycle) {
      auto value = store.append(event());
      store.acknowledge({value.event_id()});
    }
    EXPECT_LE(store.bytes(), options().max_buffer_bytes);
    (void)store.append(event());
    (void)store.append(event());
    (void)store.append(event());
    EXPECT_THROW((void)store.append(event()), std::invalid_argument);
    EXPECT_EQ(store.count(), 3);
  }
  const detail::Store store(options());
  EXPECT_EQ(store.count(), 3);
  EXPECT_EQ(store.cursors().at(0).source_seq(), 103);
}

TEST_F(ConnectorStore, CommandsAreDurableAndNeverExecuteAgainAfterUnknownOutcome) {
  wire::Command command;
  command.set_command_id(detail::uuid7());
  {
    detail::Store store(options());
    EXPECT_TRUE(store.claim(command).execute);
  }
  {
    detail::Store store(options());
    auto claim = store.claim(command);
    EXPECT_FALSE(claim.execute);
    EXPECT_EQ(claim.result.code(), "COMMAND_INVALID_STATE");
    claim.result.set_success(true);
    claim.result.clear_code();
    store.complete(claim.result);
  }
  detail::Store store(options());
  EXPECT_TRUE(store.claim(command).result.success());
  command.set_capability("different");
  EXPECT_EQ(store.claim(command).result.code(), "COMMON_IDEMPOTENCY_KEY_REUSED");
}

TEST_F(ConnectorStore, EpochCannotRegressAndInvalidEventsCannotConsumeSequence) {
  detail::Store store(options());
  auto value = event();
  value.set_source_epoch(10);
  EXPECT_EQ(store.append(value).source_seq(), 1);
  value.set_source_epoch(9);
  EXPECT_THROW((void)store.append(value), std::invalid_argument);
  value = event();
  value.set_event_id("caller-supplied");
  EXPECT_THROW((void)store.append(value), std::invalid_argument);
  value = event();
  value.mutable_occurred_at()->set_nanos(-1);
  EXPECT_THROW((void)store.append(value), std::invalid_argument);
  EXPECT_EQ(store.append(event()).source_seq(), 2);
  value = event();
  value.set_source_epoch(11);
  EXPECT_EQ(store.append(value).source_seq(), 1);
}

TEST_F(ConnectorStore, DiskQuotaRejectsNewEventsWithoutDiscardingPreviouslyAcceptedData) {
  options().max_buffer_bytes = 16384;
  std::vector<std::string> accepted;
  {
    detail::Store store(options());
    auto large = event();
    (*large.mutable_attributes())["payload"] = std::string(4096, 'x');
    for (int i = 0; i < 10; ++i) {
      try {
        accepted.push_back(store.append(large).event_id());
      } catch (const std::runtime_error&) {
        break;
      }
    }
    EXPECT_GT(accepted.size(), 0);
    EXPECT_LT(accepted.size(), 10);
    EXPECT_EQ(store.count(), accepted.size());
    EXPECT_LE(store.bytes(), options().max_buffer_bytes);
  }
  detail::Store store(options());
  const auto pending = store.pending(10, 65536);
  ASSERT_EQ(pending.size(), accepted.size());
  for (std::size_t i = 0; i < pending.size(); ++i) {
    EXPECT_EQ(pending[i].event_id(), accepted[i]);
  }
  store.acknowledge(accepted);
  EXPECT_EQ(store.append(event()).source_seq(), accepted.size() + 1);
}

TEST_F(ConnectorStore, CommandLedgerRemainsBoundedAndSurvivesCompaction) {
  options().max_buffer_bytes = 16384;
  options().max_commands = 1;
  wire::Command first;
  first.set_command_id(detail::uuid7());
  (*first.mutable_parameters())["payload"] = std::string(8000, 'x');
  {
    detail::Store store(options());
    auto claim = store.claim(first);
    ASSERT_TRUE(claim.execute);
    claim.result.set_success(true);
    claim.result.clear_code();
    claim.result.set_message(std::string(1024, 'x'));
    store.complete(claim.result);
    for (int i = 0; i < 80; ++i) {
      const auto value = store.append(event());
      store.acknowledge({value.event_id()});
    }
    wire::Command second;
    second.set_command_id(detail::uuid7());
    EXPECT_THROW((void)store.claim(second), std::runtime_error);
  }
  detail::Store store(options());
  const auto duplicate = store.claim(first);
  EXPECT_FALSE(duplicate.execute);
  EXPECT_TRUE(duplicate.result.success());
}

}  // namespace
}  // namespace psim::platform::connector
