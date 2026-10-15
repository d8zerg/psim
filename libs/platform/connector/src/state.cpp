#include "psim/platform/connector/state.hpp"

#include <google/protobuf/util/time_util.h>
#include <psim/connector/v1/connector.pb.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace psim::platform::connector {
void StateTracker::connected(std::string session_id) {
  session_id_ = std::move(session_id);
  for (auto& [source, state] : sources_) {
    state.quality = Quality::kUnknown;
  }
}

void StateTracker::disconnected() {
  session_id_.clear();
  for (auto& [source, state] : sources_) {
    state.quality = state.quality == Quality::kActual ? Quality::kStale : state.quality;
  }
}

bool StateTracker::apply(const ::psim::connector::v1::StateObservation& observation) {
  if (session_id_.empty() || observation.cursor().source_ref().empty() || !observation.has_observed_at()
      || !google::protobuf::util::TimeUtil::IsTimestampValid(observation.observed_at())
      || !::psim::connector::v1::DeviceAvailability_IsValid(observation.availability())) {
    return false;
  }
  auto& state = sources_[observation.cursor().source_ref()];
  const auto& previous = state.observation.cursor();
  const auto& current = observation.cursor();
  if (std::make_tuple(current.source_epoch(), current.source_seq())
      < std::make_tuple(previous.source_epoch(), previous.source_seq())) {
    return false;
  }
  if (current.source_epoch() == previous.source_epoch() && current.source_seq() == previous.source_seq()
      && state.observation.has_observed_at()
      && std::make_tuple(observation.observed_at().seconds(), observation.observed_at().nanos())
             < std::make_tuple(state.observation.observed_at().seconds(), state.observation.observed_at().nanos())) {
    return false;
  }
  state.observation = observation;
  state.quality = observation.availability() == ::psim::connector::v1::DEVICE_AVAILABILITY_UNKNOWN
                          || observation.availability() == ::psim::connector::v1::DEVICE_AVAILABILITY_UNSPECIFIED
                      ? Quality::kUnknown
                      : Quality::kActual;
  return true;
}

bool StateTracker::apply(const ::psim::connector::v1::ConnectorEvent& event,
                         const std::map<std::string, std::string>& values) {
  if (!event.live_observation() || event.observation_session_id() != session_id_ || !event.has_occurred_at()) {
    return false;
  }
  const auto found = sources_.find(event.source_ref());
  if (found != sources_.end()) {
    const auto& cursor = found->second.observation.cursor();
    if (std::make_tuple(event.source_epoch(), event.source_seq())
        <= std::make_tuple(cursor.source_epoch(), cursor.source_seq())) {
      return false;
    }
  }
  ::psim::connector::v1::StateObservation observation;
  observation.mutable_cursor()->set_source_ref(event.source_ref());
  observation.mutable_cursor()->set_source_epoch(event.source_epoch());
  observation.mutable_cursor()->set_source_seq(event.source_seq());
  *observation.mutable_observed_at() = event.occurred_at();
  observation.set_availability(::psim::connector::v1::DEVICE_AVAILABILITY_ONLINE);
  observation.mutable_values()->insert(values.begin(), values.end());
  return apply(observation);
}

void StateTracker::expire(std::int64_t now_seconds, std::int64_t max_age_seconds) {
  for (auto& [source, state] : sources_) {
    if (state.quality == Quality::kActual
        && now_seconds - state.observation.observed_at().seconds() > max_age_seconds) {
      state.quality = Quality::kStale;
    }
  }
}

SourceState StateTracker::get(std::string_view source) const {
  const auto found = sources_.find(std::string(source));
  return found == sources_.end() ? SourceState{} : found->second;
}
}  // namespace psim::platform::connector
