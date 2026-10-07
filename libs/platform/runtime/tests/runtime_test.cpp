#include "psim/platform/runtime.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <gtest/gtest.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers): kill is POSIX
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): setenv, unsetenv are POSIX
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "psim/platform/config.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/log.hpp"

namespace {

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace fs = std::filesystem;
using psim::platform::ErrorCode;
using psim::platform::fail;
using psim::platform::Result;
using psim::platform::config::Config;
using psim::platform::config::Schema;
using psim::platform::config::Sources;
using psim::platform::log::MemorySink;
using psim::platform::runtime::Component;
using psim::platform::runtime::Components;
using psim::platform::runtime::configuration_schema;
using psim::platform::runtime::Context;
using psim::platform::runtime::Runtime;
using psim::platform::runtime::ServiceDefinition;

// Shared state observed by the tests; the component lives on the runtime thread.
struct Probe {
  std::atomic<bool> ready{true};
  std::atomic<bool> fail_start{false};
  std::atomic<bool> drained{false};
  std::atomic<int> reconfigured{0};
  std::chrono::milliseconds drain_time{std::chrono::milliseconds{50}};
};

class FakeComponent final : public Component {
 public:
  FakeComponent(const Context& context, Probe& probe) : executor_(context.executor()), probe_(probe) {}

  std::string_view name() const override { return "fake"; }

  asio::awaitable<Result<void>> start() override {
    if (probe_.fail_start) {
      co_return fail(ErrorCode::kCommonUnavailable, "dependency down");
    }
    co_return Result<void>{};
  }

  bool ready() const override { return probe_.ready; }

  asio::awaitable<void> drain() override {
    // Work in progress takes drain_time to finish.
    asio::steady_timer timer(executor_, probe_.drain_time);
    co_await timer.async_wait(asio::use_awaitable);
    probe_.drained = true;
  }

  void reconfigure(const Config& /*config*/) override { ++probe_.reconfigured; }

 private:
  asio::any_io_executor executor_;
  Probe& probe_;
};

ServiceDefinition definition(Probe& probe) {
  return ServiceDefinition{.name = "fake-service",
                           .settings_schema = R"({"type": "object", "additionalProperties": false})",
                           .create = [&probe](Context& context) -> Result<Components> {
                             Components components;
                             components.push_back(std::make_unique<FakeComponent>(context, probe));
                             return components;
                           }};
}

Sources sources(std::optional<fs::path> file, std::map<std::string, std::string> env = {}) {
  env.emplace("PSIM__ADMIN__LISTEN", "127.0.0.1:0");
  env.emplace("PSIM__SERVICE__INSTANCE", "fake-0");
  return Sources{.file = std::move(file), .environment = std::move(env)};
}

struct Started {
  std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>();
  std::unique_ptr<Runtime> runtime;
  std::thread thread;
  std::atomic<int> exit_code{-1};

  Started(ServiceDefinition service, const Sources& from) {
    auto schema = Schema::parse(configuration_schema(service.settings_schema)).value();
    auto config = psim::platform::config::load(schema, from).value();
    runtime = std::make_unique<Runtime>(std::move(service), std::move(schema), std::move(config), from, sink);
    thread = std::thread([this] { exit_code = runtime->run(); });
  }

  Started(const Started&) = delete;
  Started& operator=(const Started&) = delete;
  Started(Started&&) = delete;
  Started& operator=(Started&&) = delete;

  ~Started() {
    if (thread.joinable()) {
      runtime->request_stop();
      thread.join();
    }
  }

  int stop() {
    runtime->request_stop();
    thread.join();
    return exit_code;
  }

  int join() {
    thread.join();
    return exit_code;
  }

