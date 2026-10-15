#include <google/protobuf/util/time_util.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/common/v1/types.pb.h>

#include <memory>
#include <string>
#include <utility>

#include "psim/domain/processing/alarm.hpp"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"

namespace psim::normalizer {
namespace p = platform;

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "normalizer", "psim.ingest.raw.v1") {}

  boost::asio::awaitable<p::Result<void>> initialize() override {
    co_return co_await schema("psim.events.normalized.v1");
  }

  boost::asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* output) override {
    auto trace = p::stream::span(context(), "normalize", message->headers());
    const auto input = decode<ingest::v1::RawEventRecord>(*message);
    if (!input || !input->has_raw_event() || input->envelope().tenant_id() != "11111111-1111-4111-8111-111111111111") {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid raw event");
    }
    const auto& raw = input->raw_event();
    if (raw.connector_id() != "22222222-2222-4222-8222-222222222222" || raw.source_ref() != "door-1") {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingDeviceNotFound, "source is absent from fixture catalog");
    }
    const auto classified = domain::processing::classify(raw.raw_code());
    if (!classified) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "unknown fixture mapping");
    }
    processing::v1::EventRecord event;
    *event.mutable_envelope() = input->envelope();
    *event.mutable_envelope()->mutable_produced_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
    event.mutable_envelope()->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
    event.mutable_envelope()->mutable_actor()->set_id("normalizer");
    auto* value = event.mutable_event();
    value->set_type(std::string(classified->type));
    value->set_taxonomy_version("1");
    value->set_event_class(classified->alarm ? common::v1::EVENT_CLASS_ALARM : common::v1::EVENT_CLASS_RESTORE);
    value->set_severity(classified->alarm ? common::v1::SEVERITY_HIGH : common::v1::SEVERITY_INFO);
    value->set_site_id("33333333-3333-4333-8333-333333333333");
    value->set_zone_id("44444444-4444-4444-8444-444444444444");
    value->set_device_id("55555555-5555-4555-8555-555555555555");
    value->set_source_id(raw.source_id());
    value->set_connector_id(raw.connector_id());
    value->set_source_epoch(raw.source_epoch());
    value->set_raw_code(raw.raw_code());
    value->set_significant(true);
    for (const auto& [key, text] : raw.attributes()) {
      (*value->mutable_attributes())["x." + key] = text;
    }
    auto headers = message->headers();
    p::kafka::inject(*trace, headers);
    auto result = co_await output->send(
        record("psim.events.normalized.v1", value->site_id() + ":" + value->zone_id(), event, std::move(headers)));
    trace->End();
    co_return result;
  }
};
}  // namespace psim::normalizer

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "normalizer",
                                       .settings_schema = std::string(psim::platform::stream::kSettings),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::normalizer::Worker>(context));
                                         return result;
                                       }});
}
