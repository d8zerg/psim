#include "live_worker.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/config/v1/config.pb.h>
#include <psim/processing/v1/internal.pb.h>
#include <psim/processing/v1/signal.pb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "live.hpp"
#include "psim/audit/v1/audit.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/replica.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "rules.hpp"
#include "state_store.hpp"

namespace psim::correlation::live {
namespace {
namespace asio = boost::asio;
using Time = google::protobuf::util::TimeUtil;
using Changelog = processing::v1::CorrelationChangelogRecord;
constexpr auto kChangelog = "psim.correlation.changelog.v1";
constexpr auto kRepartition = "psim.correlation.repartition.v1";

struct EndSpan {
  explicit EndSpan(p::stream::Span value) : span(std::move(value)) {}

  EndSpan(const EndSpan&) = delete;
  EndSpan& operator=(const EndSpan&) = delete;
  EndSpan(EndSpan&&) = delete;
  EndSpan& operator=(EndSpan&&) = delete;
  p::stream::Span span;

  ~EndSpan() { span->End(); }
};

class Base : public p::stream::Worker {
 public:
  Base(const p::runtime::Context& context, std::string stage, std::string input, std::size_t shard = 0)
      : p::stream::Worker(context, std::move(stage), std::move(input), p::kafka::Semantics::kExactlyOnce, false,
                          shard) {}

  std::size_t batch_size() const override { return 512; }

  std::chrono::milliseconds commit_interval() const override { return std::chrono::milliseconds(10); }

  std::chrono::milliseconds transaction_gap() const override { return std::chrono::milliseconds(5); }

  std::chrono::milliseconds transaction_timeout() const override { return std::chrono::seconds(10); }

  bool commit_on_revoke() const override { return false; }

  asio::awaitable<p::Result<void>> before_commit(p::kafka::Output* /*output*/) override {
    commit_started_ = std::chrono::steady_clock::now();
    co_return p::Result<void>{};
  }

  void committed() override {
    commits_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - commit_started_).count());
  }

 protected:
  std::int64_t now() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(context().clock().wall().time_since_epoch()).count();
  }

  asio::awaitable<p::Result<void>> open_control() {
    const auto* role = name().starts_with("correlation-router") ? "router" : "engine";
    commits_ = &prometheus::BuildHistogram()
                    .Name(std::string("psim_correlation_") + role + "_commit_seconds")
                    .Help("State preparation and Kafka transaction commit duration")
                    .Register(context().metrics().registry())
                    .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.02, 0.05, 0.1, 1});
    auto taxonomy = co_await context().blocking().run(
        [path = context().config().get<std::string>("settings.taxonomy_file")] { return rules::load_types(path); });
    if (!taxonomy) {
      co_return std::unexpected(taxonomy.error());
    }
    model_ = std::make_unique<Control>(std::move(*taxonomy));
    auto replica = co_await p::stream::Replica::open(
        executor(), kafka_options(std::string(name()) + "-control"),
        {.blocking = &context().blocking(), .logger = &context().logger()},
        {"psim.catalog.v1", "psim.config.v1", "psim.audit.v1"},
        [this](const p::kafka::Message& message) -> p::Result<void> {
          if (message.value().empty()) {
            return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "control records must retain their versions");
          }
          if (message.topic() == "psim.catalog.v1") {
            auto value = decode<catalog::v1::CatalogRecord>(message);
            return value ? model_->catalog(*value) : std::unexpected(value.error());
          }
          if (message.topic() == "psim.config.v1") {
            auto value = decode<config::v1::ConfigRecord>(message);
            return value ? model_->config(*value) : std::unexpected(value.error());
          }
          auto value = decode<audit::v1::AuditEventRecord>(message);
          if (!value) {
            return std::unexpected(value.error());
          }
          const auto& entry = value->audit_entry();
          if (!entry.action().starts_with("catalog.maintenance.") || !entry.details().contains("snapshot")) {
            return {};
          }
          config::v1::ConfigRecord policy;
          *policy.mutable_envelope() = value->envelope();
          if (!google::protobuf::util::JsonStringToMessage(entry.details().at("snapshot"),
                                                           policy.mutable_maintenance_window())
                   .ok()) {
            return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid maintenance audit snapshot");
          }
          return model_->config(policy);
        });
    if (!replica) {
      co_return std::unexpected(replica.error());
    }
    control_ = std::move(*replica);
    co_return p::Result<void>{};
  }

  asio::awaitable<void> shutdown() override {
    if (control_) {
      co_await control_->close();
      control_.reset();
    }
    model_.reset();
  }

  Control& model() { return *model_; }

  const Control& model() const { return *model_; }

  p::stream::Replica& control() { return *control_; }

 private:
  std::unique_ptr<Control> model_;
  std::unique_ptr<p::stream::Replica> control_;
  prometheus::Histogram* commits_ = nullptr;
  std::chrono::steady_clock::time_point commit_started_{};
};

