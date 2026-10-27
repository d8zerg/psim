#include "model.hpp"

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "condition.hpp"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/internal.pb.h"

namespace psim::normalizer {
namespace {
using Time = google::protobuf::util::TimeUtil;
constexpr std::size_t kWindow = 4096;

std::string key(const std::string& tenant, const std::string& id) {
  return tenant + "/" + id;
}

std::string cursor_key(const processing::v1::NormalizerStateRecord& record) {
  return key(record.envelope().tenant_id(), record.source_state().source_id()) + ":"
         + std::to_string(record.source_state().source_epoch());
}

bool newer(const google::protobuf::Timestamp& left, const google::protobuf::Timestamp& right) {
  return left.seconds() > right.seconds() || (left.seconds() == right.seconds() && left.nanos() > right.nanos());
}

bool timestamp(const google::protobuf::Timestamp& time) {
  return Time::IsTimestampValid(time) && time.seconds() > 0;
}

p::Result<std::string> advance(const processing::v1::SourceSequenceState* previous, std::uint64_t sequence) {
  const auto maximum = previous == nullptr ? sequence : std::max(previous->max_sequence(), sequence);
  const auto distance = maximum - sequence;
  if (previous != nullptr && sequence <= previous->max_sequence()) {
    if (distance >= kWindow) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "sequence is outside the deduplication window");
    }
    if ((static_cast<unsigned char>(previous->window()[distance / 8]) & (1U << (distance % 8))) != 0) {
      return std::string{};
    }
  }
  std::string bytes(kWindow / 8, '\0');
  if (previous != nullptr) {
    const auto shift = maximum - previous->max_sequence();
    if (shift < kWindow) {
      const auto whole = static_cast<std::size_t>(shift / 8);
      const auto bits = static_cast<unsigned int>(shift % 8);
      for (std::size_t index = whole; index < bytes.size(); ++index) {
        auto value = static_cast<unsigned int>(static_cast<unsigned char>(previous->window()[index - whole])) << bits;
        if (bits != 0 && index > whole) {
          value |= static_cast<unsigned int>(static_cast<unsigned char>(previous->window()[index - whole - 1]))
                   >> (8U - bits);
        }
        bytes[index] = static_cast<char>(value & 255U);
      }
    }
  }
  bytes[distance / 8] = static_cast<char>(static_cast<unsigned char>(bytes[distance / 8]) | (1U << (distance % 8)));
  return bytes;
}
}  // namespace

Model::Model(Taxonomy taxonomy, std::size_t max_entries) : taxonomy_(std::move(taxonomy)), max_entries_(max_entries) {}

p::Result<Taxonomy> load_taxonomy(const std::string& path) {
  try {
    const auto document = YAML::LoadFile(path);
    if (document["version"].as<std::string>() != "1") {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "unsupported taxonomy version");
    }
    Taxonomy result;
    for (const auto& domain : document["domains"]) {
      for (const auto& type : domain["types"]) {
        common::v1::EventClass event_class{};
        common::v1::Severity severity{};
        auto cls = type["class"].as<std::string>();
        auto sev = type["severity"].as<std::string>();
        std::ranges::transform(cls, cls.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        std::ranges::transform(sev, sev.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        if (!common::v1::EventClass_Parse("EVENT_CLASS_" + cls, &event_class)
            || !common::v1::Severity_Parse("SEVERITY_" + sev, &severity)
            || !result.emplace(type["code"].as<std::string>(), Taxon{event_class, severity}).second) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid taxonomy entry");
        }
      }
    }
    if (result.empty()) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "empty taxonomy");
    }
    return result;
  } catch (const YAML::Exception&) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "cannot load taxonomy");
  }
}

