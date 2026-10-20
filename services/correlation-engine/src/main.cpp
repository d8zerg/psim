#include <google/protobuf/util/time_util.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/common/v1/types.pb.h>

#include <expected>
#include <memory>
#include <string>
#include <utility>

#include "management.hpp"
#include "psim/domain/processing/alarm.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace psim::correlation {
namespace p = platform;

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "correlation", "psim.events.normalized.v1") {}

  boost::asio::awaitable<p::Result<void>> initialize() override { co_return co_await schema("psim.signals.v1"); }

  boost::asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* output) override {
    auto trace = p::stream::span(context(), "correlate", message->headers());
    auto event = decode<processing::v1::EventRecord>(*message);
    if (!event || !event->has_event()) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid normalized event");
    }
    if (!domain::processing::alarm_rule(event->event().type())) {
      trace->End();
      co_return p::Result<void>{};
    }
    processing::v1::SignalRecord record_value;
    auto* envelope = record_value.mutable_envelope();
    *envelope = event->envelope();
    const auto id =
        p::stream::uuid5("c8b07c6e-a8aa-5eb0-9744-705b51f8c211",
                         {envelope->tenant_id(), "66666666-6666-4666-8666-666666666666", "1", envelope->message_id()});
    envelope->set_causation_id(envelope->message_id());
    envelope->set_message_id(id);
    envelope->set_sequence(1);
    envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
    envelope->mutable_actor()->set_id("correlation-engine");
    *envelope->mutable_produced_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
    auto* signal = record_value.mutable_signal();
    signal->mutable_origin()->set_kind(processing::v1::SIGNAL_ORIGIN_KIND_RULE);
    signal->mutable_origin()->set_rule_id("66666666-6666-4666-8666-666666666666");
    signal->mutable_origin()->set_rule_version(1);
    const auto key =
        envelope->tenant_id() + ":" + event->event().site_id() + ":" + event->event().zone_id() + ":intrusion";
    signal->set_correlation_key(key);
    signal->set_grouping_key(key);
    signal->set_incident_type_id("77777777-7777-4777-8777-777777777777");
    signal->set_priority(common::v1::PRIORITY_P2);
    signal->set_site_id(event->event().site_id());
    signal->set_zone_id(event->event().zone_id());
    signal->add_event_ids(event->envelope().message_id());
    *signal->mutable_first_event_at() = event->envelope().occurred_at();
    *signal->mutable_last_event_at() = event->envelope().occurred_at();
    signal->set_summary("Intrusion alarm at Main entrance");
    auto headers = message->headers();
    p::kafka::inject(*trace, headers);
    auto result = co_await output->send(record("psim.signals.v1", key, record_value, std::move(headers)));
    trace->End();
    co_return result;
  }
};
}  // namespace psim::correlation

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "correlation-engine",
                                       .settings_schema = psim::correlation::management::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         if (context.config().get<std::string>("settings.role") == "management") {
                                           auto manager = psim::correlation::management::create(context);
                                           if (!manager) {
                                             return std::unexpected(manager.error());
                                           }
                                           result.push_back(std::move(*manager));
                                         } else {
                                           result.push_back(std::make_unique<psim::correlation::Worker>(context));
                                         }
                                         return result;
                                       }});
}
