#include "psim/platform/security/tls.hpp"

#include <boost/asio/ssl/context.hpp>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/prov_ssl.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "crypto.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/secret.hpp"

namespace psim::platform::security {
namespace {
template <typename T, auto Free>
using Handle = std::unique_ptr<T, decltype(Free)>;
constexpr const char* kCiphers =
    "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305";

bool valid_identity(std::string_view identity) {
  const auto prefix = identity.starts_with("psim://service/") ? std::string_view("psim://service/")
                                                              : std::string_view("psim://connector/");
  if (!identity.starts_with(prefix) || identity.size() <= prefix.size() || identity.size() > 256) {
    return false;
  }
  return std::ranges::all_of(identity.substr(prefix.size()), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

std::chrono::system_clock::time_point as_time(const ASN1_TIME* value) {
  std::tm parts{};
  if (ASN1_TIME_to_tm(value, &parts) != 1) {
    return {};
  }
  return std::chrono::sys_days{std::chrono::year{parts.tm_year + 1900} / (parts.tm_mon + 1) / parts.tm_mday}
         + std::chrono::hours(parts.tm_hour) + std::chrono::minutes(parts.tm_min) + std::chrono::seconds(parts.tm_sec);
}

Result<Handle<EVP_PKEY, EVP_PKEY_free>> read_key(std::string_view path) {
  auto secret = read_secret(path);
  if (!secret) {
    return std::unexpected(secret.error());
  }
  const Handle<BIO, BIO_free> bio(BIO_new_mem_buf(secret->view().data(), static_cast<int>(secret->view().size())),
                                  BIO_free);
  if (!bio) {
    return fail(ErrorCode::kCommonInternal, "key reader initialization failed");
  }
  // No password prompt or interactive callback; encrypted keys must be supplied by the operator's agent.
  Handle<EVP_PKEY, EVP_PKEY_free> key(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, [](char*, int, int, void*) { return 0; }, nullptr), EVP_PKEY_free);
  if (!key) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid private key material");
  }
  return key;
}
}  // namespace

Result<void> configure_tls(SSL_CTX* context, const TlsOptions& options, bool server) {
  if (context == nullptr || (server && options.certificate_file.empty())
      || options.certificate_file.empty() != options.private_key_file.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "incomplete TLS configuration");
  }
  SSL_CTX_set_security_level(context, 2);
  std::string groups = "X25519:P-256:P-384";
  if (SSL_CTX_set_min_proto_version(context, options.boundary == Boundary::kExternal ? TLS1_3_VERSION : TLS1_2_VERSION)
          != 1
      || SSL_CTX_set_cipher_list(context, kCiphers) != 1
      || SSL_CTX_set_ciphersuites(context, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256")
             != 1
      || SSL_CTX_set1_groups_list(context, groups.data()) != 1) {
    return fail(ErrorCode::kCommonInvalidArgument, "TLS policy configuration failed");
  }
  (void)SSL_CTX_set_options(context, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_TICKET);
  (void)SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);
  SSL_CTX_set_verify(
      context, SSL_VERIFY_PEER | (server && options.require_client_certificate ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0),
      nullptr);
  SSL_CTX_set_verify_depth(context, 4);
  const auto trusted = options.ca_file.empty()
                           ? SSL_CTX_set_default_verify_paths(context)
                           : SSL_CTX_load_verify_locations(context, options.ca_file.c_str(), nullptr);
  if (trusted != 1) {
    return fail(ErrorCode::kCommonInvalidArgument, "TLS trust material could not be loaded");
  }
  if (!options.crl_file.empty()) {
    auto* store = SSL_CTX_get_cert_store(context);
    if (X509_STORE_load_file(store, options.crl_file.c_str()) != 1
        || X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL) != 1) {
      return fail(ErrorCode::kCommonInvalidArgument, "TLS revocation material could not be loaded");
    }
  }
  if (!options.certificate_file.empty()) {
    auto key = read_key(options.private_key_file);
    if (!key || SSL_CTX_use_certificate_chain_file(context, options.certificate_file.c_str()) != 1
        || SSL_CTX_use_PrivateKey(context, key->get()) != 1 || SSL_CTX_check_private_key(context) != 1) {
      return fail(ErrorCode::kCommonInvalidArgument, "TLS identity material could not be loaded");
    }
  }
  return {};
}

Result<void> configure_hostname(SSL* stream, std::string_view hostname) {
  if (stream == nullptr || hostname.empty() || hostname.size() > 253) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid TLS hostname");
  }
  if (!std::ranges::all_of(hostname, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'
               || c == ':';
      })) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid TLS hostname");
  }
  const std::string name(hostname);
  auto* parameters = SSL_get0_param(stream);
  X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  if (X509_VERIFY_PARAM_set1_ip_asc(parameters, name.c_str()) == 1) {
    return {};
  }
  if (SSL_set_tlsext_host_name(stream, name.c_str()) != 1 || SSL_set1_host(stream, name.c_str()) != 1) {
    return fail(ErrorCode::kCommonInvalidArgument, "TLS hostname configuration failed");
  }
  return {};
}

Result<std::shared_ptr<boost::asio::ssl::context>> tls_context(const TlsOptions& options, bool server) {
  try {
    auto context = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);
    auto result = configure_tls(context->native_handle(), options, server);
    if (!result) {
      return std::unexpected(result.error());
    }
    return context;
  } catch (const std::exception&) {
    return fail(ErrorCode::kCommonInvalidArgument, "TLS context initialization failed");
  }
}

Result<void> TlsReload::reload(const TlsOptions& options, bool server) {
  auto context = tls_context(options, server);
  if (!context) {
    return std::unexpected(context.error());
  }
  current_.store(std::move(*context));
  return {};
}

