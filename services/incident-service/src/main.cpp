#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <psim/incident/v1/events.pb.h>
#include <psim/processing/v1/signal.pb.h>

#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"

namespace psim::incident_service {
namespace p = platform;

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "incident", "psim.signals.v1", p::kafka::Semantics::kAtLeastOnce),
        tick_(executor()),
        done_(executor()) {}

  boost::asio::awaitable<p::Result<void>> initialize() override {
    auto registered = co_await schema("psim.incidents.events.v1");
    if (!registered) {
      co_return registered;
    }
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    repository_ = std::make_unique<Repository>(pool_.get());
    auto initialized = co_await repository_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), repository_->messages(),
                                                  p::pg::kafka_publisher(producer()), 32);
    relaying_ = true;
    boost::asio::co_spawn(executor(), publish(), boost::asio::detached);
    co_return p::Result<void>{};
  }

  boost::asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message,
                                                 p::kafka::Output* /*output*/) override {
    auto trace = p::stream::span(context(), "persist incident and outbox", message->headers());
    auto signal = decode<processing::v1::SignalRecord>(*message);
    if (!signal || !signal->has_signal() || !p::stream::uuid(signal->envelope().tenant_id())
        || !p::stream::uuid(signal->envelope().message_id())) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid signal");
    }
    auto headers = message->headers();
    p::kafka::inject(*trace, headers);
    auto result = co_await repository_->apply(
        std::move(*signal), [this, headers](const incident::v1::IncidentEventRecord& event) {
          const auto& incident =
              event.has_incident_created() ? event.incident_created().incident() : event.signal_attached().incident();
          return record("psim.incidents.events.v1", incident.incident_id(), event, headers);
        });
    trace->End();
    co_return result;
  }

  boost::asio::awaitable<void> publish() {
    while (!stopping_) {
      auto result = co_await relay_->drain_once();
      if (!result) {
        context().logger().warn("outbox publish deferred");
      }
      tick_.expires_after(std::chrono::milliseconds(result ? 20 : 1000));
      co_await tick_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
    }
    relaying_ = false;
    done_.cancel();
  }

  boost::asio::awaitable<void> shutdown() override {
    stopping_ = true;
    tick_.cancel();
    while (relaying_) {
      done_.expires_at(boost::asio::steady_timer::time_point::max());
      co_await done_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
    }
    relay_.reset();
    repository_.reset();
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
  }

 private:
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  boost::asio::steady_timer tick_, done_;
  bool stopping_ = false, relaying_ = false;
};
}  // namespace psim::incident_service

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "incident-service",
                                       .settings_schema = std::string(psim::platform::stream::kSettings),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::incident_service::Worker>(context));
                                         return result;
                                       }});
}
