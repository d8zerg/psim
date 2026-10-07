// DoD of step 2.3: no losses and exactly-once output when the processing process is killed at
// random moments (SIGKILL: no drain, no commit, an open transaction left behind).

#include <gtest/gtest.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "component_support.hpp"

// NOLINTNEXTLINE(readability-redundant-declaration,cppcoreguidelines-avoid-non-const-global-variables): POSIX
extern char** environ;

namespace psim::platform::kafka::testing {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr int kMessages = 20000;
constexpr int kKills = 4;

pid_t spawn_worker(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  std::string path = PSIM_KAFKA_TEST_WORKER;
  argv.push_back(path.data());
  std::vector<std::string> copies = args;
  for (auto& arg : copies) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    throw std::runtime_error("cannot start the worker");
  }
  return pid;
}

/// Exit code of the worker; a worker still running after `timeout` is killed and gives -1.
int wait_exit(pid_t pid, std::chrono::seconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (::waitpid(pid, &status, WNOHANG) == 0) {  // NOLINT(misc-include-cleaner): <sys/wait.h> macro
    if (std::chrono::steady_clock::now() > deadline) {
      ::kill(pid, SIGKILL);  // NOLINT(misc-include-cleaner): POSIX kill from <signal.h>
      ::waitpid(pid, &status, 0);
      return -1;
    }
    std::this_thread::sleep_for(milliseconds(100));
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;  // NOLINT(misc-include-cleaner): <sys/wait.h> macros
}

/// Kill the worker kKills times at random moments, then let a last one finish the input.
void run_with_crashes(const std::string& mode, const std::string& input, const std::string& output) {
  std::mt19937 random{std::random_device{}()};
  std::uniform_int_distribution<int> lifetime(4000, 12000);
  const auto group = unique("crash-group");
  const auto instance = "crash-" + std::to_string(random());
  for (int kill = 0; kill < kKills; ++kill) {
    const pid_t pid = spawn_worker({mode, environment().kafka, input, output, group, instance, "0"});
    std::this_thread::sleep_for(milliseconds(lifetime(random)));
    ::kill(pid, SIGKILL);  // NOLINT(misc-include-cleaner): POSIX kill from <signal.h>
    wait_exit(pid);
  }
  // The partitions of the last killed member return after the group session timeout (10 s in the
  // local environment), so the last worker waits longer than that before it stops as idle.
  const pid_t last = spawn_worker({mode, environment().kafka, input, output, group, instance, "15000"});
  EXPECT_EQ(wait_exit(last, std::chrono::seconds(240)), 0);
}

TEST(KafkaCrash, ExactlyOnceSurvivesKilledProcesses) {
  Topics topics;
  const auto input = topics.create("eos-in", 4);
  const auto output = topics.create("eos-out", 4);
  produce_numbers(input, kMessages);
  run_with_crashes("eos", input, output);

  const auto counts = histogram(read_all(output, kMessages + 1, seconds(20), true));
  EXPECT_EQ(counts.size(), static_cast<std::size_t>(kMessages)) << "lost messages";
  int duplicates = 0;
  for (const auto& [value, count] : counts) {
    duplicates += count - 1;
  }
  EXPECT_EQ(duplicates, 0);
  // Uncommitted output of the killed transactions is there, but invisible to read_committed.
  EXPECT_GE(read_all(output, kMessages + 1, seconds(10), false).size(), static_cast<std::size_t>(kMessages));
}

TEST(KafkaCrash, AtLeastOnceLosesNothingWhenProcessesAreKilled) {
  Topics topics;
  const auto input = topics.create("alo-crash-in", 4);
  const auto output = topics.create("alo-crash-out", 4);
  produce_numbers(input, kMessages);
  run_with_crashes("alo", input, output);

  const auto counts = histogram(read_all(output, static_cast<std::size_t>(kMessages) * 2, seconds(15), true));
  EXPECT_EQ(counts.size(), static_cast<std::size_t>(kMessages)) << "lost messages";
}

}  // namespace
}  // namespace psim::platform::kafka::testing
