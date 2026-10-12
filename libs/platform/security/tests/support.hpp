#pragma once

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/prov_ssl.h>
#include <openssl/rsa.h>
#include <openssl/tls1.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "../src/crypto.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security::testing {
namespace asio = boost::asio;
using Json = nlohmann::json;
template <typename T, auto Free>
using Handle = std::unique_ptr<T, decltype(Free)>;

inline void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("test fixture failed");
  }
}

template <typename T>
T take(Result<T> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

inline void check(Result<void> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
}

template <typename T>
T run(asio::awaitable<T> operation) {
  asio::io_context io;
  auto result = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return result.get();
}

inline std::string pem(BIO* bio) {
  const char* data = nullptr;
  const auto size = BIO_get_mem_data(bio, &data);
  require(size > 0);
  return {data, static_cast<std::size_t>(size)};
}

inline std::shared_ptr<EVP_PKEY> key(bool rsa = false) {
  const Handle<EVP_PKEY_CTX, EVP_PKEY_CTX_free> context(
      EVP_PKEY_CTX_new_from_name(nullptr, rsa ? "RSA" : "EC", nullptr), EVP_PKEY_CTX_free);
  require(context && EVP_PKEY_keygen_init(context.get()) == 1);
  require(rsa ? EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) == 1
              : EVP_PKEY_CTX_set_group_name(context.get(), "prime256v1") == 1);
  EVP_PKEY* native = nullptr;
  require(EVP_PKEY_generate(context.get(), &native) == 1);
  return {native, EVP_PKEY_free};
}

inline std::string private_pem(EVP_PKEY* native) {
  const Handle<BIO, BIO_free> bio(BIO_new(BIO_s_mem()), BIO_free);
  require(PEM_write_bio_PrivateKey(bio.get(), native, nullptr, nullptr, 0, nullptr, nullptr) == 1);
  return pem(bio.get());
}

inline std::string certificate_pem(X509* certificate) {
  const Handle<BIO, BIO_free> bio(BIO_new(BIO_s_mem()), BIO_free);
  require(PEM_write_bio_X509(bio.get(), certificate) == 1);
  return pem(bio.get());
}

struct Directory {
  std::filesystem::path path;

  Directory()
      : path(std::filesystem::temp_directory_path() / ("psim-security-" + std::to_string(std::random_device{}()))) {
    std::filesystem::create_directory(path);
  }

  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  Directory(Directory&&) = delete;
  Directory& operator=(Directory&&) = delete;

  ~Directory() { std::filesystem::remove_all(path); }

  std::string write(std::string_view name, std::string_view content, bool secret = false) const {
    const auto file = path / name;
    if (std::filesystem::exists(file)) {
      std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    }
    {
      std::ofstream stream(file, std::ios::binary);
      stream << content;
    }
    std::filesystem::permissions(file, secret
                                           ? std::filesystem::perms::owner_read
                                           : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    return file.string();
  }
};

inline void extension(X509* certificate, X509* issuer, int kind, const char* value) {
  X509V3_CTX context{};
  X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
  const Handle<X509_EXTENSION, X509_EXTENSION_free> item(X509V3_EXT_conf_nid(nullptr, &context, kind, value),
                                                         X509_EXTENSION_free);
  require(item && X509_add_ext(certificate, item.get(), -1) == 1);
}

struct Certificate {
  std::shared_ptr<EVP_PKEY> private_key;
  std::shared_ptr<X509> native;
};

inline Certificate issue(long serial, std::string_view identity, const Certificate* issuer = nullptr,
                         bool expired = false) {
  Certificate result{key(), {X509_new(), X509_free}};
  require(result.native != nullptr);
  require(X509_set_version(result.native.get(), 2) == 1);
  require(ASN1_INTEGER_set(X509_get_serialNumber(result.native.get()), serial) == 1);
  require(X509_gmtime_adj(X509_getm_notBefore(result.native.get()), -7200) != nullptr);
  require(X509_gmtime_adj(X509_getm_notAfter(result.native.get()), expired ? -3600 : 7 * 24 * 3600) != nullptr);
  require(X509_set_pubkey(result.native.get(), result.private_key.get()) == 1);
  auto* subject = X509_get_subject_name(result.native.get());
  const std::string name = "PSIM test fixture " + std::to_string(serial);
  require(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, detail::octets(name), static_cast<int>(name.size()),
                                     -1, 0)
          == 1);
  require(X509_set_issuer_name(result.native.get(), issuer ? X509_get_subject_name(issuer->native.get()) : subject)
          == 1);
  extension(result.native.get(), issuer ? issuer->native.get() : result.native.get(), NID_basic_constraints,
            issuer ? "critical,CA:FALSE" : "critical,CA:TRUE");
  extension(result.native.get(), issuer ? issuer->native.get() : result.native.get(), NID_key_usage,
            issuer ? "critical,digitalSignature" : "critical,keyCertSign,cRLSign");
  if (issuer) {
    extension(result.native.get(), issuer->native.get(), NID_ext_key_usage, "serverAuth,clientAuth");
    const auto san = "DNS:localhost,IP:127.0.0.1,URI:" + std::string(identity);
    extension(result.native.get(), issuer->native.get(), NID_subject_alt_name, san.c_str());
  }
  require(X509_sign(result.native.get(), issuer ? issuer->private_key.get() : result.private_key.get(), EVP_sha256())
          > 0);
  return result;
}

