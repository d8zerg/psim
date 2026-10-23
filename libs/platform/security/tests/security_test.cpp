#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/prov_ssl.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/types.h>
#include <openssl/x509.h>

#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/security/trust.hpp"
#include "support.hpp"

namespace psim::platform::security {
namespace {
using namespace testing;  // NOLINT(google-build-using-namespace): local test helpers

TEST(SecuritySecret, ProtectedFilesAreBoundedAndNeverExposePathsOrContents) {
  Directory const directory;
  const auto file = directory.write("private-sensitive-path", "canary-secret\n", true);
  auto value = take(read_secret(file));
  EXPECT_EQ(value.view(), "canary-secret\n");
  Secret moved(std::move(value));
  EXPECT_EQ(moved.view(), "canary-secret\n");
  Secret assigned("old-sensitive-value");
  assigned = std::move(moved);
  EXPECT_EQ(assigned.view(), "canary-secret\n");
  EXPECT_FALSE(read_secret(file, 2));
  EXPECT_FALSE(read_secret(file, 0));
  EXPECT_FALSE(read_secret(file, (1024 * 1024) + 1));
  EXPECT_FALSE(read_secret(""));
  EXPECT_FALSE(read_secret(std::string("name\0suffix", 11)));
  const auto missing = read_secret((directory.path / "missing-sensitive-path").string());
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().message().find("sensitive"), std::string::npos);
  std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::group_read);
  EXPECT_FALSE(read_secret(file));
  std::filesystem::permissions(file, std::filesystem::perms::owner_read);
  std::filesystem::create_symlink(file, directory.path / "link");
  EXPECT_FALSE(read_secret((directory.path / "link").string()));
  EXPECT_FALSE(read_secret(directory.path.string()));
  EXPECT_EQ(take(read_secret(directory.write("empty", "", true))).view(), "");
}

TEST(SecurityTls, RejectsInvalidMaterialsAndHostnameAndPreservesLastGoodSnapshot) {
  Fixture const fixture;
  TlsReload reload;
  EXPECT_FALSE(reload.current());
  auto options = fixture.options(fixture.server, "server");
  check(reload.reload(options, true));
  auto previous = reload.current();
  EXPECT_EQ(SSL_CTX_get_min_proto_version(previous->native_handle()), TLS1_3_VERSION);
  options.private_key_file = "/missing-sensitive-key";
  auto failed = reload.reload(options, true);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().message().find("sensitive"), std::string::npos);
  EXPECT_EQ(reload.current(), previous);
  EXPECT_FALSE(configure_tls(nullptr, {}));
  EXPECT_FALSE(tls_context({}, true));
  EXPECT_FALSE(authenticated_peer(nullptr));
  const Handle<SSL, SSL_free> stream(SSL_new(previous->native_handle()), SSL_free);
  EXPECT_FALSE(authenticated_peer(stream.get()));
  EXPECT_FALSE(configure_hostname(nullptr, "localhost"));
  EXPECT_FALSE(configure_hostname(stream.get(), ""));
  EXPECT_FALSE(configure_hostname(stream.get(), "bad/name"));
  check(configure_hostname(stream.get(), "127.0.0.1"));
  check(configure_hostname(stream.get(), "localhost"));
  EXPECT_STREQ(SSL_get_servername(stream.get(), TLSEXT_NAMETYPE_host_name), "localhost");
  options = fixture.options(fixture.server, "server2");
  options.boundary = Boundary::kInternal;
  check(reload.reload(options, true));
  EXPECT_EQ(SSL_CTX_get_min_proto_version(reload.current()->native_handle()), TLS1_2_VERSION);
}

TEST(SecurityTls, ReloadSnapshotsAreSafeAcrossThreads) {
  Fixture const fixture;
  TlsReload reload;
  const auto options = fixture.options(fixture.server, "server");
  check(reload.reload(options, true));
  std::thread reader([&] {
    for (int i = 0; i < 1000; ++i) {
      EXPECT_NE(reload.current(), nullptr);
    }
  });
  for (int i = 0; i < 5; ++i) {
    check(reload.reload(options, true));
  }
  reader.join();
}

