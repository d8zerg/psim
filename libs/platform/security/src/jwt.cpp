#include "psim/platform/security/jwt.hpp"

#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#include <openssl/rsa.h>
#include <openssl/types.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crypto.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security {
namespace {
using Json = nlohmann::json;
template <typename T, auto Free>
using Handle = std::unique_ptr<T, decltype(Free)>;

std::unexpected<Error> denied() {
  return fail(ErrorCode::kAuthUnauthenticated, "invalid access token or signing keys");
}

bool valid_options(const JwtOptions& options) {
  return !options.issuer.empty() && options.issuer.size() <= 2048 && !options.audience.empty()
         && options.audience.size() <= 1024 && !options.roles_client.empty() && options.roles_client.size() <= 1024
         && options.clock_skew >= std::chrono::seconds::zero() && options.clock_skew <= std::chrono::seconds(30)
         && options.max_lifetime > std::chrono::seconds::zero() && options.max_lifetime <= std::chrono::seconds(300);
}

Result<std::string> decode(std::string_view value) {
  if (value.empty() || value.size() % 4 == 1 || value.size() > 16384) {
    return denied();
  }
  constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  unsigned int accumulator = 0;
  unsigned int bits = 0;
  for (const char c : value) {
    const auto digit = kAlphabet.find(c);
    if (digit == std::string_view::npos) {
      return denied();
    }
    accumulator = (accumulator << 6U) | static_cast<unsigned int>(digit);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<char>((accumulator >> bits) & 255U));
    }
  }
  if (bits != 0 && (accumulator & ((1U << bits) - 1U)) != 0) {
    return denied();
  }
  return output;
}

Result<Json> parse_json(std::string_view value, std::size_t limit) {
  if (value.size() > limit) {
    return denied();
  }
  std::vector<std::set<std::string>> objects;
  bool invalid = false;
  auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
    if (depth > 32) {
      invalid = true;
      return false;
    }
    if (event == Json::parse_event_t::object_start) {
      objects.emplace_back();
    } else if (event == Json::parse_event_t::object_end) {
      if (!objects.empty()) {
        objects.pop_back();
      }
    } else if (event == Json::parse_event_t::key && !objects.empty()) {
      if (!objects.back().insert(parsed.get<std::string>()).second) {
        invalid = true;
      }
    }
    return true;
  };
  auto document = Json::parse(value, callback, false);
  if (invalid || document.is_discarded() || !document.is_object()) {
    return denied();
  }
  return document;
}

Result<std::string> field(const Json& object, std::string_view name, bool required = true) {
  const auto it = object.find(name);
  if (it == object.end() && !required) {
    return std::string{};
  }
  if (it == object.end() || !it->is_string() || it->get_ref<const std::string&>().empty()
      || it->get_ref<const std::string&>().size() > 1024
      || it->get_ref<const std::string&>().find('\0') != std::string::npos) {
    return denied();
  }
  return it->get<std::string>();
}

struct Token {
  Json header;
  Json claims;
  std::string signature;
  std::string_view signed_text;
  std::string kid;
  std::string algorithm;
};

Result<Token> parse_token(std::string_view token) {
  if (token.size() > 16384) {
    return denied();
  }
  const auto first = token.find('.');
  const auto last = token.rfind('.');
  if (first == std::string_view::npos || first == last || token.find('.', first + 1) != last) {
    return denied();
  }
  auto head_bytes = decode(token.substr(0, first));
  auto claim_bytes = decode(token.substr(first + 1, last - first - 1));
  auto signature = decode(token.substr(last + 1));
  if (!head_bytes || !claim_bytes || !signature || signature->size() > 1024) {
    return denied();
  }
  auto header = parse_json(*head_bytes, 2048);
  auto claims = parse_json(*claim_bytes, 12288);
  if (!header || !claims) {
    return denied();
  }
  auto kid = field(*header, "kid");
  auto algorithm = field(*header, "alg");
  auto type = field(*header, "typ");
  if (!kid || !algorithm || !type || (*algorithm != "RS256" && *algorithm != "ES256")
      || (*type != "JWT" && *type != "at+jwt") || header->contains("crit") || header->contains("b64")
      || header->contains("jku") || header->contains("jwk") || header->contains("x5u")) {
    return denied();
  }
  return Token{std::move(*header),    std::move(*claims), std::move(*signature),
               token.substr(0, last), std::move(*kid),    std::move(*algorithm)};
}

