// Worker of the crash tests: copies <input> to <output> with the processor; the test kills it
// with SIGKILL at random moments and checks the output for losses and duplicates.
//   psim_kafka_test_worker <alo|eos> <bootstrap> <input> <output> <group> <instance> <idle-exit-ms>
// idle-exit-ms > 0: stop cleanly after that long without messages; 0: run until killed.

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/log.hpp"

namespace asio = boost::asio;
namespace kafka = psim::platform::kafka;
using psim::platform::Result;
using std::chrono::milliseconds;

namespace {

struct Arguments {
  bool exactly_once = false;
  std::string bootstrap;
  std::string input;
  std::string output;
  std::string group;
  std::string instance;
  milliseconds idle_exit{0};
};

class Worker final : public kafka::Handler {
 public:
  explicit Worker(Arguments args)
      : args_(std::move(args)),
        logger_({.service = "kafka-test-worker",
                 .instance = args_.instance,
                 .level = psim::platform::log::Level::kInfo,
                 .trace_context = {}},
                std::make_shared<psim::platform::log::StdoutSink>()) {}

  asio::awaitable<Result<void>> handle(const kafka::Message* message, kafka::Output* out) override {
    last_message_ = std::chrono::steady_clock::now();
    co_return co_await out->send(
        {.topic = args_.output, .key = std::string(message->key()), .value = std::string(message->value())});
  }

  asio::awaitable<Result<void>> work(psim::platform::async::BlockingPool* pool) {
    const auto executor = co_await asio::this_coro::executor;
    psim::platform::async::SystemClock clock;
    const kafka::Dependencies deps{.blocking = pool, .logger = &logger_};
    const kafka::ClientOptions client{.bootstrap = args_.bootstrap, .client_id = args_.instance};
    auto consumer = kafka::Consumer::create(executor,
                                            {.client = client,
                                             .group_id = args_.group,
                                             .topics = {args_.input},
                                             .read_committed = true,
                                             .group_protocol = "consumer",
                                             .auto_offset_reset = "earliest"},
                                            deps);
    auto producer = kafka::Producer::create(
        executor, {.client = client, .transactional_id = args_.exactly_once ? args_.instance : std::string{}}, deps);
    if (!consumer || !producer) {
      co_return psim::platform::fail(psim::platform::ErrorCode::kCommonInternal, "cannot create the clients");
    }
    kafka::Processor processor(
        consumer->get(), producer->get(), &clock,
        {.stage = "crash-test",
         .semantics = args_.exactly_once ? kafka::Semantics::kExactlyOnce : kafka::Semantics::kAtLeastOnce,
         .max_batch = 200,
         .commit_interval = milliseconds(args_.exactly_once ? 50 : 200)},
        deps);
    last_message_ = std::chrono::steady_clock::now();
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator&&
    auto result = co_await (process(&processor) && stop_when_idle(&processor));
    logger_.info("worker stopping", {{"ok", result.has_value()}});
    co_await (*consumer)->close();
    co_return result;
  }

 private:
  asio::awaitable<Result<void>> process(kafka::Processor* processor) {
    auto result = co_await processor->run(this);
    finished_ = true;
    co_return result;
  }

  asio::awaitable<void> stop_when_idle(kafka::Processor* processor) {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    while (!finished_) {
      timer.expires_after(milliseconds(100));
      co_await timer.async_wait(asio::use_awaitable);
      if (args_.idle_exit.count() > 0 && std::chrono::steady_clock::now() - last_message_ > args_.idle_exit) {
        processor->stop();
        break;
      }
    }
  }

  Arguments args_;
  psim::platform::log::Logger logger_;
  std::chrono::steady_clock::time_point last_message_;
  bool finished_ = false;
};

int run(std::span<char*> arguments) {
  if (arguments.size() != 8) {
    std::println(stderr,
                 "usage: psim_kafka_test_worker <alo|eos> <bootstrap> <input> <output> <group> <instance> <idle-ms>");
    return 2;
  }
  Worker worker({.exactly_once = std::string_view(arguments[1]) == "eos",
                 .bootstrap = arguments[2],
                 .input = arguments[3],
                 .output = arguments[4],
                 .group = arguments[5],
                 .instance = arguments[6],
                 .idle_exit = milliseconds(std::stoll(arguments[7]))});
  asio::io_context io;
  psim::platform::async::BlockingPool pool(2, 16);
  auto result = asio::co_spawn(io, worker.work(&pool), asio::use_future);
  io.run();
  if (auto outcome = result.get(); !outcome) {
    std::println(stderr, "worker failed: {}", outcome.error().message());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception& e) {
    std::ignore = std::fputs(e.what(), stderr);
    return 1;
  } catch (...) {
    return 1;
  }
}