class Router final : public Base {
 public:
  explicit Router(const p::runtime::Context& context)
      : Base(context, "correlation-router", "psim.events.normalized.v1") {}

  std::string consumer_group() const override { return "psim-correlation-router"; }

  bool ready() const override { return Base::ready() && serving_.load(); }

  void assigned(const std::vector<p::kafka::TopicPartition>& partitions) override {
    partition_count_ = 0;
    if (!partitions.empty()) {
      serving_.store(true);
    }
  }

  void revoked(const std::vector<p::kafka::TopicPartition>& partitions) override {
    partition_count_ = 0;
    if (consumer()->assignment().size() <= partitions.size()) {
      serving_.store(false);
    }
  }

  asio::awaitable<p::Result<void>> initialize() override {
    if (auto registered = co_await schema(kRepartition); !registered) {
      co_return registered;
    }
    co_return co_await open_control();
  }

  asio::awaitable<p::Result<void>> before_batch() override {
    if (!producer()->in_transaction()) {
      co_return co_await control().update();
    }
    co_return p::Result<void>{};
  }

  bool has_pending_work() const override {
    if (now() < inventory_at_) {
      return false;
    }
    for (const auto& [tenant, set] : model().sets()) {
      if (!model().context(tenant).subjects.empty() && std::ranges::any_of(set.rules(), [](const auto& rule) {
            return rule.has_absence() && !rule.absence().has_trigger();
          })) {
        return true;
      }
    }
    return false;
  }

  asio::awaitable<p::Result<void>> on_idle(p::kafka::Output* output) override {
    if (partition_count_ == 0) {
      const auto bounds = co_await consumer()->end_offsets(std::chrono::seconds(5));
      if (!bounds) {
        co_return std::unexpected(bounds.error());
      }
      partition_count_ = bounds->size();
    }
    const auto partitions = partition_count_;
    if (partitions == 0) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "normalized topic has no partitions");
    }
    std::set<std::string> emitted;
    for (const auto& [tenant, set] : model().sets()) {
      for (const auto& rule : set.rules()) {
        if (!rule.has_absence() || rule.absence().has_trigger()) {
          continue;
        }
        const auto& context = model().context(tenant);
        auto compiled = model().compiled(tenant, rule);
        if (!compiled) {
          co_return std::unexpected(compiled.error());
        }
        for (auto subject : context.subjects) {
          auto selected = rules::selects(**compiled, subject, context, true);
          if (!selected) {
            co_return std::unexpected(selected.error());
          }
          if (!*selected) {
            continue;
          }
          const auto key = rules::correlation_key(subject, context, rule.correlation_scope());
          const auto id = state_key(tenant, rule, key);
          const auto owner = static_cast<std::int32_t>(std::hash<std::string>{}(id) % partitions);
          if (!consumer()->assignment().contains({"psim.events.normalized.v1", owner}) || !emitted.insert(id).second) {
            continue;
          }
          auto origin = Time::TimestampToMilliseconds(set.activated_at());
          if (subject.envelope().has_occurred_at() && Time::IsTimestampValid(subject.envelope().occurred_at())) {
            origin = std::max(origin, Time::TimestampToMilliseconds(subject.envelope().occurred_at()));
          }
          const auto window = Time::DurationToMilliseconds(rule.window());
          if (origin < now()) {
            origin += ((now() - origin) / window) * window;
          }
          auto* envelope = subject.mutable_envelope();
          *envelope->mutable_occurred_at() = Time::MillisecondsToTimestamp(origin);
          *envelope->mutable_produced_at() = Time::GetCurrentTime();
          envelope->set_message_id(p::stream::uuid5(rule.rule_id(), {key, std::to_string(origin), "inventory"}));
          envelope->set_correlation_id(envelope->message_id());
          envelope->set_sequence(1);
          envelope->set_schema_version("1");
          envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
          envelope->mutable_actor()->set_id("correlation-router");
          auto sent = co_await route(&subject, &rule, set.version(), key, true, output, {});
          if (!sent) {
            co_return sent;
          }
        }
      }
    }
    inventory_at_ = now() + 1000;
    co_return p::Result<void>{};
  }

  void aborted() override { inventory_at_ = 0; }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* output) override {
    auto event = decode<rules::Event>(*message);
    if (!event || !event->has_event() || message->key() != event->event().site_id() + ":" + event->event().zone_id()) {
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "normalized correlation input has an invalid key");
    }
    const auto found = model().sets().find(event->envelope().tenant_id());
    if (found == model().sets().end()) {
      co_return p::Result<void>{};
    }
    const auto& context = model().context(found->first);
    for (const auto& compiled : model().active(found->first)) {
      const auto& rule = compiled->rule;
      auto selected = rules::selects(*compiled, *event, context);
      if (!selected) {
        co_return std::unexpected(selected.error());
      }
      if (!*selected) {
        continue;
      }
      const auto key = rules::correlation_key(*event, context, rule.correlation_scope());
      if (key.empty()) {
        co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "correlation scope is unavailable");
      }
      auto sent = co_await route(&*event, &rule, found->second.version(), key, false, output, message->headers());
      if (!sent) {
        co_return sent;
      }
    }
    co_return p::Result<void>{};
  }

 private:
  asio::awaitable<p::Result<void>> route(const rules::Event* event, const rules::Rule* rule, std::uint64_t version,
                                         std::string key, bool inventory, p::kafka::Output* output,
                                         p::kafka::Headers headers) {
    processing::v1::RepartitionRecord value;
    *value.mutable_envelope() = event->envelope();
    auto* routed = value.mutable_repartitioned_event();
    routed->set_rule_id(rule->rule_id());
    routed->set_rule_version(rule->version());
    routed->set_rule_set_version(version);
    routed->set_correlation_key(key);
    routed->set_inventory(inventory);
    *routed->mutable_event() = event->event();
    *routed->mutable_rule() = *rule;
    co_return co_await output->send(record(kRepartition, key, value, std::move(headers)));
  }

  std::int64_t inventory_at_ = 0;
  std::size_t partition_count_ = 0;
  std::atomic_bool serving_{false};
};

