#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security {
/// Shard-owned projection of Resource Catalog registrations (C1/C2). Persistent registration,
/// authorization and audit belong to step 4.2; distribute revocation to every gateway before ack.
class ConnectorTrust {
 public:
  using Time = std::chrono::system_clock::time_point;
  explicit ConnectorTrust(std::size_t max_connectors = 10000, std::size_t max_sessions = 10000);
  [[nodiscard]] Result<void> enroll(std::string connector, std::string tenant, const Peer& certificate, Time now);
  [[nodiscard]] Result<void> renew(std::string_view connector, const Peer& current, const Peer& replacement, Time now);
  [[nodiscard]] Result<std::string> authenticate(std::string_view connector, const Peer& peer, Time now) const;
  [[nodiscard]] Result<void> attach(std::string session, std::string_view connector, const Peer& peer,
                                    std::function<void()> close, Time now);
  void detach(std::string_view session);
  [[nodiscard]] Result<void> revoke(std::string_view connector, std::string_view fingerprint);
  /// Expiry closes active sessions too; invoke on the gateway timer and check auth on each message.
  void expire(Time now);
  /// Recovery replaces the complete registration and closes all sessions; operator authorization
  /// and MFA are required at the service boundary, never inferred from a new certificate alone.
  [[nodiscard]] Result<void> recover(std::string_view connector, const Peer& replacement, Time now);

 private:
  struct Registration {
    std::string tenant;
    std::map<std::string, Time, std::less<>> certificates{};
    std::map<std::string, Time, std::less<>> revoked{};
  };

  struct Session {
    std::string connector;
    std::string fingerprint;
    Time expires_at;
    std::function<void()> close;
  };

  void close_matching(std::string_view connector, std::string_view fingerprint);
  std::size_t max_connectors_;
  std::size_t max_sessions_;
  std::map<std::string, Registration, std::less<>> registrations_;
  std::map<std::string, Session, std::less<>> sessions_;
};
}  // namespace psim::platform::security