struct Key {
  std::string algorithm;
  std::shared_ptr<EVP_PKEY> native;
};

Result<Key> signing_key(const Json& document, std::string algorithm) {
  const Handle<OSSL_PARAM_BLD, OSSL_PARAM_BLD_free> builder(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
  if (!builder) {
    return denied();
  }
  Handle<BIGNUM, BN_free> modulus(nullptr, BN_free);
  Handle<BIGNUM, BN_free> exponent(nullptr, BN_free);
  std::string point;
  const char* kind = algorithm == "RS256" ? "RSA" : "EC";
  if (algorithm == "RS256") {
    auto n = field(document, "n");
    auto e = field(document, "e");
    if (!n || !e) {
      return denied();
    }
    auto n_bytes = decode(*n);
    auto e_bytes = decode(*e);
    if (!n_bytes || !e_bytes || n_bytes->size() < 256 || n_bytes->size() > 1024 || e_bytes->size() > 4
        || n_bytes->front() == '\0' || e_bytes->front() == '\0') {
      return denied();
    }
    modulus.reset(BN_bin2bn(detail::octets(*n_bytes), static_cast<int>(n_bytes->size()), nullptr));
    exponent.reset(BN_bin2bn(detail::octets(*e_bytes), static_cast<int>(e_bytes->size()), nullptr));
    if (!modulus || !exponent || BN_num_bits(modulus.get()) < 2048 || BN_get_word(exponent.get()) < 65537
        || BN_is_odd(exponent.get()) != 1
        || OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_N, modulus.get()) != 1
        || OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_E, exponent.get()) != 1) {
      return denied();
    }
  } else {
    auto curve = field(document, "crv");
    auto x = field(document, "x");
    auto y = field(document, "y");
    if (!curve || *curve != "P-256" || !x || !y) {
      return denied();
    }
    auto x_bytes = decode(*x);
    auto y_bytes = decode(*y);
    if (!x_bytes || !y_bytes || x_bytes->size() != 32 || y_bytes->size() != 32) {
      return denied();
    }
    point = std::string(1, '\4') + *x_bytes + *y_bytes;
    if (OSSL_PARAM_BLD_push_utf8_string(builder.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) != 1
        || OSSL_PARAM_BLD_push_octet_string(builder.get(), OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()) != 1) {
      return denied();
    }
  }
  const Handle<OSSL_PARAM, OSSL_PARAM_free> parameters(OSSL_PARAM_BLD_to_param(builder.get()), OSSL_PARAM_free);
  const Handle<EVP_PKEY_CTX, EVP_PKEY_CTX_free> context(EVP_PKEY_CTX_new_from_name(nullptr, kind, nullptr),
                                                        EVP_PKEY_CTX_free);
  EVP_PKEY* native = nullptr;
  if (!parameters || !context || EVP_PKEY_fromdata_init(context.get()) != 1
      || EVP_PKEY_fromdata(context.get(), &native, EVP_PKEY_PUBLIC_KEY, parameters.get()) != 1) {
    EVP_PKEY_free(native);
    return denied();
  }
  std::shared_ptr<EVP_PKEY> key(native, EVP_PKEY_free);
  const Handle<EVP_PKEY_CTX, EVP_PKEY_CTX_free> check(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
  if (!check || EVP_PKEY_public_check(check.get()) != 1) {
    return denied();
  }
  return Key{std::move(algorithm), std::move(key)};
}