TEST(SecurityTls, MtlsChecksTrustNamesExpiryMissingCertAndProtocolFloor) {
  Fixture const fixture;
  auto server = take(tls_context(fixture.options(fixture.server, "server"), true));
  auto client = take(tls_context(fixture.options(fixture.client, "client")));
  auto success = run(handshake(server, client));
  ASSERT_TRUE(success.accepted) << success.error;
  if (success.peer) {
    EXPECT_EQ(success.peer->identity(), "psim://connector/connector1");
    EXPECT_EQ(success.peer->fingerprint().size(), 64);
  } else {
    FAIL() << "authenticated peer missing";
  }
  EXPECT_FALSE(run(handshake(server, client, "wrong.test")).accepted);
  auto no_certificate = fixture.options(fixture.client, "unused");
  no_certificate.certificate_file.clear();
  no_certificate.private_key_file.clear();
  EXPECT_FALSE(run(handshake(server, take(tls_context(no_certificate)))).accepted);
  Fixture const untrusted;
  EXPECT_FALSE(run(handshake(server, take(tls_context(untrusted.options(untrusted.client, "untrusted"))))).accepted);
  const auto expired = issue(7, "psim://connector/connector1", &fixture.ca, true);
  EXPECT_FALSE(run(handshake(server, take(tls_context(fixture.options(expired, "expired"))))).accepted);
  // Internal client can attempt TLS 1.2; external server only accepts TLS 1.3.
  auto legacy = fixture.options(fixture.client, "legacy");
  legacy.boundary = Boundary::kInternal;
  EXPECT_FALSE(run(handshake(server, take(tls_context(legacy)), "localhost", TLS1_2_VERSION)).accepted);
}

TEST(SecurityTls, FreshCrlRevokesAndStaleCrlFailsClosed) {
  Fixture const fixture;
  auto options = fixture.options(fixture.server, "server");
  options.crl_file = fixture.directory.write("clean.crl", crl(fixture.ca));
  auto client = take(tls_context(fixture.options(fixture.client, "client")));
  EXPECT_TRUE(run(handshake(take(tls_context(options, true)), client)).accepted);
  options.crl_file = fixture.directory.write("revoked.crl", crl(fixture.ca, &fixture.client));
  EXPECT_FALSE(run(handshake(take(tls_context(options, true)), client)).accepted);
  options.crl_file = fixture.directory.write("stale.crl", crl(fixture.ca, nullptr, true));
  EXPECT_FALSE(run(handshake(take(tls_context(options, true)), client)).accepted);
  options.crl_file = "missing-crl-sensitive-path";
  EXPECT_FALSE(tls_context(options, true));
}

TEST(SecurityTls, CsrHasVerifiedSignatureAndRestrictedIdentityWithoutCaSigning) {
  Fixture const fixture;
  const auto options = fixture.options(fixture.client, "client");
  const auto csr = take(make_csr(options.private_key_file, "psim://connector/connector1"));
  const Handle<BIO, BIO_free> bio(BIO_new_mem_buf(csr.data(), static_cast<int>(csr.size())), BIO_free);
  const Handle<X509_REQ, X509_REQ_free> request(PEM_read_bio_X509_REQ(bio.get(), nullptr, nullptr, nullptr),
                                                X509_REQ_free);
  ASSERT_NE(request, nullptr);
  EXPECT_EQ(X509_REQ_verify(request.get(), fixture.client.private_key.get()), 1);
  EXPECT_EQ(X509_REQ_get_version(request.get()), 0);
  EXPECT_FALSE(make_csr(options.private_key_file, "psim://connector/bad,id"));
  EXPECT_FALSE(make_csr(options.private_key_file, "https://foreign"));
  EXPECT_FALSE(make_csr("missing-secret", "psim://service/api-gateway"));
  const auto rsa = key(true);
  EXPECT_TRUE(make_csr(options.private_key_file, "psim://connector/" + std::string(128, 'a')));
  EXPECT_FALSE(make_csr(fixture.directory.write("rsa", private_pem(rsa.get()), true), "psim://service/api-gateway"));
}

TEST(SecurityJwt, VerifiesRs256AndEs256AndExtractsOnlyClientRolesAndTrustedScopes) {
  const auto now = std::chrono::system_clock::now();
  for (bool const rsa : {true, false}) {
    const auto native = key(rsa);
    auto verifier = take(JwtVerifier::create(jwt_options(), jwks(native.get())));
    auto document = claims(now);
    document["realm_access"] = {{"roles", {"admin"}}};
    document["resource_access"]["other"] = {{"roles", {"admin"}}};
    document["aud"] = {"other", "psim"};
    const auto token = sign(native.get(), document);
    EXPECT_TRUE(take(verifier.knows_key(token)));
    const auto principal = take(verifier.verify(token, now));
    EXPECT_EQ(principal.subject, "operator1");
    EXPECT_EQ(principal.tenant, "tenant1");
    EXPECT_EQ(principal.roles, (std::set<std::string>{"operator"}));
    EXPECT_EQ(principal.scopes.size(), 2);
    document.erase("psim_tenant");
    document.erase("psim_scopes");
    document.erase("azp");
    document.erase("resource_access");
    const auto sparse = take(verifier.verify(sign(native.get(), document), now));
    EXPECT_TRUE(sparse.tenant.empty());
    EXPECT_TRUE(sparse.roles.empty());
  }
}