class Engine final : public Base {
 public:
  Engine(const p::runtime::Context& context, std::size_t shard)
      : Base(context, "correlation", kRepartition, shard),
        shard_(shard),
        component_name_("correlation-" + std::to_string(shard)) {}

  std::string_view name() const override { return component_name_; }

  std::string consumer_group() const override { return "psim-correlation-engine"; }

  bool ready() const override { return Base::ready() && serving_.load(); }

  asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.signals.v1", kChangelog, "psim.audit.v1"}) {
      if (auto registered = co_await schema(topic); !registered) {
        co_return registered;
      }
    }
    auto& metrics = context().metrics().registry();
    late_ = &prometheus::BuildCounter()
                 .Name("psim_correlation_late_events_total")
                 .Help("Events arriving behind the watermark")
                 .Register(metrics)
                 .Add({});
    suppressed_ = &prometheus::BuildCounter()
                       .Name("psim_correlation_suppressed_total")
                       .Help("Decisions suppressed by versioned maintenance")
                       .Register(metrics)
                       .Add({});
    latency_ = &prometheus::BuildHistogram()
                    .Name("psim_correlation_processing_seconds")
                    .Help("Live correlation handler duration")
                    .Register(metrics)
                    .Add({}, prometheus::Histogram::BucketBoundaries{0.0001, 0.001, 0.005, 0.01, 0.05, 0.1});
    recovery_ = &prometheus::BuildHistogram()
                     .Name("psim_correlation_restore_seconds")
                     .Help("Assigned partition recovery duration")
                     .Register(metrics)
                     .Add({}, prometheus::Histogram::BucketBoundaries{0.01, 0.1, 1, 5, 10, 20, 30});
    persistence_ = &prometheus::BuildHistogram()
                        .Name("psim_correlation_store_seconds")
                        .Help("Committed local RocksDB write duration")
                        .Register(metrics)
                        .Add({}, prometheus::Histogram::BucketBoundaries{0.0001, 0.001, 0.005, 0.01, 0.05, 0.1});
    co_return co_await open_control();
  }

  void assigned(const std::vector<p::kafka::TopicPartition>& partitions) override {
    serving_.store(false);
    for (const auto& partition : partitions) {
      restoring_.insert(partition.partition);
    }
  }

  void revoked(const std::vector<p::kafka::TopicPartition>& partitions) override {
    serving_.store(false);
    for (const auto& partition : partitions) {
      restoring_.erase(partition.partition);
      closing_.insert(partition.partition);
      clocks_.erase(partition.partition);
      pending_clocks_.erase(partition.partition);
      std::vector<std::string> removed;
      for (const auto& [key, timer] : timer_keys_) {
        if (timer.partition == partition.partition) {
          removed.push_back(key);
        }
      }
      for (const auto& key : removed) {
        unschedule(key);
      }
      std::erase_if(cache_, [this, partition](const auto& entry) {
        if (entry.second.partition != partition.partition) {
          return false;
        }
        cache_bytes_ -= entry.second.value.ByteSizeLong();
        return true;
      });
    }
    pending_.clear();
    timers_.clear();
  }

  asio::awaitable<p::Result<void>> before_batch() override {
    if (!durable_.empty()) {
      const auto started = std::chrono::steady_clock::now();
      for (const auto& [partition, changes] : durable_) {
        auto applied = co_await context().blocking().run([this, partition, &changes] {
          return stores_.at(partition)->apply(changes, durable_checkpoints_.at(partition));
        });
        if (!applied) {
          co_return applied;
        }
      }
      durable_.clear();
      durable_checkpoints_.clear();
      persistence_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    }
    for (auto partition : closing_) {
      (void)co_await context().blocking().run([this, partition] { stores_.erase(partition); });
    }
    closing_.clear();
    if (!producer()->in_transaction()) {
      if (auto updated = co_await control().update(); !updated) {
        co_return updated;
      }
    }
    for (auto partition : restoring_) {
      if (auto restored = co_await restore(partition); !restored) {
        co_return restored;
      }
    }
    restoring_.clear();
    serving_.store(!consumer()->assignment().empty());
    if (rules_revision_ != model().revision()) {
      std::set<std::string> active;
      for (const auto& [tenant, set] : model().sets()) {
        for (const auto& rule : set.rules()) {
          active.insert(tenant + "/" + rule.rule_id() + "/" + std::to_string(rule.version()) + "/");
        }
      }
      for (auto& [key, timer] : timer_keys_) {
        const auto slash = key.find('/', key.find('/', key.find('/') + 1) + 1);
        if (!active.contains(key.substr(0, slash + 1))) {
          scheduled_.erase({timer.due, key});
          timer.due = now();
          scheduled_.insert({timer.due, key});
        }
      }
      rules_revision_ = model().revision();
    }
    if (!producer()->in_transaction()) {
      timers_.clear();
      for (auto it = scheduled_.begin(); it != scheduled_.end() && it->first <= now() && timers_.size() < 64; ++it) {
        const auto& key = it->second;
        const auto partition = timer_keys_.at(key).partition;
        const auto cached = cache_.find(key);
        if (cached != cache_.end()) {
          timers_.push_back(cached->second);
          continue;
        }
        auto bytes =
            co_await context().blocking().run([this, partition, &key] { return stores_.at(partition)->get(key); });
        if (!bytes || !*bytes) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "scheduled correlation state is unavailable");
        }
        Changelog value;
        if (!value.ParseFromString(**bytes)) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "invalid scheduled correlation state");
        }
        timers_.push_back({key, std::move(value), partition});
      }
    }
    co_return p::Result<void>{};
  }

  bool has_pending_work() const override { return !timers_.empty(); }

  asio::awaitable<p::Result<void>> on_idle(p::kafka::Output* output) override {
    for (auto& entry : timers_) {
      if (pending_.contains(entry.key)) {
        continue;
      }
      const auto waiting = durable_.find(entry.partition);
      if (waiting != durable_.end() && waiting->second.contains(entry.key)) {
        const auto& bytes = waiting->second.at(entry.key);
        if (!bytes) {
          continue;
        }
        if (!entry.value.ParseFromString(*bytes)) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "invalid committed timer state");
        }
      } else if (const auto cached = cache_.find(entry.key); cached != cache_.end()) {
        entry.value = cached->second.value;
      }
      State state;
      if (!state.ParseFromString(entry.value.state().state()) || entry.value.state().state_format() != 1) {
        co_return p::fail(p::ErrorCode::kCommonInternal, "unsupported local state encoding");
      }
      const auto& tenant = entry.value.envelope().tenant_id();
      const auto found = model().sets().find(tenant);
      const bool active =
          found != model().sets().end() && std::ranges::any_of(found->second.rules(), [&](const auto& rule) {
            return rule.rule_id() == state.rule().rule_id() && rule.version() == state.rule().version();
          });
      const auto expiry = std::max({model().options(tenant, {}).replay_age_ms, std::int64_t{60000},
                                    Time::DurationToMilliseconds(state.rule().window()),
                                    Time::DurationToMilliseconds(state.rule().suppression())});
      const bool expired = !state.has_subject() && state.pending().empty() && state.buffered().empty()
                           && state.candidates().empty() && now() - state.last_activity_ms() > expiry;
      const bool removed_subject =
          state.has_subject() && !std::ranges::any_of(model().context(tenant).subjects, [&](const auto& subject) {
            return rules::correlation_key(subject, model().context(tenant), state.rule().correlation_scope())
                   == entry.value.state().correlation_key();
          });
      if (!active || expired || removed_subject) {
        deletions_[entry.partition].insert(entry.key);
        continue;
      }
      auto compiled = model().compiled(tenant, state.rule());
      if (!compiled) {
        co_return std::unexpected(compiled.error());
      }
      const auto options = timed_options(entry.partition, tenant, state.rule());
      auto advanced = advance(**compiled, state, nullptr, model().context(tenant), tenant, now(), false, options);
      if (!advanced) {
        co_return std::unexpected(advanced.error());
      }
      if (auto published = co_await publish(std::move(advanced->decisions), entry.value.envelope(), output, {});
          !published) {
        co_return published;
      }
      entry.value.mutable_state()->set_state(state.SerializeAsString());
      const auto key = entry.key;
      pending_.emplace(key, std::move(entry));
    }
    timers_.clear();
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* output) override {
    const auto started = std::chrono::steady_clock::now();
    const EndSpan trace{p::stream::span(context(), "correlate", message->headers())};
    auto value = decode<processing::v1::RepartitionRecord>(*message);
    if (!value || !value->has_repartitioned_event()) {
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid repartitioned correlation input");
    }
    const auto& routed = value->repartitioned_event();
    const auto& rule = routed.rule();
    const auto& tenant = value->envelope().tenant_id();
    if (message->key() != routed.correlation_key() || rule.rule_id() != routed.rule_id()
        || rule.version() != routed.rule_version() || !p::stream::uuid(tenant)) {
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "repartitioned rule identity or key mismatch");
    }
    auto compiled = model().compiled(tenant, rule);
    if (!compiled) {
      co_return std::unexpected(compiled.error());
    }
    rules::Event event;
    *event.mutable_envelope() = value->envelope();
    *event.mutable_event() = routed.event();
    if (rules::correlation_key(event, model().context(tenant), rule.correlation_scope()) != routed.correlation_key()) {
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "correlation key differs from its event scope");
    }
    const auto key = state_key(tenant, rule, routed.correlation_key());
    const auto limit = context().config().get<std::size_t>("settings.max_live_keys");
    if (!timer_keys_.contains(key) && !pending_.contains(key) && timer_keys_.size() + pending_.size() >= limit) {
      co_return p::fail(p::ErrorCode::kCommonRateLimited, "live correlation key capacity exceeded");
    }
    State state;
    const Entry* previous = nullptr;
    if (const auto found = pending_.find(key); found != pending_.end()) {
      previous = &found->second;
    } else if (const auto found_cache = cache_.find(key); found_cache != cache_.end()) {
      previous = &found_cache->second;
    }
    if (previous != nullptr) {
      if (!state.ParseFromString(previous->value.state().state())) {
        co_return p::fail(p::ErrorCode::kCommonInternal, "invalid pending key state");
      }
    } else {
      p::Result<std::optional<std::string>> bytes;
      const auto waiting = durable_.find(message->partition());
      if (waiting != durable_.end() && waiting->second.contains(key)) {
        bytes = waiting->second.at(key);
      } else {
        bytes = co_await context().blocking().run(
            [this, partition = message->partition(), &key] { return stores_.at(partition)->get(key); });
      }
      if (!bytes) {
        co_return std::unexpected(bytes.error());
      }
      if (*bytes) {
        Changelog previous_value;
        if (!previous_value.ParseFromString(**bytes) || previous_value.state().state_format() != 1
            || !state.ParseFromString(previous_value.state().state())) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "invalid persisted key state");
        }
      }
    }
    *state.mutable_rule() = rule;
    state.set_rule_set_version(routed.rule_set_version());
    if (!routed.inventory() && event.envelope().has_occurred_at() && event.envelope().has_received_at()
        && Time::IsTimestampValid(event.envelope().occurred_at())
        && Time::IsTimestampValid(event.envelope().received_at())) {
      if (!pending_clocks_.contains(message->partition())) {
        pending_clocks_[message->partition()] = clocks_[message->partition()];
      }
      auto& source = pending_clocks_[message->partition()][tenant][event.event().source_id()];
      const auto options = model().options(tenant, {});
      const auto receipt = Time::TimestampToMilliseconds(event.envelope().received_at());
      auto at = Time::TimestampToMilliseconds(event.envelope().occurred_at());
      if (at > receipt + options.clock_skew_ms) {
        at = receipt;
      }
      source.set_maximum_ms(std::max(source.maximum_ms(), at));
      source.set_received_ms(std::max(source.received_ms(), now()));
    }
    auto advanced = advance(**compiled, state, &event, model().context(tenant), tenant, now(), routed.inventory(),
                            timed_options(message->partition(), tenant, rule));
    if (!advanced) {
      co_return std::unexpected(advanced.error());
    }
    if (advanced->late) {
      late_->Increment();
    }
    auto headers = message->headers();
    p::kafka::inject(*trace.span, headers);
    if (auto sent = co_await publish(std::move(advanced->decisions), value->envelope(), output, headers); !sent) {
      co_return sent;
    }
    Changelog change;
    *change.mutable_envelope() = value->envelope();
    auto* entry = change.mutable_state();
    entry->set_rule_id(rule.rule_id());
    entry->set_rule_version(rule.version());
    entry->set_correlation_key(routed.correlation_key());
    entry->set_state_format(1);
    entry->set_state(state.SerializeAsString());
    pending_.insert_or_assign(key, Entry{key, std::move(change), message->partition()});
    deletions_[message->partition()].erase(key);
    latency_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> before_commit(p::kafka::Output* output) override {
    (void)co_await Base::before_commit(output);
    prepared_.clear();
    for (const auto& [key, entry] : pending_) {
      auto change = record(kChangelog, key, entry.value, {});
      change.partition = entry.partition;
      if (auto sent = co_await output->send(std::move(change)); !sent) {
        co_return sent;
      }
      prepared_[entry.partition][key] = entry.value.SerializeAsString();
    }
    for (const auto& [partition, keys] : deletions_) {
      for (const auto& key : keys) {
        if (auto sent = co_await output->send({.topic = kChangelog, .key = key, .partition = partition}); !sent) {
          co_return sent;
        }
        prepared_[partition][key] = std::nullopt;
      }
    }
    for (const auto& [partition, changes] : prepared_) {
      auto marker = co_await producer()->send({.topic = kChangelog, .key = "__checkpoint", .partition = partition});
      if (!marker) {
        co_return std::unexpected(marker.error());
      }
      checkpoints_[partition] = marker->offset + 1;
    }
    co_return p::Result<void>{};
  }

  void committed() override {
    Base::committed();
    for (auto& [partition, tenants] : pending_clocks_) {
      clocks_[partition] = std::move(tenants);
    }
    pending_clocks_.clear();
    for (auto& [partition, changes] : prepared_) {
      for (auto& [key, value] : changes) {
        durable_[partition].insert_or_assign(key, std::move(value));
      }
      durable_checkpoints_[partition] = checkpoints_.at(partition);
    }
    prepared_.clear();
    checkpoints_.clear();
    for (auto& [key, entry] : pending_) {
      schedule(entry);
      if (const auto previous = cache_.find(key); previous != cache_.end()) {
        cache_bytes_ -= previous->second.value.ByteSizeLong();
      }
      cache_bytes_ += entry.value.ByteSizeLong();
      cache_.insert_or_assign(key, std::move(entry));
    }
    for (const auto& [partition, keys] : deletions_) {
      for (const auto& key : keys) {
        if (const auto previous = cache_.find(key); previous != cache_.end()) {
          cache_bytes_ -= previous->second.value.ByteSizeLong();
          cache_.erase(previous);
        }
        unschedule(key);
      }
    }
    while (cache_.size() > 4096 || cache_bytes_ > 67108864) {
      cache_bytes_ -= cache_.begin()->second.value.ByteSizeLong();
      cache_.erase(cache_.begin());
    }
    pending_.clear();
    deletions_.clear();
  }

  void aborted() override {
    pending_clocks_.clear();
    prepared_.clear();
    pending_.clear();
    deletions_.clear();
    checkpoints_.clear();
    timers_.clear();
  }

  asio::awaitable<void> shutdown() override {
    co_await Base::shutdown();
    (void)co_await context().blocking().run([this] { stores_.clear(); });
  }

 private:
  struct Entry {
    std::string key;
    Changelog value;
    std::int32_t partition;
  };

  void unschedule(const std::string& key) {
    const auto found = timer_keys_.find(key);
    if (found != timer_keys_.end()) {
      scheduled_.erase({found->second.due, key});
      timer_keys_.erase(found);
    }
  }

  void schedule(const Entry& entry) {
    State state;
    if (!state.ParseFromString(entry.value.state().state())) {
      return;
    }
    const auto& rule = state.rule();
    const auto options = model().options(entry.value.envelope().tenant_id(), {});
    const auto lateness =
        rule.has_allowed_lateness() ? Time::DurationToMilliseconds(rule.allowed_lateness()) : options.lateness_ms;
    auto due = state.last_activity_ms()
               + std::max({options.replay_age_ms, std::int64_t{60000}, Time::DurationToMilliseconds(rule.window()),
                           Time::DurationToMilliseconds(rule.suppression())});
    if (state.has_subject()) {
      due = state.next_deadline_ms() + lateness;
    }
    for (const auto& pending : state.pending()) {
      due = std::min(due, pending.deadline_ms() + lateness);
    }
    for (const auto& event : state.buffered()) {
      due = std::min(due, Time::TimestampToMilliseconds(event.envelope().occurred_at()) + lateness);
    }
    if (due <= now()) {
      auto active_until = std::numeric_limits<std::int64_t>::max();
      for (const auto& source : state.sources()) {
        const auto expires = source.received_ms() + options.idle_ms + 1;
        if (expires > now()) {
          active_until = std::min(active_until, expires);
        }
      }
      due = active_until == std::numeric_limits<std::int64_t>::max() ? now() + 10 : active_until;
    }
    unschedule(entry.key);
    timer_keys_[entry.key] = {entry.partition, due};
    scheduled_.insert({due, entry.key});
  }

  Options timed_options(std::int32_t partition, const std::string& tenant, const rules::Rule& rule) const {
    auto options = model().options(tenant, {});
    const auto observed = now();
    const auto& clock = pending_clocks_.contains(partition) ? pending_clocks_.at(partition) : clocks_.at(partition);
    auto minimum = std::numeric_limits<std::int64_t>::max();
    const auto sources = clock.find(tenant);
    if (sources != clock.end()) {
      for (const auto& [id, source] : sources->second) {
        if (observed - source.received_ms() <= options.idle_ms) {
          minimum = std::min(minimum, source.maximum_ms());
        }
      }
    }
    const auto lateness =
        rule.has_allowed_lateness() ? Time::DurationToMilliseconds(rule.allowed_lateness()) : options.lateness_ms;
    options.watermark_limit_ms = (minimum == std::numeric_limits<std::int64_t>::max() ? observed : minimum) - lateness;
    return options;
  }

  asio::awaitable<p::Result<void>> restore(std::int32_t partition) {
    const auto started = std::chrono::steady_clock::now();
    const auto directory = context().config().get<std::string>("settings.state_directory") + "/"
                           + std::to_string(shard_) + "/" + std::to_string(partition);
    auto opened = co_await context().blocking().run([directory] { return StateStore::open(directory); });
    if (!opened) {
      co_return std::unexpected(opened.error());
    }
    stores_[partition] = std::move(*opened);
    auto checkpoint =
        co_await context().blocking().run([this, partition] { return stores_.at(partition)->checkpoint(); });
    if (!checkpoint) {
      co_return std::unexpected(checkpoint.error());
    }
    auto source = p::kafka::Consumer::create(executor(),
                                             {.client = kafka_options("correlation-restore"),
                                              .group_id = "correlation-restore-" + p::stream::uuid7(),
                                              .topics = {kChangelog},
                                              .group_protocol = "classic",
                                              .manual = true},
                                             {.blocking = &context().blocking(), .logger = &context().logger()});
    if (!source) {
      co_return std::unexpected(source.error());
    }
    auto bounds = co_await (*source)->offset_bounds(std::chrono::seconds(5));
    if (!bounds) {
      co_return std::unexpected(bounds.error());
    }
    const p::kafka::TopicPartition id{kChangelog, partition};
    if (!bounds->end.contains(id)) {
      co_return p::fail(p::ErrorCode::kCommonInvalidArgument,
                        "changelog and repartition topics have different partition counts");
    }
    const auto target = bounds->end.at(id);
    auto groups = co_await consumer()->committed({{kRepartition, partition}}, std::chrono::seconds(5));
    if (!groups) {
      co_return std::unexpected(groups.error());
    }
    if (*checkpoint == 0 || *checkpoint < bounds->begin.at(id) || *checkpoint > target || groups->empty()) {
      auto cleared = co_await context().blocking().run([this, partition] { return stores_.at(partition)->clear(); });
      if (!cleared) {
        co_return cleared;
      }
      *checkpoint = bounds->begin.at(id);
    }
    if (auto assigned = (*source)->assign({{id, *checkpoint}}); !assigned) {
      co_return assigned;
    }
    while ((*source)->positions().at(id) < target) {
      if (std::chrono::steady_clock::now() - started >= std::chrono::seconds(30)) {
        co_return p::fail(p::ErrorCode::kCommonDeadlineExceeded, "correlation state recovery exceeded its deadline");
      }
      auto messages = co_await (*source)->poll(512, std::chrono::milliseconds(10));
      if (!messages) {
        co_return std::unexpected(messages.error());
      }
      Changes changes;
      for (const auto& message : *messages) {
        if (message.offset() >= target || message.key() == "__checkpoint") {
          continue;
        }
        const auto key = std::string(message.key());
        if (message.value().empty()) {
          changes[key] = std::nullopt;
          continue;
        }
        auto value = decode<Changelog>(message);
        State state;
        if (!value || !value->has_state() || value->state().state_format() != 1
            || !state.ParseFromString(value->state().state())
            || state_key(value->envelope().tenant_id(), state.rule(), value->state().correlation_key()) != key) {
          co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid,
                            "invalid correlation changelog identity or encoding");
        }
        changes[key] = value->SerializeAsString();
      }
      const auto offset = std::min(target, (*source)->positions().at(id));
      auto applied = co_await context().blocking().run(
          [this, partition, &changes, offset] { return stores_.at(partition)->apply(changes, offset); });
      if (!applied) {
        co_return applied;
      }
    }
    co_await (*source)->close();
    auto& clock = clocks_[partition];
    std::string after;
    while (true) {
      auto entries =
          co_await context().blocking().run([this, partition, &after] { return stores_.at(partition)->page(after); });
      if (!entries) {
        co_return std::unexpected(entries.error());
      }
      if (entries->empty()) {
        break;
      }
      for (const auto& [key, bytes] : *entries) {
        if (!timer_keys_.contains(key)
            && timer_keys_.size() >= context().config().get<std::size_t>("settings.max_live_keys")) {
          co_return p::fail(p::ErrorCode::kCommonRateLimited, "restored correlation key capacity exceeded");
        }
        Changelog record_value;
        State state;
        if (!record_value.ParseFromString(bytes) || !state.ParseFromString(record_value.state().state())) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "invalid restored source clock");
        }
        for (const auto& source_clock : state.sources()) {
          auto& current = clock[record_value.envelope().tenant_id()][source_clock.source_id()];
          current.set_maximum_ms(std::max(current.maximum_ms(), source_clock.maximum_ms()));
          current.set_received_ms(std::max(current.received_ms(), source_clock.received_ms()));
        }
        schedule({key, record_value, partition});
      }
      after = entries->rbegin()->first;
    }
    recovery_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> publish(std::vector<rules::Decision> decisions, common::v1::Envelope source,
                                           p::kafka::Output* output, p::kafka::Headers headers) {
    for (const auto& decision : decisions) {
      if (decision.suppressed) {
        suppressed_->Increment();
        audit::v1::AuditEventRecord audit_record;
        *audit_record.mutable_envelope() = source;
        auto* envelope = audit_record.mutable_envelope();
        envelope->set_message_id(p::stream::uuid5(decision.id, {"maintenance-suppressed"}));
        envelope->set_causation_id(decision.id);
        *envelope->mutable_produced_at() = Time::GetCurrentTime();
        envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
        envelope->mutable_actor()->set_id("correlation-engine");
        auto* entry = audit_record.mutable_audit_entry();
        *entry->mutable_actor() = envelope->actor();
        entry->set_action("correlation.maintenance_suppressed");
        entry->set_object_type("signal");
        entry->set_object_id(decision.id);
        entry->set_site_id(decision.signal.site_id());
        entry->set_outcome(audit::v1::AUDIT_OUTCOME_SUCCESS);
        nlohmann::json versions = nlohmann::json::array();
        for (const auto& [id, version] : decision.maintenance_versions) {
          versions.push_back({{"maintenance_id", id}, {"version", version}});
        }
        (*entry->mutable_details())["maintenance_versions"] = versions.dump();
        (*entry->mutable_details())["signal"] = p::stream::json(decision.signal);
        if (auto sent = co_await output->send(record("psim.audit.v1", source.tenant_id(), audit_record, headers));
            !sent) {
          co_return sent;
        }
        continue;
      }
      processing::v1::SignalRecord value;
      *value.mutable_envelope() = source;
      auto* envelope = value.mutable_envelope();
      envelope->set_causation_id(source.message_id());
      envelope->set_message_id(decision.id);
      envelope->set_sequence(1);
      envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
      envelope->mutable_actor()->set_id("correlation-engine");
      *envelope->mutable_produced_at() = Time::GetCurrentTime();
      *envelope->mutable_occurred_at() =
          decision.signal.has_absence_deadline() ? decision.signal.absence_deadline() : decision.signal.last_event_at();
      *value.mutable_signal() = decision.signal;
      if (auto sent = co_await output->send(record("psim.signals.v1", decision.signal.grouping_key(), value, headers));
          !sent) {
        co_return sent;
      }
    }
    co_return p::Result<void>{};
  }

  std::size_t shard_;
  std::string component_name_;
  std::map<std::int32_t, std::unique_ptr<StateStore>> stores_;
  using TenantClocks = std::map<std::string, std::map<std::string, processing::v1::LiveSource>>;
  std::map<std::int32_t, TenantClocks> clocks_;
  std::map<std::int32_t, TenantClocks> pending_clocks_;
  std::set<std::int32_t> restoring_;
  std::set<std::int32_t> closing_;
  std::map<std::string, Entry> pending_;
  std::map<std::string, Entry> cache_;
  std::size_t cache_bytes_ = 0;
  std::map<std::int32_t, std::set<std::string>> deletions_;
  std::map<std::int32_t, Changes> prepared_;
  std::map<std::int32_t, Changes> durable_;
  std::map<std::int32_t, std::int64_t> checkpoints_;
  std::map<std::int32_t, std::int64_t> durable_checkpoints_;
  std::vector<Entry> timers_;

  struct TimerKey {
    std::int32_t partition;
    std::int64_t due;
  };

  std::map<std::string, TimerKey> timer_keys_;
  std::set<std::pair<std::int64_t, std::string>> scheduled_;
  std::atomic_bool serving_{false};
  std::uint64_t rules_revision_ = 0;
  prometheus::Counter* late_ = nullptr;
  prometheus::Counter* suppressed_ = nullptr;
  prometheus::Histogram* latency_ = nullptr;
  prometheus::Histogram* recovery_ = nullptr;
  prometheus::Histogram* persistence_ = nullptr;
};
}  // namespace

std::unique_ptr<p::runtime::Component> router(const p::runtime::Context& context) {
  return std::make_unique<Router>(context);
}

std::unique_ptr<p::runtime::Component> engine(const p::runtime::Context& context, std::size_t shard) {
  return std::make_unique<Engine>(context, shard);
}
}  // namespace psim::correlation::live