bool verify_signature(const Key& key, const Token& token) {
  std::string signature = token.signature;
  if (key.algorithm == "ES256") {
    if (signature.size() != 64) {
      return false;
    }
    const auto bytes = std::string_view(signature);
    Handle<BIGNUM, BN_free> r(BN_bin2bn(detail::octets(bytes), 32, nullptr), BN_free);
    Handle<BIGNUM, BN_free> s(BN_bin2bn(detail::octets(bytes.substr(32)), 32, nullptr), BN_free);
    const Handle<ECDSA_SIG, ECDSA_SIG_free> der(ECDSA_SIG_new(), ECDSA_SIG_free);
    if (!r || !s || !der || ECDSA_SIG_set0(der.get(), r.get(), s.get()) != 1) {
      return false;
    }
    (void)r.release();
    (void)s.release();
    const auto size = i2d_ECDSA_SIG(der.get(), nullptr);
    if (size <= 0) {
      return false;
    }
    signature.resize(static_cast<std::size_t>(size));
    auto* output = detail::writable_octets(signature);
    if (i2d_ECDSA_SIG(der.get(), &output) != size) {
      return false;
    }
  }
  const Handle<EVP_MD_CTX, EVP_MD_CTX_free> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  EVP_PKEY_CTX* parameters = nullptr;
  if (!digest || EVP_DigestVerifyInit(digest.get(), &parameters, EVP_sha256(), nullptr, key.native.get()) != 1
      || (key.algorithm == "RS256" && EVP_PKEY_CTX_set_rsa_padding(parameters, RSA_PKCS1_PADDING) != 1)) {
    return false;
  }
  return EVP_DigestVerify(digest.get(), detail::octets(signature), signature.size(), detail::octets(token.signed_text),
                          token.signed_text.size())
         == 1;
}

Result<std::int64_t> numeric_date(const Json& claims, std::string_view name) {
  const auto it = claims.find(name);
  if (it == claims.end() || !it->is_number_integer()
      || (it->is_number_unsigned() && it->get<std::uint64_t>() > 253402300799ULL)) {
    return denied();
  }
  const auto value = it->get<std::int64_t>();
  if (value < 0 || value > 253402300799LL) {
    return denied();
  }
  return value;
}

Result<std::set<std::string>> string_set(const Json& claims, std::string_view name) {
  const auto it = claims.find(name);
  std::set<std::string> values;
  if (it == claims.end()) {
    return values;
  }
  if (!it->is_array() || it->size() > 256) {
    return denied();
  }
  for (const auto& item : *it) {
    if (!item.is_string() || item.get_ref<const std::string&>().empty()
        || item.get_ref<const std::string&>().size() > 512) {
      return denied();
    }
    values.insert(item.get<std::string>());
  }
  return values;
}

boost::asio::awaitable<Result<std::string>> fetch_keys(std::string url, HttpOptions options) {
  auto response = co_await http_request(std::move(url), std::move(options));
  if (!response) {
    co_return std::unexpected(response.error());
  }
  if (response->status != 200) {
    co_return fail(ErrorCode::kCommonUnavailable, "JWKS endpoint rejected request");
  }
  co_return std::move(response->body);
}
}  // namespace

struct JwtVerifier::State {
  JwtOptions options;
  std::map<std::string, Key, std::less<>> keys;
};