TEST(SecurityJwt, RejectsAlgConfusionTokenSelectedKeysDuplicateFieldsAndTampering) {
  const auto native = key(true);
  auto verifier = take(JwtVerifier::create(jwt_options(), jwks(native.get())));
  const auto now = std::chrono::system_clock::now();
  for (const auto* alg : {"none", "HS256", "RS512", "ES256"}) {
    EXPECT_FALSE(verifier.verify(sign(native.get(), claims(now), "key1", {{"alg", alg}}), now));
  }
  for (const auto* name : {"jku", "jwk", "x5u", "crit", "b64"}) {
    EXPECT_FALSE(verifier.verify(sign(native.get(), claims(now), "key1", {{name, "hostile-canary"}}), now));
  }
  EXPECT_FALSE(verifier.verify(sign(native.get(), claims(now), "key1", {{"typ", "ID"}}), now));
  const auto duplicate =
      sign_text(native.get(), R"({"typ":"JWT","alg":"RS256","kid":"key1","kid":"key1"})", claims(now).dump());
  EXPECT_FALSE(verifier.verify(duplicate, now));
  auto token = sign(native.get(), claims(now));
  token.at(token.rfind('.') + 1) = token.at(token.rfind('.') + 1) == 'A' ? 'B' : 'A';
  EXPECT_FALSE(verifier.verify(token, now));
  EXPECT_FALSE(verifier.verify(sign(native.get(), claims(now), "unknown"), now));
  EXPECT_FALSE(take(verifier.knows_key(sign(native.get(), claims(now), "unknown"))));
  const auto foreign = key(true);
  EXPECT_FALSE(verifier.verify(sign(foreign.get(), claims(now)), now));
}

TEST(SecurityJwt, RejectsMalformedAndOversizedTokenAndInvalidUtf8) {
  const auto native = key();
  auto verifier = take(JwtVerifier::create(jwt_options(), jwks(native.get())));
  const auto now = std::chrono::system_clock::now();
  for (const auto* token : {"", "a", "a.b", "a.b.c.d", "A.e30.A", "e30=.e30.AA", "e30.e30.AB", "e30.e30.??"}) {
    EXPECT_FALSE(verifier.verify(token, now));
    EXPECT_FALSE(verifier.knows_key(token));
  }
  EXPECT_FALSE(verifier.verify(std::string(16385, 'a'), now));
  EXPECT_FALSE(verifier.verify(sign_text(native.get(), R"({"typ":"JWT","alg":"ES256","kid":"key1"})", "\xff"), now));
  EXPECT_FALSE(verifier.verify(sign_text(native.get(), std::string(2049, 'x'), "{}"), now));
}

TEST(SecurityJwt, ValidatesIssuerAudienceTokenKindSubjectAndClaimTypes) {
  const auto native = key();
  const auto now = std::chrono::system_clock::now();
  auto verifier = take(JwtVerifier::create(jwt_options(), jwks(native.get())));
  for (const auto* name : {"iss", "aud", "sub", "typ", "iat", "exp"}) {
    auto document = claims(now);
    document.erase(name);
    EXPECT_FALSE(verifier.verify(sign(native.get(), document), now)) << name;
  }
  for (const auto* name : {"iss", "aud", "sub", "typ", "psim_tenant", "azp"}) {
    auto document = claims(now);
    document[name] = 10;
    EXPECT_FALSE(verifier.verify(sign(native.get(), document), now)) << name;
  }
  for (const auto* name : {"iss", "aud", "typ"}) {
    auto document = claims(now);
    document[name] = "foreign";
    EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  }
  auto document = claims(now);
  document["resource_access"]["psim"]["roles"] = {"operator", 1};
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  document = claims(now);
  document["psim_scopes"] = "*";
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  document = claims(now);
  document["psim_scopes"] = Json::array({""});
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  document = claims(now);
  document["resource_access"] = "admin";
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
}

TEST(SecurityJwt, EnforcesLifetimeExpiryNotBeforeIssuedAtAndBoundedSkew) {
  const auto native = key();
  const auto now = std::chrono::system_clock::now();
  auto verifier = take(JwtVerifier::create(jwt_options(), jwks(native.get())));
  EXPECT_TRUE(verifier.verify(sign(native.get(), claims(now)), now + std::chrono::seconds(329)));
  const auto expired = verifier.verify(sign(native.get(), claims(now)), now + std::chrono::seconds(330));
  ASSERT_FALSE(expired);
  EXPECT_EQ(expired.error().code(), ErrorCode::kAuthTokenExpired);
  auto document = claims(now);
  document["nbf"] = seconds(now) + 31;
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  document["nbf"] = seconds(now) + 30;
  EXPECT_TRUE(verifier.verify(sign(native.get(), document), now));
  document["nbf"] = document["exp"];
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  document = claims(now + std::chrono::seconds(31));
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  document = claims(now);
  document["exp"] = seconds(now) + 301;
  EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  for (const auto& number : Json::array({-1, 1.5, 18446744073709551615ULL})) {
    document["exp"] = number;
    EXPECT_FALSE(verifier.verify(sign(native.get(), document), now));
  }
  auto options = jwt_options();
  options.clock_skew = std::chrono::seconds(31);
  EXPECT_FALSE(JwtVerifier::create(options, jwks(native.get())));
  options = jwt_options();
  options.max_lifetime = std::chrono::seconds(301);
  EXPECT_FALSE(JwtVerifier::create(options, jwks(native.get())));
  options = jwt_options();
  options.audience.clear();
  EXPECT_FALSE(JwtVerifier::create(options, jwks(native.get())));
}

