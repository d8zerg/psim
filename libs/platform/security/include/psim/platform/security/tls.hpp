#pragma once

#include <boost/asio/ssl/context.hpp>
#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"

namespace psim::platform::security {
enum class Boundary : std::uint8_t { kInternal, kExternal };

struct TlsOptions {
  Boundary boundary = Boundary::kInternal;
  std::string ca_file{};
  std::string certificate_file{};
  std::string private_key_file{};
  /// A full, fresh PEM CRL bundle. Missing/stale CRLs reject peers when configured.
  std::string crl_file{};
  bool require_client_certificate = true;
};

[[nodiscard]] Result<void> configure_tls(SSL_CTX* context, const TlsOptions& options, bool server = false);
/// Configure BOTH SNI and DNS/IP verification; an empty or malformed name is refused.
[[nodiscard]] Result<void> configure_hostname(SSL* stream, std::string_view hostname);
[[nodiscard]] Result<std::shared_ptr<boost::asio::ssl::context>> tls_context(const TlsOptions& options,
                                                                             bool server = false);

/// Build before publishing; failed reload preserves the previous immutable snapshot. Existing
/// connections keep its context alive. Gateways close revoked sessions through ConnectorTrust.
class TlsReload {
 public:
  [[nodiscard]] Result<void> reload(const TlsOptions& options, bool server = false);
  [[nodiscard]] std::shared_ptr<boost::asio::ssl::context> current() const;

 private:
  std::atomic<std::shared_ptr<boost::asio::ssl::context>> current_;
};

class Peer {
 public:
  [[nodiscard]] const std::string& fingerprint() const noexcept { return fingerprint_; }

  [[nodiscard]] const std::string& identity() const noexcept { return identity_; }

  [[nodiscard]] std::chrono::system_clock::time_point expires_at() const noexcept { return expires_at_; }

 private:
  friend Result<Peer> authenticated_peer(SSL* stream);
  Peer(std::string fingerprint, std::string identity, std::chrono::system_clock::time_point expires_at);
  std::string fingerprint_;
  std::string identity_;
  std::chrono::system_clock::time_point expires_at_;
};

/// Only a completed, verified mTLS connection yields a Peer; certificate validity <= 1 year.
[[nodiscard]] Result<Peer> authenticated_peer(SSL* stream);
/// Generate an EC P-256 CSR from a protected PEM key with a single psim://service/... or
/// psim://connector/... URI SAN. No signing/CA functionality is provided by the platform.
[[nodiscard]] Result<std::string> make_csr(std::string_view private_key_file, std::string_view identity);
}  // namespace psim::platform::security