inline std::string crl(const Certificate& ca, const Certificate* revoked = nullptr, bool stale = false) {
  const Handle<X509_CRL, X509_CRL_free> list(X509_CRL_new(), X509_CRL_free);
  require(X509_CRL_set_version(list.get(), 1) == 1);
  require(X509_CRL_set_issuer_name(list.get(), X509_get_subject_name(ca.native.get())) == 1);
  const Handle<ASN1_TIME, ASN1_TIME_free> start(ASN1_TIME_adj(nullptr, std::time(nullptr), 0, -7200), ASN1_TIME_free);
  const Handle<ASN1_TIME, ASN1_TIME_free> end(ASN1_TIME_adj(nullptr, std::time(nullptr), 0, stale ? -3600 : 86400),
                                              ASN1_TIME_free);
  require(X509_CRL_set1_lastUpdate(list.get(), start.get()) == 1
          && X509_CRL_set1_nextUpdate(list.get(), end.get()) == 1);
  if (revoked != nullptr) {
    Handle<X509_REVOKED, X509_REVOKED_free> item(X509_REVOKED_new(), X509_REVOKED_free);
    require(X509_REVOKED_set_serialNumber(item.get(), X509_get_serialNumber(revoked->native.get())) == 1);
    require(X509_REVOKED_set_revocationDate(item.get(), start.get()) == 1);
    require(X509_CRL_add0_revoked(list.get(), item.get()) == 1);
    (void)item.release();
  }
  require(X509_CRL_sort(list.get()) == 1);
  require(X509_CRL_sign(list.get(), ca.private_key.get(), EVP_sha256()) > 0);
  const Handle<BIO, BIO_free> output(BIO_new(BIO_s_mem()), BIO_free);
  require(PEM_write_bio_X509_CRL(output.get(), list.get()) == 1);
  return pem(output.get());
}

struct Fixture {
  Directory directory;
  Certificate ca = issue(1, "");
  Certificate server = issue(2, "psim://service/api-gateway", &ca);
  Certificate client = issue(3, "psim://connector/connector1", &ca);

  TlsOptions options(const Certificate& identity, const std::string& name) const {
    return {.boundary = Boundary::kExternal,
            .ca_file = directory.write(name + "-ca.pem", certificate_pem(ca.native.get())),
            .certificate_file = directory.write(name + ".pem", certificate_pem(identity.native.get())),
            .private_key_file = directory.write(name + "-key.pem", private_pem(identity.private_key.get()), true)};
  }
};

struct Handshake {
  bool accepted{};
  std::optional<Peer> peer{};
  std::string error{};
};