TEST(SecurityJwt, RejectsBadJwksDuplicateKidsPrivateMaterialWeakRsaAndInvalidEcPoints) {
  const auto native = key();
  const auto public_key = jwk(native.get());
  auto document = Json{{"keys", Json::array({public_key, public_key})}};
  EXPECT_FALSE(JwtVerifier::create(jwt_options(), document.dump()));
  for (const auto* name : {"d", "p", "q", "dp", "dq", "qi", "oth", "k"}) {
    document = {{"keys", Json::array({public_key})}};
    document["keys"][0][name] = "private-canary";
    EXPECT_FALSE(JwtVerifier::create(jwt_options(), document.dump()));
  }
  for (const auto* name : {"kid", "x", "y", "crv", "kty"}) {
    document = {{"keys", Json::array({public_key})}};
    document["keys"][0].erase(name);
    EXPECT_FALSE(JwtVerifier::create(jwt_options(), document.dump()));
  }
  document = {{"keys", Json::array({public_key})}};
  document["keys"][0]["x"] = base64(std::string(32, '\0'));
  document["keys"][0]["y"] = base64(std::string(32, '\0'));
  EXPECT_FALSE(JwtVerifier::create(jwt_options(), document.dump()));
  const auto rsa = key(true);
  document = {{"keys", Json::array({jwk(rsa.get())})}};
  document["keys"][0]["n"] = base64(std::string(128, '\x01'));
  EXPECT_FALSE(JwtVerifier::create(jwt_options(), document.dump()));
  document = {{"keys", Json::array({public_key})}};
  document["keys"][0]["key_ops"] = {"sign"};
  EXPECT_FALSE(JwtVerifier::create(jwt_options(), document.dump()));
  EXPECT_FALSE(JwtVerifier::create(jwt_options(), R"({"keys":[]})"));
  EXPECT_FALSE(JwtVerifier::create(jwt_options(), "invalid"));
  document = {{"keys", Json::array({public_key})}};
  document["keys"][0].erase("alg");
  EXPECT_TRUE(JwtVerifier::create(jwt_options(), document.dump()));
  auto encryption = jwk(rsa.get(), "encryption");
  encryption["use"] = "enc";
  encryption["alg"] = "RSA-OAEP";
  document["keys"].push_back(encryption);
  EXPECT_TRUE(JwtVerifier::create(jwt_options(), document.dump()));
}

TEST(SecurityJwks, RotatesKeysBoundsOutagesAndThrottlesUnknownKidRequests) {
  const auto first = key();
  const auto second = key();
  auto now = std::chrono::system_clock::now();
  auto payload = claims(now);
  std::string response = jwks(first.get());
  bool unavailable = false;
  int requests = 0;
  auto fetch =
      [&]() -> asio::awaitable<Result<std::string>> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    ++requests;
    if (unavailable) {
      co_return fail(ErrorCode::kCommonUnavailable, "IdP offline");
    }
    co_return response;
  };
  JwksOptions const options{.jwt = jwt_options(), .url = "https://keycloak.test/certs"};
  auto cache = take(JwksCache::create(options, fetch, [&] { return now; }));
  EXPECT_TRUE(run(cache->verify(sign(first.get(), payload))));
  EXPECT_EQ(requests, 1);
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(run(cache->verify(sign(second.get(), payload, "unknown"))));
  }
  EXPECT_EQ(requests, 1);
  now += std::chrono::seconds(5);
  response = jwks(second.get(), "new-key");
  EXPECT_TRUE(run(cache->verify(sign(second.get(), payload, "new-key"))));
  EXPECT_EQ(requests, 2);
  EXPECT_FALSE(run(cache->verify(sign(first.get(), payload))));
  unavailable = true;
  now += std::chrono::seconds(61);
  EXPECT_TRUE(run(cache->verify(sign(second.get(), payload, "new-key"))));
  EXPECT_EQ(requests, 3);
  now += std::chrono::seconds(241);
  EXPECT_FALSE(run(cache->verify(sign(second.get(), claims(now), "new-key"))));
  EXPECT_EQ(requests, 4);
  unavailable = false;
  response = "invalid replacement";
  EXPECT_FALSE(run(cache->refresh()));
  EXPECT_FALSE(run(cache->verify(sign(second.get(), claims(now), "new-key"))));
  response = jwks(second.get(), "new-key");
  check(run(cache->refresh()));
  EXPECT_TRUE(run(cache->verify(sign(second.get(), claims(now), "new-key"))));
  const auto saved = now;
  now -= std::chrono::seconds(1);
  EXPECT_FALSE(run(cache->verify(sign(second.get(), claims(saved), "new-key"))));
}