p::Result<void> Model::catalog(const catalog::v1::CatalogRecord& record) {
  const auto& tenant = record.envelope().tenant_id();
  if (!p::stream::uuid(tenant)) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid catalog tenant");
  }
  std::string id;
  std::uint64_t version = 0;
  if (record.has_device()) {
    id = "device/" + record.device().device_id();
    version = record.device().version();
  } else if (record.has_connector()) {
    id = "connector/" + record.connector().connector_id();
    version = record.connector().version();
  } else if (record.has_device_type()) {
    id = "type/" + record.device_type().device_type_id();
    version = record.device_type().version();
  } else if (record.has_location()) {
    id = "location/" + record.location().location_id();
    version = record.location().version();
  } else {
    return {};
  }
  const auto full_key = key(tenant, id);
  const auto previous = catalog_.find(full_key);
  if (previous != catalog_.end() && previous->second.envelope().sequence() >= version) {
    return {};
  }
  if (previous == catalog_.end() && catalog_.size() >= max_entries_) {
    return p::fail(p::ErrorCode::kCommonRateLimited, "catalog replica capacity exceeded");
  }
  if (record.has_device()) {
    const auto old_sources = previous == catalog_.end()
                                 ? std::size_t{0}
                                 : static_cast<std::size_t>(previous->second.device().sources_size());
    if (record.device().sources_size() > 4096
        || sources_.size() - old_sources + static_cast<std::size_t>(record.device().sources_size()) > max_entries_) {
      return p::fail(p::ErrorCode::kCommonRateLimited, "source catalog replica capacity exceeded");
    }
    for (const auto& source : record.device().sources()) {
      if (!p::stream::uuid(source.source_id()) || source.external_ref().empty() || source.external_ref().size() > 256) {
        return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid catalog source");
      }
      const auto owner = sources_.find(key(tenant, source.source_id()));
      if (owner != sources_.end() && owner->second != full_key) {
        return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "catalog source belongs to another device");
      }
    }
    if (previous != catalog_.end()) {
      for (const auto& source : previous->second.device().sources()) {
        sources_.erase(key(tenant, source.source_id()));
      }
    }
    for (const auto& source : record.device().sources()) {
      sources_[key(tenant, source.source_id())] = full_key;
    }
  }
  catalog_[full_key] = record;
  catalog_[full_key].mutable_envelope()->set_sequence(version);
  return {};
}

p::Result<void> Model::config(const config::v1::ConfigRecord& record) {
  const auto& tenant = record.envelope().tenant_id();
  if (!p::stream::uuid(tenant)) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid configuration tenant");
  }
  if (record.has_mapping_rule_set()) {
    const auto& rules = record.mapping_rule_set();
    if (rules.taxonomy_version() != "1" || rules.rules().empty() || rules.rules_size() > 10000
        || rules.connector_type().empty()) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid mapping rule set");
    }
    for (const auto& rule : rules.rules()) {
      if (rule.raw_code().empty() || rule.raw_code().size() > 128
          || !condition(rule.condition(), ingest::v1::RawEvent{}) || !taxonomy_.contains(rule.type())
          || !common::v1::Severity_IsValid(rule.severity())) {
        return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "unsupported or invalid mapping rule");
      }
      if (rule.raw_code().find('*') != std::string::npos && (rule.raw_code().find('*') != rule.raw_code().size() - 1)) {
        return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "mapping wildcard must be terminal");
      }
    }
    const auto id = key(tenant, rules.connector_type());
    if (const auto previous = mappings_.find(id);
        previous != mappings_.end() && previous->second.version() >= rules.version()) {
      return {};
    }
    if (mappings_.size() >= max_entries_ && !mappings_.contains(id)) {
      return p::fail(p::ErrorCode::kCommonRateLimited, "mapping replica capacity exceeded");
    }
    mappings_[id] = rules;
  } else if (record.has_processing()) {
    const auto& settings = record.processing();
    if (!common::v1::Severity_IsValid(settings.significance_threshold()) || settings.significance_threshold() == 0) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid significance threshold");
    }
    if (settings_.size() >= max_entries_ && !settings_.contains(tenant)) {
      return p::fail(p::ErrorCode::kCommonRateLimited, "processing replica capacity exceeded");
    }
    if (!settings_.contains(tenant) || settings_.at(tenant).version() < settings.version()) {
      settings_[tenant] = settings;
    }
  }
  return {};
}

