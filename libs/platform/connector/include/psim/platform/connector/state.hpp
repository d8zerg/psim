#pragma once

#include <psim/connector/v1/connector.pb.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace psim::platform::connector {
enum class Quality : std::uint8_t { kUnknown, kActual, kStale };

struct SourceState {
  ::psim::connector::v1::StateObservation observation;
  Quality quality = Quality::kUnknown;
};

/// Ordered receiver model for protocol conformance and the later gateway/projection integration.
/// A connection alone and historical replay never establish current state.
class StateTracker {
 public:
  void connected(std::string session_id);
  void disconnected();
  bool apply(const ::psim::connector::v1::StateObservation& observation);
  bool apply(const ::psim::connector::v1::ConnectorEvent& event, const std::map<std::string, std::string>& values);
  void expire(std::int64_t now_seconds, std::int64_t max_age_seconds);
  [[nodiscard]] SourceState get(std::string_view source) const;

 private:
  std::string session_id_;
  std::map<std::string, SourceState> sources_;
};
}  // namespace psim::platform::connector