TEST(SecurityIntrospection, RevocationOutagesAndReferenceScopesAreBounded) {
  const auto native = key();
  auto now = std::chrono::system_clock::now();
  auto payload = claims(now);
  payload.erase("psim_scopes");
  payload["psim_scope_set"] = "permission-set";
  auto response = payload;
  response["active"] = true;
  response["client_id"] = payload.at("azp");
  response["psim_scopes"] = Json::array();
  for (int i = 0; i < 1000; ++i) {
    response["psim_scopes"].push_back("site:" + std::to_string(i));
  }
  bool unavailable = false;
  int inspections = 0;
  auto fetch =
      [&]() -> asio::awaitable<Result<std::string>> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // test joins before closure destruction.
    co_return jwks(native.get());
  };
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): run joins while the callback is owned.
  auto inspect = [&](std::string)
      -> asio::awaitable<Result<std::string>> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                 // test joins before closure destruction.
    ++inspections;
    if (unavailable) {
      co_return fail(ErrorCode::kCommonUnavailable, "IdP unavailable");
    }
    co_return response.dump();
  };
  JwksOptions options{.jwt = jwt_options(),
                      .url = "https://keycloak.test/certs",
                      .introspection_url = "https://keycloak.test/introspect",
                      .introspection_client = "psim",
                      .introspection_secret = "test-secret"};
  auto cache = take(JwksCache::create(options, fetch, [&] { return now; }, inspect));
  const auto token = sign(native.get(), payload);
  const auto first = run(cache->verify(token));
  ASSERT_TRUE(first);
  EXPECT_EQ(first->scopes.size(), 1000U);
  EXPECT_TRUE(run(cache->verify(token)));
  EXPECT_EQ(inspections, 1);
  now += std::chrono::seconds(1);
  response["active"] = false;
  EXPECT_FALSE(run(cache->verify(token)));
  now += std::chrono::seconds(1);
  response["active"] = true;
  unavailable = true;
  EXPECT_EQ(run(cache->verify(token)).error().code(), ErrorCode::kCommonUnavailable);
  now += std::chrono::seconds(1);
  unavailable = false;
  response["psim_scope_set"] = "another-set";
  EXPECT_FALSE(run(cache->verify(token)));
  now += std::chrono::seconds(1);
  response["psim_scope_set"] = "permission-set";
  response["sub"] = "different-user";
  EXPECT_FALSE(run(cache->verify(token)));
  EXPECT_FALSE(run(cache->verify("malformed")));
  EXPECT_EQ(inspections, 5);
  response["sub"] = payload.at("sub");
  const auto valid = response;
  for (const auto* field : {"iss", "client_id", "psim_tenant", "exp", "resource_access", "psim_scopes"}) {
    SCOPED_TRACE(field);
    response = valid;
    response[field] = nullptr;
    now += std::chrono::seconds(1);
    EXPECT_FALSE(run(cache->verify(token)));
  }
  response = valid;
  response["psim_scopes"] = Json::array();
  for (int i = 0; i < 10001; ++i) {
    response["psim_scopes"].push_back("zone:" + std::to_string(i));
  }
  now += std::chrono::seconds(1);
  EXPECT_FALSE(run(cache->verify(token)));
  response = valid;
  response["resource_access"]["psim"]["roles"] = Json::array({"admin"});
  now += std::chrono::seconds(1);
  EXPECT_FALSE(run(cache->verify(token)));
  payload["psim_scopes"] = Json::array({"*"});
  EXPECT_FALSE(run(cache->verify(sign(native.get(), payload))));
  options.introspection_secret.clear();
  EXPECT_FALSE(JwksCache::create(options));
}

// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines): with_https joins all operations before their
// captures leave scope.
TEST(SecurityIntrospection, UsesVerifiedHttpsAndRejectsInvalidInlineClaimsAndServerFailures) {
  const Fixture fixture;
  const auto native = key();
  const auto payload = claims(std::chrono::system_clock::now());
  auto response = payload;
  response["active"] = true;
  response["client_id"] = payload.at("azp");
  for (const auto status : {200U, 503U}) {
    for (const auto valid : {true, false}) {
      auto document = response;
      if (!valid) {
        document["psim_scopes"] = Json::array({"*"});
      }
      auto operation = [&](std::string url)
          -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                      // joined before closure destruction.
        auto cache = take(JwksCache::create(
            {.jwt = jwt_options(),
             .url = url + "/certs",
             .http = {.tls = fixture.options(fixture.client, "introspection-client")},
             .introspection_url = url + "/introspect",
             .introspection_client = "psim",
             .introspection_secret = "secret+&=%"},
            [&]() -> asio::awaitable<
                      Result<std::string>> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                              // native key outlives joined operation.
              co_return jwks(native.get());
            }));
        auto result = co_await cache->verify(sign(native.get(), payload));
        EXPECT_EQ(result.has_value(), valid && status == 200);
      };
      run(with_https(&fixture, document.dump(), status, operation));
    }
  }
}

// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)

TEST(SecurityJwks, MalformedTokensNeverCauseFetchAndConfigurationIsValidated) {
  int requests = 0;
  auto fetch =
      [&]() -> asio::awaitable<Result<std::string>> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    ++requests;
    co_return "invalid";
  };
  JwksOptions options{.jwt = jwt_options(), .url = "https://keycloak.test/certs"};
  auto cache = take(JwksCache::create(options, fetch));
  EXPECT_FALSE(run(cache->verify("hostile-canary")));
  EXPECT_EQ(requests, 0);
  options.url = "http://keycloak.test/certs";
  EXPECT_FALSE(JwksCache::create(options));
  options.url = "https://keycloak.test/certs";
  options.jwt.issuer.clear();
  EXPECT_FALSE(JwksCache::create(options));
  options.jwt = jwt_options();
  options.max_stale = std::chrono::seconds(1);
  EXPECT_FALSE(JwksCache::create(options));
}

TEST(SecurityTrust, RenewalKeepsOldSessionRevocationClosesOnlyOldAndRecoveryClosesAll) {
  const Fixture fixture;
  const auto old = peer(fixture, fixture.client, "old");
  const auto replacement_certificate = issue(4, "psim://connector/connector1", &fixture.ca);
  const auto replacement = peer(fixture, replacement_certificate, "new");
  const auto third = peer(fixture, issue(5, "psim://connector/connector1", &fixture.ca), "third");
  const auto now = std::chrono::system_clock::now();
  ConnectorTrust trust;
  check(trust.enroll("connector1", "tenant1", old, now));
  EXPECT_FALSE(trust.enroll("connector1", "tenant1", old, now));
  int old_closed = 0;
  int new_closed = 0;
  check(trust.attach(
      "old-session", "connector1", old,
      [&] {
        ++old_closed;
        trust.detach("old-session");
      },
      now));
  check(trust.renew("connector1", old, replacement, now));
  EXPECT_EQ(old_closed, 0);
  EXPECT_EQ(take(trust.authenticate("connector1", old, now)), "tenant1");
  EXPECT_EQ(take(trust.authenticate("connector1", replacement, now)), "tenant1");
  // NOLINTNEXTLINE(readability-suspicious-call-argument): the replacement is current during the third renewal
  const auto limit = trust.renew("connector1", replacement, third, now);
  ASSERT_FALSE(limit);
  EXPECT_EQ(limit.error().code(), ErrorCode::kCatalogCertificateLimit);
  check(trust.attach("new-session", "connector1", replacement, [&] { ++new_closed; }, now));
  check(trust.revoke("connector1", old.fingerprint()));
  EXPECT_EQ(old_closed, 1);
  EXPECT_EQ(new_closed, 0);
  EXPECT_FALSE(trust.authenticate("connector1", old, now));
  // NOLINTNEXTLINE(readability-suspicious-call-argument): intentionally try to restore the revoked old certificate
  EXPECT_FALSE(trust.renew("connector1", replacement, old, now));
  EXPECT_FALSE(trust.recover("connector1", old, now));
  EXPECT_FALSE(trust.recover("connector1", replacement, now));
  check(trust.revoke("connector1", old.fingerprint()));
  EXPECT_EQ(old_closed, 1);
  check(trust.recover("connector1", third, now));
  EXPECT_EQ(new_closed, 1);
  EXPECT_FALSE(trust.authenticate("connector1", replacement, now));
  EXPECT_TRUE(trust.authenticate("connector1", third, now));
  trust.expire(third.expires_at());
  EXPECT_FALSE(trust.authenticate("connector1", third, third.expires_at()));
}