p::Result<std::optional<Prepared>> Model::prepare(const ingest::v1::RawEventRecord& raw) const {
  const auto& envelope = raw.envelope();
  const auto& input = raw.raw_event();
  if (!raw.has_raw_event() || !p::stream::uuid(envelope.tenant_id()) || !p::stream::uuid(envelope.message_id())
      || !p::stream::uuid(input.source_id()) || !p::stream::uuid(input.connector_id())
      || !timestamp(envelope.occurred_at()) || !timestamp(envelope.received_at()) || envelope.sequence() == 0
      || input.attributes_size() > 64 || input.raw_code().size() > 128) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid raw event metadata");
  }
  const auto source_key = key(envelope.tenant_id(), input.source_id());
  const auto id = source_key + ":" + std::to_string(input.source_epoch());
  const auto staged = pending_.find(id);
  const auto saved = committed_.find(id);
  const processing::v1::SourceSequenceState* previous = nullptr;
  if (staged != pending_.end()) {
    previous = &staged->second.record.source_state();
  } else if (saved != committed_.end()) {
    previous = &saved->second.record.source_state();
  }
  auto window = advance(previous, envelope.sequence());
  if (!window) {
    return std::unexpected(window.error());
  }
  if (window->empty()) {
    return std::optional<Prepared>{};
  }
  if (previous == nullptr && committed_.size() + pending_new_ >= max_entries_) {
    return p::fail(p::ErrorCode::kCommonRateLimited, "source cursor capacity exceeded");
  }
  const auto source = sources_.find(source_key);
  const auto connector = catalog_.find(key(envelope.tenant_id(), "connector/" + input.connector_id()));
  if (source == sources_.end() || connector == catalog_.end()) {
    return p::fail(p::ErrorCode::kProcessingDeviceNotFound, "source or connector is absent from catalog");
  }
  const auto& device = catalog_.at(source->second).device();
  const auto type = catalog_.find(key(envelope.tenant_id(), "type/" + device.device_type_id()));
  const auto zone = catalog_.find(key(envelope.tenant_id(), "location/" + device.zone_id()));
  if (type == catalog_.end() || zone == catalog_.end() || device.connector_id() != input.connector_id()
      || !std::ranges::any_of(device.sources(), [&input](const auto& value) {
           return value.source_id() == input.source_id() && value.external_ref() == input.source_ref();
         })) {
    return p::fail(p::ErrorCode::kProcessingDeviceNotFound, "incomplete or inconsistent device catalog");
  }
  if (device.state() != catalog::v1::RESOURCE_STATE_ACTIVE
      || connector->second.connector().state() != catalog::v1::RESOURCE_STATE_ACTIVE
      || type->second.device_type().state() != catalog::v1::RESOURCE_STATE_ACTIVE
      || zone->second.location().state() != catalog::v1::RESOURCE_STATE_ACTIVE) {
    return p::fail(p::ErrorCode::kProcessingDeviceDisabled, "source belongs to a disabled resource");
  }
  const auto mapping = mappings_.find(key(envelope.tenant_id(), connector->second.connector().connector_type()));
  const processing::v1::MappingRule* selected = nullptr;
  if (mapping != mappings_.end()) {
    for (const auto& rule : mapping->second.rules()) {
      const auto& code = rule.raw_code();
      if (code == input.raw_code()
          || (code.ends_with('*') && input.raw_code().starts_with(code.substr(0, code.size() - 1)))) {
        auto matched = condition(rule.condition(), input);
        if (!matched) {
          return std::unexpected(matched.error());
        }
        if (*matched) {
          selected = &rule;
          break;
        }
      }
    }
  }
  if (selected == nullptr) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "no matching mapping rule");
  }
  Prepared result;
  result.key = id;
  *result.event.mutable_envelope() = envelope;
  auto* event = result.event.mutable_event();
  const auto taxon = taxonomy_.at(selected->type());
  event->set_type(selected->type());
  event->set_taxonomy_version(mapping->second.taxonomy_version());
  event->set_event_class(taxon.event_class);
  event->set_severity(selected->severity() == 0 ? taxon.severity : selected->severity());
  event->set_site_id(device.site_id());
  event->set_zone_id(device.zone_id());
  event->set_device_id(device.device_id());
  event->set_device_type_id(device.device_type_id());
  event->set_source_id(input.source_id());
  event->set_connector_id(input.connector_id());
  event->set_source_epoch(input.source_epoch());
  event->set_raw_code(input.raw_code());
  event->set_clock_skew_corrected(input.clock_skew_corrected());
  const auto settings = settings_.find(envelope.tenant_id());
  event->set_significant(
      event->severity()
      >= (settings == settings_.end() ? common::v1::SEVERITY_MEDIUM : settings->second.significance_threshold()));
  for (const auto& [name, value] : input.attributes()) {
    const auto rename = selected->attribute_map().find(name);
    (*event->mutable_attributes())[rename == selected->attribute_map().end() ? "x." + name : rename->second] = value;
  }
  if (input.clock_skew_corrected() && input.has_source_occurred_at()) {
    (*event->mutable_attributes())["x.source_occurred_at"] = Time::ToString(input.source_occurred_at());
  }
  const auto clock_skew_ms = settings != settings_.end() && settings->second.max_clock_skew().seconds() > 0
                                 ? settings->second.max_clock_skew().seconds() * 1000
                                 : 5000;
  if (Time::TimestampToMilliseconds(envelope.occurred_at()) - Time::TimestampToMilliseconds(envelope.received_at())
      > clock_skew_ms) {
    *result.event.mutable_envelope()->mutable_occurred_at() = envelope.received_at();
    (*event->mutable_attributes())["x.source_occurred_at"] = Time::ToString(envelope.occurred_at());
    event->set_clock_skew_corrected(true);
  }
  event->set_gateway_session_id(input.gateway_session_id());
  const auto staged_confirmation = pending_confirmed_.find(source_key);
  const auto confirmation = confirmed_.find(source_key);
  const google::protobuf::Timestamp* last = nullptr;
  if (staged_confirmation != pending_confirmed_.end()) {
    last = &staged_confirmation->second;
  } else if (confirmation != confirmed_.end()) {
    last = &confirmation->second;
  }
  const auto age_ms =
      Time::TimestampToMilliseconds(envelope.received_at()) - Time::TimestampToMilliseconds(envelope.occurred_at());
  const bool live = input.live_observation() && !input.gateway_session_id().empty()
                    && input.gateway_session_id() == input.observation_session_id() && age_ms >= -5000 && age_ms <= 5000
                    && (last == nullptr || newer(envelope.occurred_at(), *last));
  event->set_live_observation(live);
  if (live) {
    *event->mutable_state_observed_at() = envelope.occurred_at();
  }
  *result.state.mutable_envelope() = envelope;
  auto* state = result.state.mutable_source_state();
  state->set_source_id(input.source_id());
  state->set_source_epoch(input.source_epoch());
  state->set_max_sequence(previous == nullptr ? envelope.sequence()
                                              : std::max(previous->max_sequence(), envelope.sequence()));
  state->set_window(std::move(*window));
  if (live || last != nullptr) {
    *state->mutable_last_confirmed_at() = live ? envelope.occurred_at() : *last;
  }
  return std::optional<Prepared>(std::move(result));
}