inline asio::awaitable<Handshake> handshake(std::shared_ptr<asio::ssl::context> server_context,
                                            std::shared_ptr<asio::ssl::context> client_context,
                                            std::string hostname = "localhost", int max_protocol = 0) {
  const auto executor = co_await asio::this_coro::executor;
  asio::ip::tcp::acceptor acceptor(executor, {asio::ip::make_address("127.0.0.1"), 0});
  asio::ssl::stream<asio::ip::tcp::socket> client(executor, *client_context);
  check(configure_hostname(client.native_handle(), hostname));
  if (max_protocol != 0) {
    require(SSL_set_max_proto_version(client.native_handle(), max_protocol) == 1);
  }
  auto client_operation =
      [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                        // closure outlives the structured join or run
    constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);
    co_await client.next_layer().async_connect(acceptor.local_endpoint(), kAwait);
    auto [error] = co_await client.async_handshake(asio::ssl::stream_base::client, kAwait);
    if (!error) {
      std::array<char, 1> marker{};
      (void)co_await asio::async_read(client, asio::buffer(marker), kAwait);
    }
    boost::system::error_code ignored;
    (void)client.next_layer().close(ignored);
  };
  auto server_operation =
      [&]() -> asio::awaitable<Handshake> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                             // closure outlives the structured join or run
    asio::ssl::stream<asio::ip::tcp::socket> server(executor, *server_context);
    co_await acceptor.async_accept(server.next_layer(), asio::use_awaitable);
    constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);
    auto [error] = co_await server.async_handshake(asio::ssl::stream_base::server, kAwait);
    if (error) {
      co_return Handshake{false, {}, error.message()};
    }
    auto peer = authenticated_peer(server.native_handle());
    const std::array<char, 1> marker{'a'};
    (void)co_await asio::async_write(server, asio::buffer(marker), kAwait);
    co_return Handshake{peer.has_value(), peer ? std::optional<Peer>(std::move(*peer)) : std::nullopt,
                        peer ? "" : peer.error().message()};
  };
  using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): coroutine join
  co_return co_await (client_operation() && server_operation());
}

inline Peer peer(const Fixture& fixture, const Certificate& identity, const std::string& name) {
  auto result = run(handshake(take(tls_context(fixture.options(fixture.server, "server"), true)),
                              take(tls_context(fixture.options(identity, name)))));
  require(result.accepted);
  if (!result.peer) {
    throw std::runtime_error("authenticated fixture peer missing");
  }
  return std::move(result.peer.value());
}

inline std::string base64(std::string_view value) {
  constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string result;
  unsigned int accumulator = 0;
  unsigned int bits = 0;
  for (const auto c : value) {
    accumulator = (accumulator << 8U) | static_cast<unsigned char>(c);
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      result += kAlphabet[(accumulator >> bits) & 63U];
    }
  }
  if (bits != 0) {
    result += kAlphabet[(accumulator << (6U - bits)) & 63U];
  }
  return result;
}

inline std::string bn_bytes(const BIGNUM* value) {
  std::string bytes(static_cast<std::size_t>(BN_num_bytes(value)), '\0');
  require(BN_bn2bin(value, detail::writable_octets(bytes)) > 0);
  return bytes;
}

inline Json jwk(EVP_PKEY* native, std::string kid = "key1") {
  Json result{{"kid", std::move(kid)}, {"use", "sig"}};
  if (EVP_PKEY_is_a(native, "RSA") == 1) {
    BIGNUM* n = nullptr;
    BIGNUM* e = nullptr;
    require(EVP_PKEY_get_bn_param(native, OSSL_PKEY_PARAM_RSA_N, &n) == 1);
    require(EVP_PKEY_get_bn_param(native, OSSL_PKEY_PARAM_RSA_E, &e) == 1);
    const Handle<BIGNUM, BN_free> modulus(n, BN_free);
    const Handle<BIGNUM, BN_free> exponent(e, BN_free);
    result.update({{"kty", "RSA"}, {"alg", "RS256"}, {"n", base64(bn_bytes(n))}, {"e", base64(bn_bytes(e))}});
  } else {
    std::array<unsigned char, 65> point{};
    std::size_t size = 0;
    require(EVP_PKEY_get_octet_string_param(native, OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size(), &size) == 1
            && size == 65);
    const auto bytes = detail::native_text(point.data(), point.size());
    result.update({{"kty", "EC"},
                   {"alg", "ES256"},
                   {"crv", "P-256"},
                   {"x", base64(bytes.substr(1, 32))},
                   {"y", base64(bytes.substr(33, 32))}});
  }
  return result;
}

inline JwtOptions jwt_options() {
  return {.issuer = "https://keycloak.test/realms/psim", .audience = "psim"};
}

