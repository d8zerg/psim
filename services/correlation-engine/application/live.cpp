#include "live.hpp"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/time_util.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/config/v1/config.pb.h>
#include <psim/processing/v1/rules.pb.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "rules.hpp"

namespace psim::correlation::live {
namespace {
using Time = google::protobuf::util::TimeUtil;
constexpr auto kInvalid = p::ErrorCode::kProcessingRuleInvalid;

std::int64_t occurred(const rules::Event& event) {
  return Time::TimestampToMilliseconds(event.envelope().occurred_at());
}

std::string fingerprint(const google::protobuf::Message& message) {
  std::string bytes;
  {
    google::protobuf::io::StringOutputStream stream(&bytes);
    google::protobuf::io::CodedOutputStream output(&stream);
    output.SetSerializationDeterministic(true);
    message.SerializeToCodedStream(&output);
  }
  return p::stream::sha256(bytes);
}
}  // namespace

Control::Control(std::set<std::string> taxonomy, std::size_t max_entries)
    : taxonomy_(std::move(taxonomy)), max_entries_(max_entries) {}

p::Result<std::shared_ptr<const rules::CompiledRule>> Control::compiled(const std::string& tenant,
                                                                        const rules::Rule& rule) {
  const auto id = tenant + "/" + rule.rule_id() + "/" + std::to_string(rule.version());
  const auto found = compiled_.find(id);
  if (found != compiled_.end()) {
    if (found->second.wire != rule.SerializeAsString()) {
      return p::fail(kInvalid, "conflicting immutable rule version");
    }
    return found->second.value;
  }
  auto checked = rules::compile(rule, taxonomy_);
  if (!checked.compiled || !checked.violations.empty()) {
    return p::fail(kInvalid, "invalid published correlation rule");
  }
  if (compiled_.size() >= max_entries_) {
    // Published inputs carry the immutable rule, so cold versions can be compiled again.
    compiled_.clear();
  }
  auto value = std::make_shared<const rules::CompiledRule>(std::move(*checked.compiled));
  compiled_.emplace(id, CachedRule{value, rule.SerializeAsString()});
  return value;
}

p::Result<void> Control::config(const config::v1::ConfigRecord& record) {
  const auto& tenant = record.envelope().tenant_id();
  if (!p::stream::uuid(tenant)) {
    return p::fail(kInvalid, "invalid configuration tenant");
  }
  if (record.has_rule_set()) {
    const auto& next = record.rule_set();
    const auto found = sets_.find(tenant);
    if (found != sets_.end() && found->second.version() >= next.version()) {
      if (found->second.version() == next.version() && fingerprint(found->second) != fingerprint(next)) {
        return p::fail(kInvalid, "conflicting rule set version");
      }
      return {};
    }
    if (!next.has_activated_at() || !Time::IsTimestampValid(next.activated_at()) || next.version() == 0) {
      return p::fail(kInvalid, "invalid rule set activation");
    }
    std::set<std::string> ids;
    std::vector<std::shared_ptr<const rules::CompiledRule>> active;
    active.reserve(static_cast<std::size_t>(next.rules_size()));
    for (const auto& rule : next.rules()) {
      if (rule.state() != processing::v1::RULE_STATE_ACTIVE || !ids.insert(rule.rule_id()).second) {
        return p::fail(kInvalid, "rule set contains inactive or duplicate rules");
      }
      auto checked = compiled(tenant, rule);
      if (!checked) {
        return std::unexpected(checked.error());
      }
      active.push_back(std::move(*checked));
    }
    sets_[tenant] = next;
    active_[tenant] = std::move(active);
    ++revision_;
  } else if (record.has_processing()) {
    auto& previous = settings_[tenant];
    if (record.processing().version() > previous.version()) {
      previous = record.processing();
    }
  } else if (record.has_maintenance_window()) {
    const auto& next = record.maintenance_window();
    if (!next.has_starts_at() || !next.has_ends_at() || !Time::IsTimestampValid(next.starts_at())
        || !Time::IsTimestampValid(next.ends_at())) {
      return p::fail(kInvalid, "invalid maintenance bounds");
    }
    auto& history = contexts_[tenant].maintenance;
    auto found = std::ranges::find_if(history, [&](const auto& value) {
      return value.maintenance_id() == next.maintenance_id() && value.version() == next.version();
    });
    if (found != history.end()) {
      return fingerprint(*found) == fingerprint(next) ? p::Result<void>{}
                                                      : p::fail(kInvalid, "conflicting maintenance version");
    }
    if (history.size() >= max_entries_) {
      return p::fail(kInvalid, "maintenance history limit exceeded");
    }
    history.push_back(next);
  }
  if (sets_.size() + settings_.size() + contexts_.size() > max_entries_) {
    return p::fail(kInvalid, "control replica limit exceeded");
  }
  return {};
}

p::Result<void> Control::catalog(const catalog::v1::CatalogRecord& record) {
  const auto& tenant = record.envelope().tenant_id();
  if (!p::stream::uuid(tenant)) {
    return p::fail(kInvalid, "invalid catalog tenant");
  }
  auto& context = contexts_[tenant];
  if (record.has_location()) {
    const auto& location = record.location();
    auto found = context.locations.find(location.location_id());
    if (found == context.locations.end() || found->second.version() < location.version()) {
      context.locations[location.location_id()] = location;
    }
  } else if (record.has_device()) {
    const auto& device = record.device();
    const auto id = tenant + "/" + device.device_id();
    const auto found = devices_.find(id);
    if (found != devices_.end() && found->second.version() >= device.version()) {
      return {};
    }
    devices_[id] = device;
    std::erase_if(context.subjects, [&](const auto& value) { return value.event().device_id() == device.device_id(); });
    if (device.state() == catalog::v1::RESOURCE_STATE_ACTIVE) {
      for (const auto& source : device.sources()) {
        rules::Event subject;
        subject.mutable_envelope()->set_tenant_id(tenant);
        subject.mutable_envelope()->set_message_id(source.source_id());
        *subject.mutable_envelope()->mutable_occurred_at() = device.updated_at();
        auto* event = subject.mutable_event();
        event->set_site_id(device.site_id());
        event->set_zone_id(device.zone_id());
        event->set_device_id(device.device_id());
        event->set_source_id(source.source_id());
        event->set_connector_id(device.connector_id());
        context.subjects.push_back(std::move(subject));
      }
    }
  }
  if (context.locations.size() + context.subjects.size() + devices_.size() > max_entries_) {
    return p::fail(kInvalid, "catalog replica limit exceeded");
  }
  return {};
}

const rules::Context& Control::context(const std::string& tenant) const {
  static const rules::Context kEmpty;
  const auto found = contexts_.find(tenant);
  return found == contexts_.end() ? kEmpty : found->second;
}

const std::vector<std::shared_ptr<const rules::CompiledRule>>& Control::active(const std::string& tenant) const {
  static const std::vector<std::shared_ptr<const rules::CompiledRule>> kEmpty;
  const auto found = active_.find(tenant);
  return found == active_.end() ? kEmpty : found->second;
}

Options Control::options(const std::string& tenant, Options defaults) const {
  const auto found = settings_.find(tenant);
  if (found == settings_.end()) {
    return defaults;
  }
  const auto& settings = found->second;
  if (settings.has_source_idle_timeout()) {
    defaults.idle_ms = Time::DurationToMilliseconds(settings.source_idle_timeout());
  }
  if (settings.has_max_clock_skew()) {
    defaults.clock_skew_ms = Time::DurationToMilliseconds(settings.max_clock_skew());
  }
  if (settings.has_max_buffer_replay_age()) {
    defaults.replay_age_ms = Time::DurationToMilliseconds(settings.max_buffer_replay_age());
  }
  return defaults;
}

std::string state_key(const std::string& tenant, const rules::Rule& rule, const std::string& key) {
  return tenant + "/" + rule.rule_id() + "/" + std::to_string(rule.version()) + "/" + key;
}

p::Result<Result> advance(const rules::CompiledRule& rule, State& saved, const rules::Event* incoming,
                          const rules::Context& context, const std::string& tenant, std::int64_t now_ms, bool inventory,
                          Options options) {
  if (now_ms < 0 || options.idle_ms <= 0 || options.lateness_ms < 0 || options.clock_skew_ms < 0
      || options.replay_age_ms <= 0) {
    return p::fail(kInvalid, "invalid live time configuration");
  }
  State next = saved;
  Result result;
  std::vector<rules::Event> ready;
  const auto lateness = rule.rule.has_allowed_lateness() ? Time::DurationToMilliseconds(rule.rule.allowed_lateness())
                                                         : options.lateness_ms;
  if (incoming != nullptr && !inventory) {
    if (incoming->envelope().tenant_id() != tenant || !incoming->has_event()
        || !p::stream::uuid(incoming->envelope().message_id()) || !incoming->envelope().has_occurred_at()
        || !Time::IsTimestampValid(incoming->envelope().occurred_at()) || !incoming->envelope().has_received_at()
        || !Time::IsTimestampValid(incoming->envelope().received_at())) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid live event identity or time");
    }
    auto event = *incoming;
    auto* attributes = event.mutable_event()->mutable_attributes();
    for (auto it = attributes->begin(); it != attributes->end();) {
      if (!rule.referenced_fields.contains("event.attributes." + it->first)) {
        it = attributes->erase(it);
      } else {
        ++it;
      }
    }
    if (!rule.referenced_fields.contains("event.raw_code")) {
      event.mutable_event()->clear_raw_code();
    }
    const auto receipt = Time::TimestampToMilliseconds(event.envelope().received_at());
    auto at = occurred(event);
    if (at > receipt + options.clock_skew_ms) {
      (*event.mutable_event()->mutable_attributes())["x.original_occurred_ms"] = std::to_string(at);
      *event.mutable_envelope()->mutable_occurred_at() = event.envelope().received_at();
      event.mutable_event()->set_clock_skew_corrected(true);
      at = receipt;
      result.corrected = true;
    }
    result.late = at < next.watermark_ms();
    const auto window = Time::DurationToMilliseconds(rule.rule.window());
    const bool closed =
        rule.rule.has_single() ? at < next.watermark_ms() - lateness : at <= next.watermark_ms() - window;
    if (closed || receipt - at > options.replay_age_ms || now_ms - at > options.replay_age_ms) {
      result.late = true;
      return result;
    }
    auto* sources = next.mutable_sources();
    auto source = std::ranges::find_if(*sources, [&](const auto& value) {
      return value.source_id() == event.event().source_id()
             && value.sequence().source_epoch() == event.event().source_epoch();
    });
    if (source == sources->end()) {
      auto* added = next.add_sources();
      added->set_source_id(event.event().source_id());
      added->set_maximum_ms(at);
      added->set_received_ms(now_ms);
      added->mutable_sequence()->set_source_epoch(event.event().source_epoch());
      source = sources->end() - 1;
    }
    auto* cursor = source->mutable_sequence();
    const auto sequence = event.envelope().sequence();
    const auto maximum = std::max(cursor->max_sequence(), sequence);
    const auto distance = maximum - sequence;
    if (!cursor->window().empty() && cursor->window().size() != 512) {
      return p::fail(kInvalid, "invalid live source cursor");
    }
    if (sequence <= cursor->max_sequence() && !cursor->window().empty()) {
      if (distance >= 4096) {
        result.late = true;
        return result;
      }
      if ((static_cast<unsigned char>(cursor->window()[distance / 8]) & (1U << (distance % 8))) != 0) {
        return result;
      }
    }
    std::string bitmap(512, '\0');
    if (!cursor->window().empty()) {
      const auto shift = maximum - cursor->max_sequence();
      if (shift < 4096) {
        const auto whole = static_cast<std::size_t>(shift / 8);
        const auto bits = static_cast<unsigned int>(shift % 8);
        for (std::size_t index = whole; index < bitmap.size(); ++index) {
          auto value = static_cast<unsigned int>(static_cast<unsigned char>(cursor->window()[index - whole])) << bits;
          if (bits != 0 && index > whole) {
            value |= static_cast<unsigned int>(static_cast<unsigned char>(cursor->window()[index - whole - 1]))
                     >> (8U - bits);
          }
          bitmap[index] = static_cast<char>(value & 255U);
        }
      }
    }
    bitmap[distance / 8] = static_cast<char>(static_cast<unsigned char>(bitmap[distance / 8]) | (1U << (distance % 8)));
    cursor->set_max_sequence(maximum);
    cursor->set_window(std::move(bitmap));
    source->set_maximum_ms(std::max(source->maximum_ms(), at));
    source->set_received_ms(std::max(source->received_ms(), now_ms));
    next.set_last_activity_ms(std::max(next.last_activity_ms(), now_ms));
    if (rule.rule.has_absence() || rule.rule.has_threshold()) {
      *next.add_buffered() = std::move(event);
    } else {
      ready.push_back(std::move(event));
    }
  }
  auto watermark = std::numeric_limits<std::int64_t>::max();
  for (const auto& source : next.sources()) {
    if (now_ms - source.received_ms() <= options.idle_ms) {
      watermark = std::min(watermark, source.maximum_ms() - lateness);
    }
  }
  if (watermark == std::numeric_limits<std::int64_t>::max()) {
    watermark = now_ms - lateness;
  }
  if (options.watermark_limit_ms) {
    watermark = std::min(watermark, *options.watermark_limit_ms);
  }
  watermark = std::max(next.watermark_ms(), watermark);
  next.set_watermark_ms(watermark);
  std::vector<rules::Event> waiting;
  for (const auto& event : next.buffered()) {
    (occurred(event) <= watermark ? ready : waiting).push_back(event);
  }
  std::ranges::sort(ready, [](const auto& lhs, const auto& rhs) {
    return occurred(lhs) != occurred(rhs) ? occurred(lhs) < occurred(rhs)
                                          : lhs.envelope().message_id() < rhs.envelope().message_id();
  });
  next.clear_buffered();
  for (auto& event : waiting) {
    *next.add_buffered() = std::move(event);
  }
  auto evaluated = rules::live_step(rule, next, ready, context, tenant, watermark, inventory ? incoming : nullptr);
  if (!evaluated) {
    return std::unexpected(evaluated.error());
  }
  result.decisions = std::move(*evaluated);
  if (std::cmp_greater(next.buffered_size() + next.sources_size(), options.max_entries)
      || next.ByteSizeLong() > options.max_state_bytes) {
    return p::fail(kInvalid, "live key state limit exceeded");
  }
  saved = std::move(next);
  return result;
}
}  // namespace psim::correlation::live
