#include "realtime.hpp"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "psim/incident/v1/events.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/realtime/v1/realtime.pb.h"
#include "psim/response/v1/events.pb.h"

namespace psim::api_gateway {
namespace p = platform;
using Json = nlohmann::json;

namespace {
std::string text(const google::protobuf::Message& message, const char* name) {
  const auto* field = message.GetDescriptor()->FindFieldByName(name);
  return field && field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING
             ? message.GetReflection()->GetString(message, field)
             : std::string{};
}

void identity(const google::protobuf::Message& message, RealtimeEvent& event) {
  event.site = text(message, "site_id");
  event.zone = text(message, "zone_id");
  event.incident = text(message, "incident_id");
  for (int i = 0; i < message.GetDescriptor()->field_count(); ++i) {
    const auto* field = message.GetDescriptor()->field(i);
    if (!field->is_repeated() && field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE
        && message.GetReflection()->HasField(message, field)) {
      RealtimeEvent nested;
      identity(message.GetReflection()->GetMessage(message, field), nested);
      if (!nested.site.empty()) {
        event.site = nested.site;
      }
      if (!nested.zone.empty()) {
        event.zone = nested.zone;
      }
      if (!nested.incident.empty()) {
        event.incident = nested.incident;
      }
    }
  }
}
}  // namespace

p::Result<RealtimeEvent> realtime_event(const p::kafka::Message& message) {
  RealtimeEvent result;
  result.partition = {std::string(message.topic()), message.partition()};
  result.offset = message.offset();
  result.timestamp = message.timestamp().value_or(std::chrono::system_clock::now());
  if (message.topic() == kIncidentTopic) {
    auto event = p::kafka::ProtobufSerde<incident::v1::IncidentEventRecord>(1).deserialize(message.value());
    if (!event) {
      return std::unexpected(event.error());
    }
    result.tenant = event->envelope().tenant_id();
    identity(*event, result);
    if (event->payload_case() != incident::v1::IncidentEventRecord::PAYLOAD_NOT_SET) {
      *result.value.mutable_incident_event() = std::move(*event);
    }
  } else if (message.topic() == kResponseTopic) {
    auto event = p::kafka::ProtobufSerde<response::v1::ResponseEventRecord>(1).deserialize(message.value());
    if (!event) {
      return std::unexpected(event.error());
    }
    result.tenant = event->envelope().tenant_id();
    identity(*event, result);
    // Delivery intents and private notifications have their own authorization boundary.
    if (event->has_run_updated() || event->has_sla_breached() || event->has_escalation_level_triggered()
        || event->has_plan_unavailable() || event->has_sla_policy_pinned() || event->has_site_duty_changed()) {
      *result.value.mutable_response_event() = std::move(*event);
    }
  }
  return result;
}

std::string scope_key(const p::security::Principal& principal) {
  return Json{{"tenant", principal.tenant}, {"roles", principal.roles}, {"scopes", principal.scopes}}.dump();
}

std::string resume_token(const p::kafka::Offsets& positions, std::string tenant, std::string context) {
  Json offsets = Json::object();
  Json response = Json::object();
  for (const auto& [partition, offset] : positions) {
    (partition.topic == kIncidentTopic ? offsets : response)[std::to_string(partition.partition)] = offset;
  }
  return Json{{"version", 2},
              {"tenant", std::move(tenant)},
              {"accessContext", std::move(context)},
              {"issuedAtMs", std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count()},
              {"offsets", offsets},
              {"responseOffsets", response}}
      .dump();
}

p::Result<p::kafka::Offsets> resume_positions(std::string_view token, std::string_view tenant) {
  try {
    const auto value = Json::parse(token);
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (!value.at("issuedAtMs").is_number_integer()) {
      throw std::invalid_argument("invalid token time");
    }
    const auto issued = value.at("issuedAtMs").get<std::int64_t>();
    if (value.at("version") != 2 || value.at("tenant") != tenant || issued > now + 30000 || issued < now - 900000) {
      return p::fail(p::ErrorCode::kRealtimeResyncRequired, "resume token expired");
    }
    p::kafka::Offsets result;
    for (const auto& [name, topic] :
         {std::pair{"offsets", kIncidentTopic}, std::pair{"responseOffsets", kResponseTopic}}) {
      const auto& offsets = value.at(name);
      if (!offsets.is_object() || offsets.size() > 64) {
        throw std::invalid_argument("invalid partitions");
      }
      for (const auto& [partition, offset] : offsets.items()) {
        const auto index = std::stoi(partition);
        if (index < 0 || std::to_string(index) != partition || !offset.is_number_integer()
            || offset.get<std::int64_t>() < 0) {
          throw std::invalid_argument("invalid offset");
        }
        result[{topic, index}] = offset.get<std::int64_t>();
      }
    }
    return result;
  } catch (const std::exception&) {
    return p::fail(p::ErrorCode::kRealtimeResyncRequired, "invalid resume token");
  }
}

bool realtime_filter(const p::security::Principal& principal, const realtime::v1::Subscribe& sub,
                     const RealtimeEvent& event) {
  const auto& filter = sub.filter();
  const auto contains = [](const auto& values, const std::string& id) {
    return values.empty() || std::ranges::find(values, id) != values.end();
  };
  return p::authz::scoped(principal, {event.tenant, event.site, event.zone}) && contains(filter.site_ids(), event.site)
         && contains(filter.zone_ids(), event.zone)
         && (sub.stream() != realtime::v1::STREAM_INCIDENT || event.incident == filter.incident_id());
}

boost::asio::awaitable<p::Result<p::kafka::Offsets>> replay_events(p::kafka::Consumer* source,
                                                                   p::kafka::Offsets positions,
                                                                   std::function<void(const RealtimeEvent&)> receive) {
  auto bounds = co_await source->offset_bounds(std::chrono::seconds(5));
  if (!bounds) {
    co_return std::unexpected(bounds.error());
  }
  if (positions.size() != bounds->end.size()) {
    co_return p::fail(p::ErrorCode::kRealtimeResyncRequired, "partition set changed");
  }
  for (const auto& [partition, end] : bounds->end) {
    if (!positions.contains(partition) || positions.at(partition) < bounds->begin.at(partition)
        || positions.at(partition) > end) {
      co_return p::fail(p::ErrorCode::kRealtimeResyncRequired, "offset outside retained history");
    }
  }
  auto assigned = source->assign(positions);
  if (!assigned) {
    co_return std::unexpected(assigned.error());
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::size_t count = 0;
  std::size_t bytes = 0;
  while (!std::ranges::all_of(bounds->end, [&](const auto& item) {
    return source->positions().contains(item.first) && source->positions().at(item.first) >= item.second;
  })) {
    if (std::chrono::steady_clock::now() > deadline) {
      co_return p::fail(p::ErrorCode::kRealtimeResyncRequired, "replay deadline reached");
    }
    auto batch = co_await source->poll(64, std::chrono::milliseconds(2));
    if (!batch) {
      co_return std::unexpected(batch.error());
    }
    for (const auto& message : *batch) {
      if (message.offset() >= bounds->end.at({std::string(message.topic()), message.partition()})) {
        continue;
      }
      bytes += message.value().size();
      if (++count > 10000 || bytes > 4194304) {
        co_return p::fail(p::ErrorCode::kRealtimeResyncRequired, "replay budget reached");
      }
      auto event = realtime_event(message);
      if (!event) {
        co_return std::unexpected(event.error());
      }
      if (event->timestamp < std::chrono::system_clock::now() - std::chrono::minutes(15)) {
        co_return p::fail(p::ErrorCode::kRealtimeResyncRequired, "resume gap exceeds retention window");
      }
      receive(*event);
    }
  }
  for (const auto& [partition, end] : bounds->end) {
    RealtimeEvent marker;
    marker.partition = partition;
    marker.offset = end - 1;
    receive(marker);
  }
  co_return bounds->end;
}
}  // namespace psim::api_gateway
