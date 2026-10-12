#include "psim/platform/security/trust.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security {
namespace {
bool valid(std::string_view connector, const Peer& peer, ConnectorTrust::Time now) {
  return !connector.empty() && connector.size() <= 128 && peer.expires_at() > now
         && peer.identity() == "psim://connector/" + std::string(connector);
}
}  // namespace

ConnectorTrust::ConnectorTrust(std::size_t max_connectors, std::size_t max_sessions)
    : max_connectors_(max_connectors), max_sessions_(max_sessions) {}

Result<void> ConnectorTrust::enroll(std::string connector, std::string tenant, const Peer& certificate, Time now) {
  if (!valid(connector, certificate, now) || tenant.empty() || tenant.size() > 128
      || tenant.find('\0') != std::string::npos) {
    return fail(ErrorCode::kAuthUnauthenticated, "invalid connector registration");
  }
  if (registrations_.contains(connector)) {
    return fail(ErrorCode::kCommonConcurrentModification, "connector already registered");
  }
  if (registrations_.size() >= max_connectors_) {
    return fail(ErrorCode::kCommonRateLimited, "connector registration capacity reached");
  }
  Registration registration{std::move(tenant), {{certificate.fingerprint(), certificate.expires_at()}}};
  registrations_.emplace(std::move(connector), std::move(registration));
  return {};
}

Result<void> ConnectorTrust::renew(std::string_view connector, const Peer& current, const Peer& replacement, Time now) {
  auto authenticated = authenticate(connector, current, now);
  if (!authenticated || !valid(connector, replacement, now) || replacement.fingerprint() == current.fingerprint()) {
    return fail(ErrorCode::kAuthUnauthenticated, "connector renewal refused");
  }
  auto& registration = registrations_.find(connector)->second;
  std::erase_if(registration.revoked, [now](const auto& item) { return item.second <= now; });
  if (registration.revoked.contains(replacement.fingerprint())) {
    return fail(ErrorCode::kAuthUnauthenticated, "revoked certificate cannot be renewed");
  }
  auto& certificates = registration.certificates;
  std::erase_if(certificates, [now](const auto& item) { return item.second <= now; });
  if (certificates.contains(replacement.fingerprint())) {
    return {};
  }
  if (registration.revoked.size() + certificates.size() >= 128) {
    return fail(ErrorCode::kCommonRateLimited, "connector certificate history capacity reached");
  }
  if (certificates.size() >= 2) {
    return fail(ErrorCode::kCatalogCertificateLimit, "finish certificate rotation before renewing again");
  }
  certificates.emplace(replacement.fingerprint(), replacement.expires_at());
  return {};
}

Result<std::string> ConnectorTrust::authenticate(std::string_view connector, const Peer& peer, Time now) const {
  const auto registration = registrations_.find(connector);
  if (!valid(connector, peer, now) || registration == registrations_.end()) {
    return fail(ErrorCode::kAuthUnauthenticated, "connector certificate not registered");
  }
  const auto certificate = registration->second.certificates.find(peer.fingerprint());
  if (certificate == registration->second.certificates.end() || certificate->second <= now) {
    return fail(ErrorCode::kAuthUnauthenticated, "connector certificate revoked or expired");
  }
  return registration->second.tenant;
}

Result<void> ConnectorTrust::attach(std::string session, std::string_view connector, const Peer& peer,
                                    std::function<void()> close, Time now) {
  auto authenticated = authenticate(connector, peer, now);
  if (!authenticated) {
    return std::unexpected(authenticated.error());
  }
  if (session.empty() || session.size() > 128 || !close || sessions_.contains(session)) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid connector session");
  }
  if (sessions_.size() >= max_sessions_) {
    return fail(ErrorCode::kCommonRateLimited, "connector session capacity reached");
  }
  sessions_.emplace(std::move(session),
                    Session{std::string(connector), peer.fingerprint(), peer.expires_at(), std::move(close)});
  return {};
}

void ConnectorTrust::detach(std::string_view session) {
  const auto it = sessions_.find(session);
  if (it != sessions_.end()) {
    sessions_.erase(it);
  }
}

void ConnectorTrust::close_matching(std::string_view connector, std::string_view fingerprint) {
  std::vector<std::function<void()>> close;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second.connector == connector && (fingerprint.empty() || it->second.fingerprint == fingerprint)) {
      close.push_back(std::move(it->second.close));
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
  // Erase before callbacks: a callback may detach itself or trigger another revocation.
  for (auto& callback : close) {
    callback();
  }
}

Result<void> ConnectorTrust::revoke(std::string_view connector, std::string_view fingerprint) {
  const auto registration = registrations_.find(connector);
  if (registration == registrations_.end()) {
    return fail(ErrorCode::kCommonNotFound, "connector not registered");
  }
  if (fingerprint.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "certificate fingerprint required");
  }
  const auto certificate = registration->second.certificates.find(fingerprint);
  if (certificate != registration->second.certificates.end()) {
    registration->second.revoked.emplace(certificate->first, certificate->second);
    registration->second.certificates.erase(certificate);
  }
  close_matching(connector, fingerprint);
  return {};
}

void ConnectorTrust::expire(Time now) {
  std::vector<std::function<void()>> close;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second.expires_at <= now) {
      close.push_back(std::move(it->second.close));
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& [connector, registration] : registrations_) {
    std::erase_if(registration.certificates, [now](const auto& item) { return item.second <= now; });
    std::erase_if(registration.revoked, [now](const auto& item) { return item.second <= now; });
  }
  for (auto& callback : close) {
    callback();
  }
}

Result<void> ConnectorTrust::recover(std::string_view connector, const Peer& replacement, Time now) {
  const auto registration = registrations_.find(connector);
  if (registration == registrations_.end() || !valid(connector, replacement, now)) {
    return fail(ErrorCode::kAuthUnauthenticated, "connector recovery refused");
  }
  std::erase_if(registration->second.revoked, [now](const auto& item) { return item.second <= now; });
  if (registration->second.revoked.contains(replacement.fingerprint())
      || registration->second.certificates.contains(replacement.fingerprint())
      || registration->second.revoked.size() + registration->second.certificates.size() >= 128) {
    return fail(ErrorCode::kAuthUnauthenticated, "fresh recovery certificate required");
  }
  for (const auto& [fingerprint, end] : registration->second.certificates) {
    registration->second.revoked.emplace(fingerprint, end);
  }
  registration->second.certificates = {{replacement.fingerprint(), replacement.expires_at()}};
  close_matching(connector, {});
  return {};
}
}  // namespace psim::platform::security