inline std::int64_t seconds(std::chrono::system_clock::time_point now) {
  return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

inline Json claims(std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
  return {{"iss", jwt_options().issuer},
          {"aud", "psim"},
          {"sub", "operator1"},
          {"typ", "Bearer"},
          {"iat", seconds(now)},
          {"exp", seconds(now) + 300},
          {"psim_tenant", "tenant1"},
          {"azp", "api-gateway"},
          {"psim_scopes", {"site1", "zone1"}},
          {"resource_access", {{"psim", {{"roles", {"operator"}}}}}}};
}

inline std::string sign_text(EVP_PKEY* native, std::string_view header, std::string_view payload) {
  const bool rsa = EVP_PKEY_is_a(native, "RSA") == 1;
  const auto input = base64(header) + "." + base64(payload);
  const Handle<EVP_MD_CTX, EVP_MD_CTX_free> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  require(EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, native) == 1);
  std::size_t size = 0;
  require(EVP_DigestSign(context.get(), nullptr, &size, detail::octets(input), input.size()) == 1);
  std::string signature(size, '\0');
  require(EVP_DigestSign(context.get(), detail::writable_octets(signature), &size, detail::octets(input), input.size())
          == 1);
  signature.resize(size);
  if (!rsa) {
    const auto* bytes = detail::octets(signature);
    const Handle<ECDSA_SIG, ECDSA_SIG_free> der(d2i_ECDSA_SIG(nullptr, &bytes, static_cast<long>(signature.size())),
                                                ECDSA_SIG_free);
    require(der != nullptr);
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(der.get(), &r, &s);
    std::string first(32, '\0');
    std::string second(32, '\0');
    require(BN_bn2binpad(r, detail::writable_octets(first), 32) == 32);
    require(BN_bn2binpad(s, detail::writable_octets(second), 32) == 32);
    signature = first + second;
  }
  return input + "." + base64(signature);
}

inline std::string sign(EVP_PKEY* native, const Json& payload, std::string kid = "key1",
                        const Json& extra = Json::object()) {
  Json header{{"typ", "JWT"}, {"alg", EVP_PKEY_is_a(native, "RSA") == 1 ? "RS256" : "ES256"}, {"kid", std::move(kid)}};
  header.update(extra);
  return sign_text(native, header.dump(), payload.dump());
}

inline std::string jwks(EVP_PKEY* native, std::string kid = "key1") {
  return Json{{"keys", Json::array({jwk(native, std::move(kid))})}}.dump();
}

/// One structured pair; both operations finish before their enclosing captures are destroyed.
template <typename Operation>
asio::awaitable<void> with_https(const Fixture* fixture, std::string body, unsigned int status, Operation operation,
                                 std::chrono::milliseconds delay = std::chrono::milliseconds::zero()) {
  const auto executor = co_await asio::this_coro::executor;
  asio::ip::tcp::acceptor acceptor(executor, {asio::ip::make_address("127.0.0.1"), 0});
  auto context = take(tls_context(fixture->options(fixture->server, "http-server"), true));
  const auto url = "https://localhost:" + std::to_string(acceptor.local_endpoint().port());
  auto server = [&]() -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                  // closure outlives the structured join or run
    constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);
    asio::ssl::stream<asio::ip::tcp::socket> stream(executor, *context);
    co_await acceptor.async_accept(stream.next_layer(), asio::use_awaitable);
    auto [error] = co_await stream.async_handshake(asio::ssl::stream_base::server, kAwait);
    if (error) {
      co_return;
    }
    boost::beast::flat_buffer buffer;
    boost::beast::http::request<boost::beast::http::string_body> request;
    if (auto [ec, size] = co_await boost::beast::http::async_read(stream, buffer, request, kAwait); ec) {
      co_return;
    }
    if (delay > std::chrono::milliseconds::zero()) {
      asio::steady_timer timer(executor, delay);
      co_await timer.async_wait(asio::use_awaitable);
    }
    boost::beast::http::response<boost::beast::http::string_body> response;
    response.version(11);
    response.result(status);
    response.set(boost::beast::http::field::content_type, "application/json");
    response.body() = body;
    response.prepare_payload();
    (void)co_await boost::beast::http::async_write(stream, response, kAwait);
  };
  using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): structured join
  co_await (server() && operation(url));
}
}  // namespace psim::platform::security::testing