  unsigned short port() const {
    for (int i = 0; i < 500; ++i) {
      if (auto p = runtime->admin_port()) {
        return *p;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    throw std::runtime_error("admin endpoint did not start");
  }

  bool logged(std::string_view text) const {
    return std::ranges::any_of(sink->lines(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
  }
};

http::response<http::string_body> request(unsigned short port, std::string_view target,
                                          http::verb verb = http::verb::get) {
  asio::io_context io;
  asio::ip::tcp::socket socket(io);
  socket.connect({asio::ip::make_address("127.0.0.1"), port});
  http::request<http::string_body> req(verb, target, 11);
  req.set(http::field::host, "localhost");
  http::write(socket, req);
  boost::beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(socket, buffer, res);
  return res;
}

unsigned status_when(unsigned short port, std::string_view target, unsigned expected) {
  unsigned status = 0;
  for (int i = 0; i < 300 && status != expected; ++i) {
    status = request(port, target).result_int();
    if (status != expected) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
  }
  return status;
}

TEST(Runtime, ServesHealthMetricsAndInfoThenDrainsOnStop) {
  Probe probe;
  Started service(definition(probe), sources(std::nullopt));
  const auto port = service.port();

  EXPECT_EQ(status_when(port, "/health/ready", 200), 200U);
  EXPECT_EQ(request(port, "/health/live").result_int(), 200U);
  const auto metrics = request(port, "/metrics").body();
  EXPECT_NE(metrics.find("psim_runtime_ready 1"), std::string::npos) << metrics;
  EXPECT_NE(metrics.find(R"(psim_runtime_info{commit=)"), std::string::npos) << metrics;
  EXPECT_NE(request(port, "/info").body().find(R"("service":"fake-service")"), std::string::npos);
  EXPECT_EQ(request(port, "/no/such").result_int(), 404U);
  EXPECT_EQ(request(port, "/health/live", http::verb::post).result_int(), 405U);
  EXPECT_EQ(request(port, "/metrics", http::verb::head).body(), "");

  EXPECT_EQ(service.stop(), 0);
  EXPECT_TRUE(probe.drained);
  EXPECT_TRUE(service.logged("service stopping: draining"));
  EXPECT_TRUE(service.logged(R"("msg":"service stopped","exit_code":0)"));
  EXPECT_TRUE(service.logged(R"("service":"fake-service","instance":"fake-0")"));
}

TEST(Runtime, StartsTheConfiguredShards) {
  Probe probe;
  Started service(definition(probe), sources(std::nullopt, {{"PSIM__RUNTIME__SHARDS", "3"}}));
  const auto port = service.port();
  EXPECT_EQ(status_when(port, "/health/ready", 200), 200U);
  EXPECT_NE(request(port, "/metrics").body().find("psim_runtime_shards 3"), std::string::npos);
  EXPECT_EQ(service.stop(), 0);
  EXPECT_TRUE(service.logged(R"("shards":3)"));
}

TEST(Runtime, IsNotReadyUntilEveryComponentIs) {
  Probe probe;
  probe.ready = false;
  const Started service(definition(probe), sources(std::nullopt));
  const auto port = service.port();

  const auto body = [&] {
    return request(port, "/health/ready");
  };
  EXPECT_EQ(status_when(port, "/health/ready", 503), 503U);
  EXPECT_NE(body().body().find(R"("components":{"fake":false})"), std::string::npos) << body().body();

  probe.ready = true;
  EXPECT_EQ(status_when(port, "/health/ready", 200), 200U);
}

TEST(Runtime, ExitsWithOneWhenAComponentFailsToStart) {
  Probe probe;
  probe.fail_start = true;
  Started service(definition(probe), sources(std::nullopt));

  EXPECT_EQ(service.join(), 1);
  EXPECT_TRUE(service.logged(R"("msg":"component failed to start","component":"fake","code":"COMMON_UNAVAILABLE")"));
}

TEST(Runtime, ExitsWithThreeWhenDrainExceedsTheDeadline) {
  Probe probe;
  probe.drain_time = std::chrono::seconds{10};
  Started service(definition(probe), sources(std::nullopt, {{"PSIM__SHUTDOWN__DRAIN_TIMEOUT_MS", "100"}}));
  EXPECT_EQ(status_when(service.port(), "/health/ready", 200), 200U);

  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(service.stop(), 3);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{5});
  EXPECT_FALSE(probe.drained);
  EXPECT_TRUE(service.logged("drain timeout: work in progress may be lost"));
}

TEST(Runtime, ExitsWithOneWhenTheServiceCannotBeCreated) {
  auto service = ServiceDefinition{
      .name = "broken", .settings_schema = R"({"type": "object"})", .create = [](Context&) -> Result<Components> {
        return fail(ErrorCode::kCommonInternal, "bad wiring");
      }};
  Started started(std::move(service), sources(std::nullopt));
  EXPECT_EQ(started.join(), 1);
  EXPECT_TRUE(started.logged(R"("msg":"service failed to initialize","code":"COMMON_INTERNAL")"));
}

TEST(Runtime, ExitsWithOneWhenTheAdminAddressIsTaken) {
  asio::io_context io;
  const asio::ip::tcp::acceptor taken(io, {asio::ip::make_address("127.0.0.1"), 0});
  const auto port = taken.local_endpoint().port();
  Probe probe;
  Started service(definition(probe),
                  sources(std::nullopt, {{"PSIM__ADMIN__LISTEN", "127.0.0.1:" + std::to_string(port)}}));
  EXPECT_EQ(service.join(), 1);
  EXPECT_TRUE(service.logged("admin endpoint failed"));
}

class ConfigFile {
 public:
  explicit ConfigFile(const std::string& text)
      : path_(fs::temp_directory_path() / ("psim-runtime-" + std::to_string(::getpid()) + ".yaml")) {
    write(text);
  }

  ConfigFile(const ConfigFile&) = delete;
  ConfigFile& operator=(const ConfigFile&) = delete;
  ConfigFile(ConfigFile&&) = delete;
  ConfigFile& operator=(ConfigFile&&) = delete;

  ~ConfigFile() { fs::remove(path_); }

  void write(const std::string& text) const { std::ofstream(path_) << text; }

  [[nodiscard]] const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

TEST(Runtime, ReloadAppliesRunTimeSettingsAndRejectsInvalidOnes) {
  const ConfigFile file("log:\n  level: info\n");
  Probe probe;
  Started service(definition(probe), sources(file.path()));
  EXPECT_EQ(status_when(service.port(), "/health/ready", 200), 200U);

  file.write("log:\n  level: debug\nshutdown:\n  drain_timeout_ms: 1000\n");
  service.runtime->request_reload();
  for (int i = 0; i < 300 && probe.reconfigured == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  EXPECT_EQ(probe.reconfigured, 1);

  file.write("log:\n  level: chatty\n");
  service.runtime->request_reload();
  for (int i = 0; i < 300 && !service.logged("configuration reload rejected"); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  EXPECT_EQ(service.stop(), 0);

  EXPECT_TRUE(
      service.logged(R"("msg":"setting changed; takes effect after a restart","path":"shutdown.drain_timeout_ms")"));
  EXPECT_TRUE(service.logged(R"("msg":"configuration reload rejected","path":"log.level")"));
  EXPECT_EQ(probe.reconfigured, 1);
}

int run_main(std::vector<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& a : args) {
    argv.push_back(a.data());
  }
  Probe probe;
  return psim::platform::runtime::run(static_cast<int>(argv.size()), argv.data(), definition(probe));
}

TEST(Main, HandlesOptionsAndInvalidConfiguration) {
  EXPECT_EQ(run_main({"fake", "--version"}), 0);
  EXPECT_EQ(run_main({"fake", "--unknown"}), 2);
  EXPECT_EQ(run_main({"fake", "--check-config"}), 0);
  ::setenv("PSIM__LOG__LEVEL", "chatty", 1);  // NOLINT(concurrency-mt-unsafe): no other threads yet
  EXPECT_EQ(run_main({"fake", "--check-config"}), 2);
  ::unsetenv("PSIM__LOG__LEVEL");  // NOLINT(concurrency-mt-unsafe): no other threads yet
}

unsigned short free_port() {
  asio::io_context io;
  const asio::ip::tcp::acceptor acceptor(io, {asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

TEST(Main, StopsCleanlyOnSigterm) {
  const auto port = free_port();
  // NOLINTNEXTLINE(concurrency-mt-unsafe): before the service thread starts
  ::setenv("PSIM__ADMIN__LISTEN", ("127.0.0.1:" + std::to_string(port)).c_str(), 1);
  std::atomic<int> exit_code{-1};
  std::thread process([&] { exit_code = run_main({"fake"}); });

  unsigned status = 0;
  for (int i = 0; i < 500 && status != 200; ++i) {
    try {
      status = request(port, "/health/ready").result_int();
    } catch (const std::exception&) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
  }
  ASSERT_EQ(status, 200U);
  ::kill(::getpid(), SIGTERM);
  process.join();
  ::unsetenv("PSIM__ADMIN__LISTEN");  // NOLINT(concurrency-mt-unsafe): threads joined
  EXPECT_EQ(exit_code, 0);
}

}  // namespace
