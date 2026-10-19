#include "model.hpp"

#include <google/protobuf/util/time_util.h>
#include <psim/connector/v1/connector.pb.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace psim::connectors::simulator {
Model::Model(Profile profile) : profile_(std::move(profile)) {
  if (profile_.devices == 0 || profile_.devices > 100000 || profile_.count == 0 || profile_.count > 100000000
      || profile_.batch == 0 || profile_.batch > 1000 || profile_.rate > 10000000 || profile_.burst_every == 0
      || profile_.burst_size > profile_.burst_every || profile_.run_id.empty() || profile_.run_id.size() > 128
      || profile_.source_prefix.size() > 200
      || (profile_.scenario != "mixed" && profile_.scenario != "alarm" && profile_.scenario != "fire"
          && profile_.scenario != "offline" && profile_.scenario != "burst")) {
    throw std::invalid_argument("invalid simulator profile");
  }
  armed_.resize(profile_.devices, false);
}

wire::ConnectorEvent Model::event(std::size_t index) const {
  wire::ConnectorEvent result;
  result.set_source_ref(profile_.conformance ? "door-1"
                                             : profile_.source_prefix + std::to_string((index % profile_.devices) + 1));
  std::string code = "ALARM";
  if (profile_.scenario == "fire" || (profile_.scenario == "mixed" && index % 3 == 1)) {
    code = "FIRE";
  } else if (profile_.scenario == "offline" || (profile_.scenario == "mixed" && index % 3 == 2)) {
    code = "OFFLINE";
  }
  if (profile_.conformance) {
    code = index == 19 ? "REJECT" : "ALARM";
  }
  result.set_raw_code(code);
  (*result.mutable_attributes())["generator_run"] = profile_.run_id;
  (*result.mutable_attributes())["generator_seq"] = std::to_string(index + 1);
  return result;
}

std::uint64_t Model::scheduled_nanoseconds(std::size_t index) const {
  if (profile_.rate == 0) {
    return 0;
  }
  auto scheduled = static_cast<std::uint64_t>(index);
  if (profile_.scenario == "burst") {
    const auto offset = index % profile_.burst_every;
    scheduled = index - offset + (offset < profile_.burst_size ? 0 : offset);
  }
  return scheduled * 1000000000ULL / profile_.rate;
}

wire::CommandResult Model::command(const wire::Command& command, const std::stop_token& stop) {
  wire::CommandResult result;
  const auto& id = command.device_external_id();
  constexpr std::string_view kPrefix = "device-";
  std::size_t number = 0;
  if (!id.starts_with(kPrefix)) {
    result.set_code("COMMAND_DEVICE_REJECTED");
    return result;
  }
  const auto suffix = id.substr(kPrefix.size());
  const auto parsed = std::from_chars(suffix.data(), std::to_address(suffix.end()), number);
  if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(suffix.end()) || number == 0
      || number > profile_.devices) {
    result.set_code("COMMAND_DEVICE_REJECTED");
    return result;
  }
  if ((command.capability() != "arm" && command.capability() != "disarm") || !command.parameters().empty()) {
    result.set_code("COMMAND_CAPABILITY_UNSUPPORTED");
    return result;
  }
  const auto now = google::protobuf::util::TimeUtil::GetCurrentTime();
  if (stop.stop_requested() || !command.has_deadline_at()
      || !google::protobuf::util::TimeUtil::IsTimestampValid(command.deadline_at())
      || google::protobuf::util::TimeUtil::TimestampToNanoseconds(command.deadline_at())
             <= google::protobuf::util::TimeUtil::TimestampToNanoseconds(now)) {
    result.set_code("COMMAND_EXPIRED");
    return result;
  }
  const std::scoped_lock lock(mutex_);
  armed_[number - 1] = command.capability() == "arm";
  result.set_success(true);
  return result;
}

std::vector<wire::StateObservation> Model::snapshot(const std::stop_token& stop) const {
  const std::scoped_lock lock(mutex_);
  std::vector<wire::StateObservation> result;
  // The SDK bounds a snapshot at 1000 sources; sources outside it remain unknown.
  const auto count = profile_.conformance ? 1 : std::min<std::size_t>(profile_.devices, 1000);
  for (std::size_t index = 0; index < count && !stop.stop_requested(); ++index) {
    wire::StateObservation state;
    state.mutable_cursor()->set_source_ref(profile_.conformance ? "door-1"
                                                                : profile_.source_prefix + std::to_string(index + 1));
    state.set_availability(profile_.scenario == "offline" ? wire::DEVICE_AVAILABILITY_OFFLINE
                                                          : wire::DEVICE_AVAILABILITY_ONLINE);
    *state.mutable_observed_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
    (*state.mutable_values())["armed"] = armed_[index] ? "true" : "false";
    result.push_back(std::move(state));
  }
  return result;
}
}  // namespace psim::connectors::simulator
