#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/common/v1/envelope.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/replica.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/internal.pb.h"

namespace psim::normalizer {
namespace {
std::string settings() {
  auto schema = nlohmann::json::parse(p::stream::kSettings);
  schema["properties"]["taxonomy_file"] = {{"type", "string"}, {"default", "/usr/share/psim/taxonomy.yaml"}};
  schema["properties"]["max_replica_entries"] = {
      {"type", "integer"}, {"minimum", 1}, {"maximum", 10000000}, {"default", 1000000}};
  schema["properties"]["transaction_gap_ms"] = {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000}, {"default", 5}};
  return schema.dump();
}
}  // namespace

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "normalizer", "psim.ingest.raw.v1") {}

  std::string consumer_group() const override { return "psim-normalizer"; }

  std::size_t batch_size() const override { return 16; }

  std::chrono::milliseconds commit_interval() const override { return std::chrono::milliseconds(5); }

  std::chrono::milliseconds transaction_gap() const override {
    return std::chrono::milliseconds(context().config().get<std::int64_t>("settings.transaction_gap_ms"));
  }

  boost::asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.events.normalized.v1", "psim.normalizer.state.v1"}) {
      auto registered = co_await schema(topic);
      if (!registered) {
        co_return registered;
      }
    }
    const auto path = context().config().get<std::string>("settings.taxonomy_file");
    auto taxonomy = co_await context().blocking().run([path] { return load_taxonomy(path); });
    if (!taxonomy) {
      co_return std::unexpected(taxonomy.error());
    }
    model_ = std::make_unique<Model>(std::move(*taxonomy),
                                     context().config().get<std::size_t>("settings.max_replica_entries"));
    auto replica = co_await p::stream::Replica::open(
        executor(), kafka_options("normalizer-control"),
        {.blocking = &context().blocking(), .logger = &context().logger()}, {"psim.catalog.v1", "psim.config.v1"},
        [this](const p::kafka::Message& message) -> p::Result<void> {
          if (message.value().empty()) {
            return p::fail(p::ErrorCode::kProcessingPayloadInvalid,
                           "catalog and configuration require versioned archive records");
          }
          if (message.topic() == "psim.catalog.v1") {
            auto value = decode<catalog::v1::CatalogRecord>(message);
            return value ? model_->catalog(*value) : std::unexpected(value.error());
          }
          auto value = decode<config::v1::ConfigRecord>(message);
          return value ? model_->config(*value) : std::unexpected(value.error());
        });
    if (!replica) {
      co_return std::unexpected(replica.error());
    }
    control_ = std::move(*replica);
    auto& registry = context().metrics().registry();
    normalized_ = &prometheus::BuildCounter()
                       .Name("psim_normalizer_events_total")
                       .Help("Normalized events staged in Kafka transactions")
                       .Register(registry)
                       .Add({});
    duplicates_ = &prometheus::BuildCounter()
                       .Name("psim_normalizer_duplicates_total")
                       .Help("Source duplicates discarded")
                       .Register(registry)
                       .Add({});
    latency_ = &prometheus::BuildHistogram()
                    .Name("psim_normalizer_processing_seconds")
                    .Help("Normalization handler duration")
                    .Register(registry)
                    .Add({}, prometheus::Histogram::BucketBoundaries{0.0001, 0.001, 0.005, 0.01, 0.03, 0.1});
    commits_ =
        &prometheus::BuildHistogram()
             .Name("psim_normalizer_commit_seconds")
             .Help("Cursor preparation and Kafka transaction commit duration")
             .Register(registry)
             .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.002, 0.005, 0.01, 0.02, 0.03, 0.05, 0.1, 1});
    co_return p::Result<void>{};
  }

  void before_poll() override {
    batch_started_ns_ =
        std::chrono::duration_cast<std::chrono::nanoseconds>(context().clock().wall().time_since_epoch()).count();
  }

  void assigned(const std::vector<p::kafka::TopicPartition>& partitions) override {
    for (const auto& partition : partitions) {
      restore_.insert(partition.partition);
    }
  }

  void revoked(const std::vector<p::kafka::TopicPartition>& partitions) override {
    for (const auto& partition : partitions) {
      restore_.erase(partition.partition);
      model_->revoke(partition.partition);
    }
  }

  bool commit_on_revoke() const override { return false; }

  boost::asio::awaitable<p::Result<void>> before_commit(p::kafka::Output* output) override {
    commit_started_ = std::chrono::steady_clock::now();
    for (const auto& [partition, value] : model_->pending_records()) {
      const auto& state = value.source_state();
      auto change =
          record("psim.normalizer.state.v1",
                 value.envelope().tenant_id() + "/" + state.source_id() + ":" + std::to_string(state.source_epoch()),
                 value, {});
      change.partition = partition;
      auto sent = co_await output->send(std::move(change));
      if (!sent) {
        co_return sent;
      }
    }
    co_return p::Result<void>{};
  }

  void committed() override {
    commits_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - commit_started_).count());
    model_->commit();
  }

  void aborted() override { model_->abort(); }

  boost::asio::awaitable<p::Result<void>> before_batch() override {
    auto updated = co_await control_->update();
    if (!updated) {
      co_return updated;
    }
    if (!restore_.empty()) {
      const std::vector<std::int32_t> partitions(restore_.begin(), restore_.end());
      auto replica = co_await p::stream::Replica::open(
          executor(), kafka_options("normalizer-restore"),
          {.blocking = &context().blocking(), .logger = &context().logger()}, {"psim.normalizer.state.v1"},
          [this](const p::kafka::Message& message) -> p::Result<void> {
            if (message.tombstone()) {
              return model_->forget(message.key(), message.partition());
            }
            auto state = decode<processing::v1::NormalizerStateRecord>(message);
            if (!state) {
              return std::unexpected(state.error());
            }
            return model_->restore(*state, message.partition());
          },
          partitions);
      if (!replica) {
        co_return std::unexpected(replica.error());
      }
      co_await (*replica)->close();
      restore_.clear();
    }
    co_return p::Result<void>{};
  }

  boost::asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* output) override {
    const auto started = std::chrono::steady_clock::now();
    auto trace = p::stream::span(context(), "normalize", message->headers());
    const auto input = decode<ingest::v1::RawEventRecord>(*message);
    if (!input) {
      trace->End();
      co_return std::unexpected(input.error());
    }
    if (message->key() != input->raw_event().source_id()) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "raw source key does not match its payload");
    }
    auto prepared = model_->prepare(*input);
    if (!prepared) {
      trace->End();
      co_return std::unexpected(prepared.error());
    }
    if (!*prepared) {
      duplicates_->Increment();
      trace->End();
      co_return p::Result<void>{};
    }
    auto& value = **prepared;
    const auto produced = google::protobuf::util::TimeUtil::GetCurrentTime();
    for (auto* envelope : {value.event.mutable_envelope(), value.state.mutable_envelope()}) {
      *envelope->mutable_produced_at() = produced;
      envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
      envelope->mutable_actor()->set_id("normalizer");
    }
    auto headers = message->headers();
    headers.emplace_back("psim-ts-normalizer-start", std::to_string(batch_started_ns_));
    p::kafka::inject(*trace, headers);
    auto sent = co_await output->send(record("psim.events.normalized.v1",
                                             value.event.event().site_id() + ":" + value.event.event().zone_id(),
                                             value.event, headers));
    if (sent) {
      model_->stage(std::move(value), message->partition());
      normalized_->Increment();
    }
    latency_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    trace->End();
    co_return sent;
  }

  boost::asio::awaitable<void> shutdown() override {
    if (control_) {
      co_await control_->close();
      control_.reset();
    }
    model_.reset();
  }

 private:
  std::unique_ptr<Model> model_;
  std::unique_ptr<p::stream::Replica> control_;
  std::set<std::int32_t> restore_;
  std::int64_t batch_started_ns_ = 0;
  std::chrono::steady_clock::time_point commit_started_{};
  prometheus::Histogram* commits_ = nullptr;
  prometheus::Counter* normalized_ = nullptr;
  prometheus::Counter* duplicates_ = nullptr;
  prometheus::Histogram* latency_ = nullptr;
};
}  // namespace psim::normalizer

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "normalizer",
                                       .settings_schema = psim::normalizer::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::normalizer::Worker>(context));
                                         return result;
                                       }});
}