TEST(SecurityTrust, UnknownMismatchExpiredIdentityAndBoundedSessionsAreRefused) {
  const Fixture fixture;
  const auto certificate = peer(fixture, fixture.client, "client");
  const auto other = peer(fixture, issue(4, "psim://connector/other", &fixture.ca), "other");
  const auto now = std::chrono::system_clock::now();
  ConnectorTrust trust(1, 1);
  EXPECT_FALSE(trust.authenticate("connector1", certificate, now));
  EXPECT_FALSE(trust.enroll("wrong", "tenant1", certificate, now));
  EXPECT_FALSE(trust.enroll("connector1", "", certificate, now));
  EXPECT_FALSE(trust.enroll("connector1", "tenant1", certificate, certificate.expires_at()));
  check(trust.enroll("connector1", "tenant1", certificate, now));
  EXPECT_FALSE(trust.enroll("other", "tenant1", other, now));
  EXPECT_FALSE(trust.renew("connector1", certificate, other, now));
  EXPECT_FALSE(trust.renew("connector1", certificate, certificate, now));
  EXPECT_FALSE(trust.recover("unknown", other, now));
  EXPECT_FALSE(trust.revoke("unknown", certificate.fingerprint()));
  EXPECT_FALSE(trust.revoke("connector1", ""));
  int closed = 0;
  EXPECT_FALSE(trust.attach("", "connector1", certificate, [] {}, now));
  EXPECT_FALSE(trust.attach("a", "connector1", certificate, {}, now));
  check(trust.attach("a", "connector1", certificate, [&] { ++closed; }, now));
  EXPECT_FALSE(trust.attach("a", "connector1", certificate, [] {}, now));
  EXPECT_FALSE(trust.attach("b", "connector1", certificate, [] {}, now));
  trust.expire(certificate.expires_at());
  EXPECT_EQ(closed, 1);
  trust.detach("missing");
}

TEST(SecurityTrust, ExpiryClosesSessionsAfterRenewalPrunesTheirCertificate) {
  const Fixture fixture;
  const auto original = peer(fixture, fixture.client, "original");
  const auto next_certificate = issue(4, "psim://connector/connector1", &fixture.ca);
  const auto third_certificate = issue(5, "psim://connector/connector1", &fixture.ca);
  for (const auto* certificate : {&next_certificate, &third_certificate}) {
    require(X509_gmtime_adj(X509_getm_notAfter(certificate->native.get()), 8L * 24 * 3600) != nullptr);
    require(X509_sign(certificate->native.get(), fixture.ca.private_key.get(), EVP_sha256()) > 0);
  }
  const auto next = peer(fixture, next_certificate, "next");
  const auto third = peer(fixture, third_certificate, "third");
  const auto now = std::chrono::system_clock::now();
  ConnectorTrust trust;
  check(trust.enroll("connector1", "tenant1", original, now));
  int closed = 0;
  check(trust.attach("old", "connector1", original, [&] { ++closed; }, now));
  check(trust.renew("connector1", original, next, now));
  check(trust.renew("connector1", next, third, original.expires_at()));
  EXPECT_EQ(closed, 0);
  trust.expire(original.expires_at());
  EXPECT_EQ(closed, 1);
  EXPECT_TRUE(trust.authenticate("connector1", next, original.expires_at()));
  trust.expire(original.expires_at());
  EXPECT_EQ(closed, 1);
}

TEST(SecurityService, RequiresBothServiceCertificateAndTokenWithAllowedIdentity) {
  const Fixture fixture;
  const auto service = peer(fixture, issue(4, "psim://service/api-gateway", &fixture.ca), "service");
  const auto connector = peer(fixture, fixture.client, "connector");
  const auto native = key();
  const auto now = std::chrono::system_clock::now();
  auto verifier = take(JwtVerifier::create(jwt_options(), jwks(native.get())));
  auto token = take(verifier.verify(sign(native.get(), claims(now)), now));
  check(authenticate_service(service, token, {"api-gateway"}, false, now));
  EXPECT_FALSE(authenticate_service(service, token, {"normalizer"}, false, now));
  EXPECT_FALSE(authenticate_service(connector, token, {"api-gateway"}, false, now));
  token.authorized_party = "other-client";
  EXPECT_FALSE(authenticate_service(service, token, {"api-gateway"}, false, now));
  check(authenticate_service(service, token, {"api-gateway"}, true, now));
  token.expires_at = now;
  EXPECT_FALSE(authenticate_service(service, token, {"api-gateway"}, true, now));
}

TEST(SecurityHttp, StrictUrlsAndLimitsRejectUserinfoFragmentsAndDowngrades) {
  for (const auto* url : {"https://example.test/path", "https://127.0.0.1:443/", "https://[::1]:8443/certs"}) {
    EXPECT_TRUE(validate_url(url));
  }
  for (const auto* url : {"http://example.test", "file:///etc/passwd", "https://user:secret@example.test",
                          "https://example.test/#fragment", "https://example.test:0", "https://example.test:99999",
                          "https://[::1]:443:444/", "https://example.test:bad", "https://", "https://bad host/",
                          "https://example.test/\r\nheader"}) {
    EXPECT_FALSE(validate_url(url));
  }
  EXPECT_TRUE(validate_url("http://localhost:80/", true));
  EXPECT_FALSE(run(http_request("https://localhost", {.timeout = std::chrono::milliseconds(0)})));
  EXPECT_FALSE(run(http_request("https://localhost", {.max_body_bytes = 0})));
  EXPECT_FALSE(run(http_request("https://localhost", {}, "body", "bad\r\nheader")));
}

