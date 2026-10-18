#include "protocol.hpp"

#include <google/protobuf/map.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/connector/v1/connector.pb.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::connector_gateway {
namespace {
using Util = google::protobuf::util::TimeUtil;

bool attributes(const google::protobuf::Map<std::string, std::string>& values) {
  return values.size() <= 64 && std::ranges::all_of(values, [](const auto& item) {
           return !item.first.empty() && item.first.size() <= 256 && item.second.size() <= 4096;
         });
}
}  // namespace

bool authorized(const Registration& registration, const Certificate& certificate, Time now) {
  if (registration.connector.state() != catalog::v1::RESOURCE_STATE_ACTIVE || certificate.expires_at <= now
      || certificate.identity != "psim://connector/" + registration.connector.connector_id()) {
    return false;
  }
  return std::ranges::any_of(registration.connector.certificates(), [&](const auto& item) {
    return item.sha256() == certificate.fingerprint && Util::IsTimestampValid(item.not_after())
           && Util::TimestampToSeconds(item.not_after())
                  > std::chrono::floor<std::chrono::seconds>(now.time_since_epoch()).count();
  });
}

platform::Result<void> validate(const connector::v1::ConnectorEvent& event) {
  if (!platform::stream::uuid(event.event_id()) || event.event_id().at(14) != '7' || event.source_ref().empty()
      || event.source_ref().size() > 256 || event.source_seq() == 0 || !event.has_occurred_at()
      || !Util::IsTimestampValid(event.occurred_at()) || event.raw_code().empty() || event.raw_code().size() > 128
      || !attributes(event.attributes()) || event.traceparent().size() > 55
      || event.observation_session_id().size() > 128) {
    return platform::fail(platform::ErrorCode::kIngestInvalidEvent, "invalid connector event");
  }
  return {};
}

platform::Result<void> validate(const connector::v1::StateReport& report) {
  if (report.observations_size() > 1000 || report.ByteSizeLong() > 1048576
      || (report.snapshot() && !platform::stream::uuid(report.snapshot_id()))) {
    return platform::fail(platform::ErrorCode::kIngestInvalidEvent, "invalid state report bounds");
  }
  std::set<std::string> sources;
  for (const auto& observation : report.observations()) {
    const auto& source = observation.cursor().source_ref();
    if (source.empty() || source.size() > 256 || !sources.insert(source).second || !observation.has_observed_at()
        || !Util::IsTimestampValid(observation.observed_at()) || !attributes(observation.values())
        || !connector::v1::DeviceAvailability_IsValid(observation.availability())) {
      return platform::fail(platform::ErrorCode::kIngestInvalidEvent, "invalid state observation");
    }
  }
  return {};
}

std::string protocol(const connector::v1::Register& registration) {
  for (const auto* version : {"1.1", "1.0"}) {
    if (std::ranges::find(registration.protocol_versions(), version) != registration.protocol_versions().end()) {
      return version;
    }
  }
  return {};
}

bool supports(const connector::v1::Register& registration, std::string_view capability) {
  return std::ranges::any_of(registration.capabilities(),
                             [capability](const auto& item) { return item == capability; });
}

bool recent(const google::protobuf::Timestamp& timestamp, Time now, std::chrono::seconds age) {
  if (!Util::IsTimestampValid(timestamp)) {
    return false;
  }
  const auto seconds = std::chrono::floor<std::chrono::seconds>(now.time_since_epoch()).count();
  return timestamp.seconds() >= seconds - age.count() && timestamp.seconds() <= seconds + 5;
}

bool live(const connector::v1::ConnectorEvent& event, std::string_view session, Time now) {
  return event.live_observation() && event.observation_session_id() == session
         && recent(event.occurred_at(), now, std::chrono::seconds(60));
}
}  // namespace psim::connector_gateway
