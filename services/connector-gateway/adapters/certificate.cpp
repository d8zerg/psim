#include "certificate.hpp"

#include <grpcpp/security/auth_context.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <array>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "protocol.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::connector_gateway {
namespace {
template <typename T, auto Free>
using Handle = std::unique_ptr<T, decltype(Free)>;
}

platform::Result<Certificate> certificate(std::string_view pem, std::string_view ca_file) {
  if (pem.empty() || pem.size() > 65536 || ca_file.empty()) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "invalid certificate bounds");
  }
  const Handle<BIO, BIO_free> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
  const Handle<X509, X509_free> cert(bio ? PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr) : nullptr,
                                     X509_free);
  const Handle<X509_STORE, X509_STORE_free> store(X509_STORE_new(), X509_STORE_free);
  const Handle<X509_STORE_CTX, X509_STORE_CTX_free> check(X509_STORE_CTX_new(), X509_STORE_CTX_free);
  const std::string ca(ca_file);
  if (!cert || !store || !check || X509_STORE_load_file(store.get(), ca.c_str()) != 1
      || X509_STORE_CTX_init(check.get(), store.get(), cert.get(), nullptr) != 1
      || X509_STORE_CTX_set_purpose(check.get(), X509_PURPOSE_SSL_CLIENT) != 1 || X509_verify_cert(check.get()) != 1) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "untrusted client certificate");
  }
  int days = 0;
  int seconds = 0;
  if (ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(cert.get()), X509_get0_notAfter(cert.get())) != 1 || days < 0
      || days > 365 || (days == 365 && seconds > 0) || (days == 0 && seconds <= 0)) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "invalid certificate lifetime");
  }
  Certificate result;
  std::tm parts{};
  if (ASN1_TIME_to_tm(X509_get0_notAfter(cert.get()), &parts) != 1) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "invalid certificate expiry");
  }
  result.expires_at =
      std::chrono::sys_days{std::chrono::year{parts.tm_year + 1900} / (parts.tm_mon + 1) / parts.tm_mday}
      + std::chrono::hours(parts.tm_hour) + std::chrono::minutes(parts.tm_min) + std::chrono::seconds(parts.tm_sec);
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  if (X509_digest(cert.get(), EVP_sha256(), digest.data(), &size) != 1) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "certificate fingerprint failed");
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  for (unsigned int i = 0; i < size; ++i) {
    result.fingerprint += kHex[digest.at(i) >> 4U];
    result.fingerprint += kHex[digest.at(i) & 15U];
  }
  const Handle<GENERAL_NAMES, GENERAL_NAMES_free> names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr)),
      GENERAL_NAMES_free);
  if (names) {
    for (int i = 0; i < sk_GENERAL_NAME_num(names.get()); ++i) {
      const auto* name = sk_GENERAL_NAME_value(names.get(), i);
      if (name->type != GEN_URI) {
        continue;
      }
      const auto* value = static_cast<const ASN1_STRING*>(GENERAL_NAME_get0_value(name, nullptr));
      const auto size_bytes = ASN1_STRING_length(value);
      const auto* data = ASN1_STRING_get0_data(value);
      std::string identity(static_cast<std::size_t>(size_bytes), '\0');
      for (std::size_t n = 0; n < identity.size(); ++n) {
        identity[n] = static_cast<char>(
            data[n]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): ASN1 length checked by OpenSSL
      }
      if (identity.starts_with("psim://")) {
        if (!result.identity.empty() || identity.find('\0') != std::string::npos || identity.size() > 256) {
          return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "ambiguous certificate identity");
        }
        result.identity = std::move(identity);
      }
    }
  }
  if (result.identity.empty()) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "client URI identity required");
  }
  return result;
}

platform::Result<Certificate> certificate(const grpc::AuthContext& context, std::string_view ca_file) {
  const auto certificates = context.FindPropertyValues("x509_pem_cert");
  if (!context.IsPeerAuthenticated() || certificates.size() != 1) {
    return platform::fail(platform::ErrorCode::kAuthUnauthenticated, "verified mTLS identity required");
  }
  const auto& pem = certificates.front();
  return certificate(std::string_view(pem.data(), pem.size()), ca_file);
}
}  // namespace psim::connector_gateway