p::Result<void> Model::restore(const processing::v1::NormalizerStateRecord& record, std::int32_t partition) {
  if (!record.has_source_state() || record.source_state().window().size() != kWindow / 8
      || !p::stream::uuid(record.envelope().tenant_id()) || !p::stream::uuid(record.source_state().source_id())) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid source cursor changelog");
  }
  const auto id = cursor_key(record);
  if (!committed_.contains(id) && committed_.size() >= max_entries_) {
    return p::fail(p::ErrorCode::kCommonRateLimited, "source cursor capacity exceeded");
  }
  committed_.insert_or_assign(id, Cursor{record, partition});
  const auto source = key(record.envelope().tenant_id(), record.source_state().source_id());
  if (newer(record.source_state().last_confirmed_at(), confirmed_[source])) {
    confirmed_[source] = record.source_state().last_confirmed_at();
  }
  return {};
}

p::Result<void> Model::forget(std::string_view cursor, std::int32_t partition) {
  const auto slash = cursor.find('/');
  const auto colon = cursor.find(':');
  std::uint64_t epoch = 0;
  if (slash != 36 || colon != 73 || !p::stream::uuid(cursor.substr(0, slash))
      || !p::stream::uuid(cursor.substr(slash + 1, 36))) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid source cursor tombstone");
  }
  const auto suffix = cursor.substr(colon + 1);
  const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), epoch);
  if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid source cursor epoch");
  }
  const auto found = committed_.find(std::string(cursor));
  if (found != committed_.end()) {
    if (found->second.partition != partition) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "source cursor tombstone partition differs");
    }
    committed_.erase(found);
  }
  confirmed_.erase(std::string(cursor.substr(0, colon)));
  for (const auto& [id, value] : committed_) {
    const auto source = key(value.record.envelope().tenant_id(), value.record.source_state().source_id());
    if (newer(value.record.source_state().last_confirmed_at(), confirmed_[source])) {
      confirmed_[source] = value.record.source_state().last_confirmed_at();
    }
  }
  return {};
}

void Model::stage(Prepared prepared, std::int32_t partition) {
  const auto source = key(prepared.state.envelope().tenant_id(), prepared.state.source_state().source_id());
  if (prepared.state.source_state().has_last_confirmed_at()) {
    pending_confirmed_[source] = prepared.state.source_state().last_confirmed_at();
  }
  if (!pending_.contains(prepared.key) && !committed_.contains(prepared.key)) {
    ++pending_new_;
  }
  pending_.insert_or_assign(prepared.key, Cursor{std::move(prepared.state), partition});
}

std::vector<std::pair<std::int32_t, processing::v1::NormalizerStateRecord>> Model::pending_records() const {
  std::vector<std::pair<std::int32_t, processing::v1::NormalizerStateRecord>> records;
  records.reserve(pending_.size());
  for (const auto& [id, cursor] : pending_) {
    records.emplace_back(cursor.partition, cursor.record);
  }
  return records;
}

void Model::commit() {
  for (auto& [id, cursor] : pending_) {
    committed_.insert_or_assign(id, std::move(cursor));
  }
  for (auto& [source, confirmed] : pending_confirmed_) {
    confirmed_[source] = confirmed;
  }
  abort();
}

void Model::abort() {
  pending_new_ = 0;
  pending_.clear();
  pending_confirmed_.clear();
}

void Model::revoke(std::int32_t partition) {
  std::erase_if(committed_, [partition](const auto& item) { return item.second.partition == partition; });
  confirmed_.clear();
  for (const auto& [id, cursor] : committed_) {
    const auto source = key(cursor.record.envelope().tenant_id(), cursor.record.source_state().source_id());
    if (newer(cursor.record.source_state().last_confirmed_at(), confirmed_[source])) {
      confirmed_[source] = cursor.record.source_state().last_confirmed_at();
    }
  }
}
}  // namespace psim::normalizer