Result<JwtVerifier> JwtVerifier::create(JwtOptions options, std::string_view jwks) {
  if (!valid_options(options)) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid JWT trust configuration");
  }
  auto document = parse_json(jwks, 262144);
  if (!document || !document->contains("keys") || !(*document)["keys"].is_array() || (*document)["keys"].empty()
      || (*document)["keys"].size() > 32) {
    return denied();
  }
  auto state = std::make_shared<State>();
  state->options = std::move(options);
  std::set<std::string> kids;
  for (const auto& item : (*document)["keys"]) {
    auto kid = field(item, "kid");
    auto use = field(item, "use", false);
    auto kind = field(item, "kty");
    auto algorithm = field(item, "alg", false);
    if (!kid || !use || !kind || !algorithm || !kids.insert(*kid).second) {
      return denied();
    }
    constexpr std::array kPrivate = {"d", "p", "q", "dp", "dq", "qi", "oth", "k"};
    for (const auto* name : kPrivate) {
      if (item.contains(name)) {
        return denied();
      }
    }
    if (!use->empty() && *use != "sig") {
      continue;
    }
    if (algorithm->empty()) {
      *algorithm = "unsupported";
      if (*kind == "RSA") {
        *algorithm = "RS256";
      } else if (*kind == "EC") {
        *algorithm = "ES256";
      }
    }
    if (*algorithm != "RS256" && *algorithm != "ES256") {
      continue;
    }
    if ((*algorithm == "RS256" && *kind != "RSA") || (*algorithm == "ES256" && *kind != "EC")) {
      return denied();
    }
    if (item.contains("key_ops")) {
      auto operations = string_set(item, "key_ops");
      if (!operations || *operations != std::set<std::string>{"verify"}) {
        return denied();
      }
    }
    auto key = signing_key(item, *algorithm);
    if (!key) {
      return std::unexpected(key.error());
    }
    state->keys.emplace(*kid, std::move(*key));
  }
  if (state->keys.empty()) {
    return denied();
  }
  return JwtVerifier(std::move(state));
}

Result<bool> JwtVerifier::knows_key(std::string_view token) const {
  auto parsed = parse_token(token);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return state_->keys.contains(parsed->kid);
}

Result<Principal> JwtVerifier::verify(std::string_view token, std::chrono::system_clock::time_point now) const {
  auto parsed = parse_token(token);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  const auto key = state_->keys.find(parsed->kid);
  if (key == state_->keys.end() || key->second.algorithm != parsed->algorithm
      || !verify_signature(key->second, *parsed)) {
    return denied();
  }
  const auto& claims = parsed->claims;
  const auto& options = state_->options;
  auto issuer = field(claims, "iss");
  auto subject = field(claims, "sub");
  auto type = field(claims, "typ");
  auto tenant = field(claims, "psim_tenant", false);
  auto party = field(claims, "azp", false);
  if (!issuer || *issuer != options.issuer || !subject || !type || *type != "Bearer" || !tenant || !party
      || !claims.contains("aud")) {
    return denied();
  }
  const auto& audience = claims["aud"];
  if (audience.is_string()) {
    if (audience.get_ref<const std::string&>() != options.audience) {
      return denied();
    }
  } else {
    auto audiences = string_set(claims, "aud");
    if (!audiences || !audiences->contains(options.audience)) {
      return denied();
    }
  }
  auto expires = numeric_date(claims, "exp");
  auto issued = numeric_date(claims, "iat");
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  const auto skew = options.clock_skew.count();
  if (!expires || !issued || *expires <= *issued || *expires - *issued > options.max_lifetime.count()
      || *issued > seconds + skew) {
    return denied();
  }
  if (*expires <= seconds - skew) {
    return fail(ErrorCode::kAuthTokenExpired, "access token expired");
  }
  if (claims.contains("nbf")) {
    auto before = numeric_date(claims, "nbf");
    if (!before || *before > seconds + skew || *before >= *expires) {
      return denied();
    }
  }
  auto scopes = string_set(claims, "psim_scopes");
  std::set<std::string> roles;
  if (claims.contains("resource_access")) {
    const auto& resources = claims["resource_access"];
    if (!resources.is_object()) {
      return denied();
    }
    if (resources.contains(options.roles_client)) {
      auto role_values = string_set(resources[options.roles_client], "roles");
      if (!resources[options.roles_client].is_object() || !role_values) {
        return denied();
      }
      roles = std::move(*role_values);
    }
  }
  if (!scopes) {
    return denied();
  }
  return Principal{std::move(*subject), std::move(*tenant),
                   std::move(*party),   std::move(roles),
                   std::move(*scopes),  std::chrono::system_clock::time_point(std::chrono::seconds(*expires))};
}