std::shared_ptr<boost::asio::ssl::context> TlsReload::current() const {
  return current_.load();
}

Peer::Peer(std::string fingerprint, std::string identity, std::chrono::system_clock::time_point expires_at)
    : fingerprint_(std::move(fingerprint)), identity_(std::move(identity)), expires_at_(expires_at) {}

Result<Peer> authenticated_peer(SSL* stream) {
  if (stream == nullptr || SSL_is_init_finished(stream) != 1 || (SSL_get_verify_mode(stream) & SSL_VERIFY_PEER) == 0
      || SSL_get_verify_result(stream) != X509_V_OK) {
    return fail(ErrorCode::kAuthUnauthenticated, "verified mTLS connection required");
  }
  const Handle<X509, X509_free> certificate(SSL_get1_peer_certificate(stream), X509_free);
  if (!certificate) {
    return fail(ErrorCode::kAuthUnauthenticated, "peer certificate required");
  }
  int days = 0;
  int seconds = 0;
  if (ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(certificate.get()), X509_get0_notAfter(certificate.get()))
          != 1
      || days < 0 || days > 365 || (days == 365 && seconds > 0) || (days == 0 && seconds <= 0)) {
    return fail(ErrorCode::kAuthUnauthenticated, "peer certificate lifetime exceeds policy");
  }
  const auto end = as_time(X509_get0_notAfter(certificate.get()));
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (X509_digest(certificate.get(), EVP_sha256(), digest.data(), &length) != 1) {
    return fail(ErrorCode::kAuthUnauthenticated, "peer fingerprint failed");
  }
  std::string fingerprint;
  constexpr std::string_view kHex = "0123456789abcdef";
  for (unsigned int i = 0; i < length; ++i) {
    fingerprint += kHex[digest.at(i) >> 4U];
    fingerprint += kHex[digest.at(i) & 15U];
  }
  std::string identity;
  const Handle<GENERAL_NAMES, GENERAL_NAMES_free> names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate.get(), NID_subject_alt_name, nullptr, nullptr)),
      GENERAL_NAMES_free);
  if (names) {
    for (int i = 0; i < sk_GENERAL_NAME_num(names.get()); ++i) {
      const auto* name = sk_GENERAL_NAME_value(names.get(), i);
      if (name->type == GEN_URI) {
        const auto* text =
            ASN1_STRING_get0_data(static_cast<const ASN1_STRING*>(GENERAL_NAME_get0_value(name, nullptr)));
        const auto size = ASN1_STRING_length(static_cast<const ASN1_STRING*>(GENERAL_NAME_get0_value(name, nullptr)));
        const auto candidate = detail::native_text(text, static_cast<std::size_t>(size));
        if (candidate.starts_with("psim://")) {
          if (!identity.empty() || !valid_identity(candidate)) {
            return fail(ErrorCode::kAuthUnauthenticated, "ambiguous peer identity");
          }
          identity = candidate;
        }
      }
    }
  }
  return Peer(std::move(fingerprint), std::move(identity), end);
}

Result<std::string> make_csr(std::string_view private_key_file, std::string_view identity) {
  if (!valid_identity(identity)) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid CSR identity");
  }
  auto key = read_key(private_key_file);
  std::array<char, 32> group{};
  std::size_t group_size = 0;
  if (!key || EVP_PKEY_is_a(key->get(), "EC") != 1
      || EVP_PKEY_get_utf8_string_param(key->get(), "group", group.data(), group.size(), &group_size) != 1
      || std::string_view(group.data(), group_size) != "prime256v1") {
    return fail(ErrorCode::kCommonInvalidArgument, "CSR requires an EC P-256 key");
  }
  const Handle<X509_REQ, X509_REQ_free> request(X509_REQ_new(), X509_REQ_free);
  const Handle<X509_NAME, X509_NAME_free> subject(X509_NAME_new(), X509_NAME_free);
  const std::string san = "URI:" + std::string(identity);
  const Handle<X509_EXTENSION, X509_EXTENSION_free> extension(
      X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, san.c_str()), X509_EXTENSION_free);
  if (!request || !subject || !extension) {
    return fail(ErrorCode::kCommonInternal, "CSR initialization failed");
  }
  // X509_REQ_add_extensions copies the extension; stack owns only its container here.
  auto* extensions = sk_X509_EXTENSION_new_null();
  if (extensions == nullptr) {
    return fail(ErrorCode::kCommonInternal, "CSR extensions initialization failed");
  }
  (void)sk_X509_EXTENSION_push(extensions, extension.get());
  const auto added = X509_REQ_add_extensions(request.get(), extensions);
  sk_X509_EXTENSION_free(extensions);
  constexpr std::string_view kName = "PSIM identity";
  if (added != 1 || X509_REQ_set_version(request.get(), 0) != 1
      || X509_NAME_add_entry_by_txt(subject.get(), "CN", MBSTRING_ASC, detail::octets(kName),
                                    static_cast<int>(kName.size()), -1, 0)
             != 1
      || X509_REQ_set_subject_name(request.get(), subject.get()) != 1
      || X509_REQ_set_pubkey(request.get(), key->get()) != 1
      || X509_REQ_sign(request.get(), key->get(), EVP_sha256()) <= 0) {
    return fail(ErrorCode::kCommonInternal, "CSR creation failed");
  }
  const Handle<BIO, BIO_free> output(BIO_new(BIO_s_mem()), BIO_free);
  if (!output || PEM_write_bio_X509_REQ(output.get(), request.get()) != 1) {
    return fail(ErrorCode::kCommonInternal, "CSR encoding failed");
  }
  const char* data = nullptr;
  const auto size = BIO_get_mem_data(output.get(), &data);
  return std::string(data, static_cast<std::size_t>(size));
}
}  // namespace psim::platform::security