TEST(SecurityHttp, HttpsSupportsGetAndPostAndRefusesUntrustedPeers) {
  const Fixture fixture;
  auto options = HttpOptions{.tls = fixture.options(fixture.client, "http-client")};
  auto get =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    const auto response = take(co_await http_request(std::move(url), options));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "response-canary");
  };
  run(with_https(&fixture, "response-canary", 200, get));
  auto post =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    const auto response =
        take(co_await http_request(std::move(url), options, "secret-body-canary", "application/json"));
    EXPECT_EQ(response.status, 200);
  };
  run(with_https(&fixture, "ok", 200, post));
  const Fixture wrong;
  auto untrusted =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    const auto response =
        co_await http_request(std::move(url), {.tls = wrong.options(wrong.client, "untrusted-client")});
    EXPECT_FALSE(response);
    if (!response) {
      EXPECT_EQ(response.error().message().find("canary"), std::string::npos);
    }
  };
  run(with_https(&fixture, "never received", 200, untrusted));
}

TEST(SecurityHttp, RefusesOversizedResponsesDoesNotFollowRedirectsAndBoundsTotalDeadline) {
  const Fixture fixture;
  auto options = HttpOptions{.tls = fixture.options(fixture.client, "http-client"), .max_body_bytes = 10};
  auto oversized =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    EXPECT_FALSE(co_await http_request(std::move(url), options));
  };
  run(with_https(&fixture, std::string(100, 'x'), 200, oversized));
  auto redirect =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    const auto response = take(co_await http_request(std::move(url), options));
    EXPECT_EQ(response.status, 302);
  };
  run(with_https(&fixture, "", 302, redirect));
  options.timeout = std::chrono::milliseconds(20);
  auto timeout =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    const auto response = co_await http_request(std::move(url), options);
    EXPECT_FALSE(response);
    if (!response) {
      EXPECT_EQ(response.error().code(), ErrorCode::kCommonDeadlineExceeded);
    }
  };
  run(with_https(&fixture, "", 200, timeout, std::chrono::milliseconds(100)));
  EXPECT_FALSE(run(http_request("https://127.0.0.1:1/", options)));
  options.allow_plaintext = true;
  EXPECT_FALSE(run(http_request("http://127.0.0.1:1/", options)));
  EXPECT_FALSE(run(http_request("https://no-such-host.invalid:443/", options)));
}

TEST(SecurityJwks, FetchesOverVerifiedHttpsAndKeepsLastGoodKeysOnMalformedReplacement) {
  const Fixture fixture;
  const auto native = key();
  const auto payload = claims();
  auto operation =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    JwksOptions const options{
        .jwt = jwt_options(), .url = url + "/certs", .http = {.tls = fixture.options(fixture.client, "http-client")}};
    auto cache = take(JwksCache::create(options));
    EXPECT_TRUE(co_await cache->verify(sign(native.get(), payload)));
    EXPECT_TRUE(co_await cache->verify(sign(native.get(), payload)));
  };
  run(with_https(&fixture, jwks(native.get()), 200, operation));
  auto failed =
      [&](std::string url) -> asio::awaitable<void> {  // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines):
                                                       // closure outlives the structured join or run
    auto cache = take(JwksCache::create({.jwt = jwt_options(),
                                         .url = std::move(url),
                                         .http = {.tls = fixture.options(fixture.client, "http-client2")}}));
    EXPECT_FALSE(co_await cache->refresh());
  };
  run(with_https(&fixture, "rejected", 503, failed));
}
}  // namespace
}  // namespace psim::platform::security

namespace psim::platform::security {
namespace {
TEST(SecurityHttp, RejectsPrivateResolvedAddressesAndReservedApplicationHeaders) {
  for (const auto* url : {"https://127.0.0.1:1/", "https://localhost:1/", "https://[::1]:1/"}) {
    auto result = run(http_request(url, {.public_network_only = true}));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::kAuthForbidden);
  }
  for (const auto* header :
       {"Host", "Authorization", "Content-Length", "Transfer-Encoding", "Connection", "Content-Type", "Bad\r\nName"}) {
    HttpOptions options;
    options.headers = {{header, "value"}};
    auto result = run(http_request("https://127.0.0.1:1/", options));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::kCommonInvalidArgument);
  }
  HttpOptions options;
  options.headers = {{"X-PSIM-Signature", "bad\r\ninjection"}};
  auto result = run(http_request("https://127.0.0.1:1/", options));
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code(), ErrorCode::kCommonInvalidArgument);
}
}  // namespace
}  // namespace psim::platform::security