JwksCache::JwksCache(JwksOptions options, Fetch fetch, Now now)
    : options_(std::move(options)), fetch_(std::move(fetch)), now_(std::move(now)) {}

Result<std::unique_ptr<JwksCache>> JwksCache::create(JwksOptions options, Fetch fetch, Now now) {
  auto valid = validate_url(options.url, options.http.allow_plaintext);
  if (!valid || !valid_options(options.jwt) || options.refresh_interval <= std::chrono::seconds::zero()
      || options.max_stale < options.refresh_interval || options.max_stale > std::chrono::hours(1)
      || options.unknown_key_cooldown <= std::chrono::seconds::zero()) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid JWKS cache configuration");
  }
  if (!fetch) {
    fetch = [url = options.url, http = options.http] {
      return fetch_keys(url, http);
    };
  }
  if (!now) {
    now = [] {
      return std::chrono::system_clock::now();
    };
  }
  return std::unique_ptr<JwksCache>(
      new JwksCache(std::move(options), std::move(fetch),
                    std::move(now)));  // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

boost::asio::awaitable<Result<void>> JwksCache::refresh() {
  if (refreshing_) {
    co_return fail(ErrorCode::kCommonUnavailable, "JWKS refresh already in progress");
  }
  refreshing_ = true;

  struct Reset {
    explicit Reset(bool* flag) : value(flag) {}

    Reset(const Reset&) = delete;
    Reset& operator=(const Reset&) = delete;
    Reset(Reset&&) = delete;
    Reset& operator=(Reset&&) = delete;
    bool* value;

    ~Reset() { *value = false; }
  };

  const Reset reset{&refreshing_};

  attempted_ = now_();
  attempted_once_ = true;
  auto body = co_await fetch_();
  if (!body) {
    co_return std::unexpected(body.error());
  }
  auto replacement = JwtVerifier::create(options_.jwt, *body);
  if (!replacement) {
    co_return std::unexpected(replacement.error());
  }
  verifier_ = std::make_unique<JwtVerifier>(std::move(*replacement));
  loaded_ = now_();
  co_return Result<void>{};
}

boost::asio::awaitable<Result<Principal>> JwksCache::verify(std::string token) {
  // Validate hostile input before permitting it to cause a network request.
  auto parsed = parse_token(token);
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  const auto now = now_();
  const bool known = verifier_ && verifier_->knows_key(token).value_or(false);
  if ((!verifier_ || !known || now - loaded_ >= options_.refresh_interval)
      && (!attempted_once_ || now - attempted_ >= options_.unknown_key_cooldown)) {
    (void)co_await refresh();
  }
  const auto current = now_();
  if (!verifier_ || current < loaded_ || current - loaded_ > options_.max_stale) {
    co_return fail(ErrorCode::kCommonUnavailable, "trusted signing keys unavailable or stale");
  }
  co_return verifier_->verify(token, current);
}

Result<void> authenticate_service(const Peer& peer, const Principal& token,
                                  const std::set<std::string>& allowed_services, bool delegated_user,
                                  std::chrono::system_clock::time_point now) {
  constexpr std::string_view kPrefix = "psim://service/";
  if (!peer.identity().starts_with(kPrefix) || peer.expires_at() <= now || token.expires_at <= now) {
    return fail(ErrorCode::kAuthUnauthenticated, "valid service certificate and token required");
  }
  const auto service = peer.identity().substr(kPrefix.size());
  if (!allowed_services.contains(service) || (!delegated_user && token.authorized_party != service)) {
    return fail(ErrorCode::kAuthForbidden, "service identity not authorized");
  }
  return {};
}
}  // namespace psim::platform::security
