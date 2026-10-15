#include <aws/core/Aws.h>
#include <aws/core/http/HttpClientFactory.h>
#include <aws/core/http/HttpRequest.h>
#include <aws/core/http/HttpTypes.h>
#include <aws/core/http/URI.h>
#include <aws/core/http/curl/CurlHttpClient.h>
#include <aws/core/http/standard/StandardHttpRequest.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <curl/curl.h>
#include <openssl/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "detail.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::s3 {
namespace {
class Http final : public Aws::Http::CurlHttpClient {
 public:
  explicit Http(const Aws::Client::ClientConfiguration& config) : Aws::Http::CurlHttpClient(config) {
    if (const auto* typed = dynamic_cast<const detail::Configuration*>(&config)) {
      tls_ = typed->tls;
    }
  }

 private:
  static CURLcode context(CURL* /*handle*/, void* native, void* user) {
    const auto* self = static_cast<const Http*>(user);
    return security::configure_tls(static_cast<SSL_CTX*>(native), self->tls_) ? CURLE_OK : CURLE_SSL_CERTPROBLEM;
  }

  void OverrideOptionsOnConnectionHandle(
      CURL* handle) const override {  // NOLINT(readability-identifier-naming): SDK virtual API
    (void)curl_easy_setopt(handle, CURLOPT_PROXY,
                           "");  // NOLINT(cppcoreguidelines-pro-type-vararg): libcurl typed option API
    (void)curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION,
                           0L);  // NOLINT(cppcoreguidelines-pro-type-vararg): libcurl typed option API
    (void)curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER,
                           1L);  // NOLINT(cppcoreguidelines-pro-type-vararg): libcurl typed option API
    (void)curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST,
                           2L);  // NOLINT(cppcoreguidelines-pro-type-vararg): libcurl typed option API
    (void)curl_easy_setopt(handle, CURLOPT_SSL_CTX_FUNCTION,
                           &Http::context);  // NOLINT(cppcoreguidelines-pro-type-vararg): libcurl typed option API
    (void)curl_easy_setopt(handle, CURLOPT_SSL_CTX_DATA,
                           this);  // NOLINT(cppcoreguidelines-pro-type-vararg): libcurl typed option API
  }

  security::TlsOptions tls_{};
};

class Factory final : public Aws::Http::HttpClientFactory {
 public:
  std::shared_ptr<Aws::Http::HttpClient> CreateHttpClient(const Aws::Client::ClientConfiguration& config)
      const override {  // NOLINT(readability-identifier-naming): SDK virtual API
    return std::make_shared<Http>(config);
  }

  std::shared_ptr<Aws::Http::HttpRequest> CreateHttpRequest(
      const Aws::String& uri, Aws::Http::HttpMethod method,
      const Aws::IOStreamFactory& stream) const override {  // NOLINT(readability-identifier-naming): SDK virtual API
    return CreateHttpRequest(Aws::Http::URI(uri), method, stream);
  }

  std::shared_ptr<Aws::Http::HttpRequest> CreateHttpRequest(
      const Aws::Http::URI& uri, Aws::Http::HttpMethod method,
      const Aws::IOStreamFactory& stream) const override {  // NOLINT(readability-identifier-naming): SDK virtual API
    auto request = std::make_shared<Aws::Http::Standard::StandardHttpRequest>(uri, method);
    request->SetResponseStreamFactory(stream);
    return request;
  }

  void InitStaticState() override {
    Aws::Http::CurlHttpClient::InitGlobalState();
  }  // NOLINT(readability-identifier-naming): SDK virtual API

  void CleanupStaticState() override {
    Aws::Http::CurlHttpClient::CleanupGlobalState();
  }  // NOLINT(readability-identifier-naming): SDK virtual API
};

struct Runtime {
  Runtime() {
    options.httpOptions.httpClientFactory_create_fn = [] {
      return std::make_shared<Factory>();
    };
    options.httpOptions.installSigPipeHandler = false;
    Aws::InitAPI(options);
  }

  ~Runtime() { Aws::ShutdownAPI(options); }

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(Runtime&&) = delete;
  Aws::SDKOptions options{};
};

std::mutex& runtime_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unique_ptr<Runtime>& runtime() {
  static std::unique_ptr<Runtime> instance;
  return instance;
}

std::size_t& references() {
  static std::size_t count = 0;
  return count;
}
}  // namespace

Sdk::Sdk() {
  const std::scoped_lock lock(runtime_mutex());
  if (references() == 0) {
    runtime() = std::make_unique<Runtime>();
  }
  ++references();
}

Sdk::~Sdk() {
  const std::scoped_lock lock(runtime_mutex());
  if (--references() == 0) {
    runtime().reset();
  }
}

namespace detail {
bool initialized() {
  const std::scoped_lock lock(runtime_mutex());
  return references() != 0;
}

bool valid_key(std::string_view key) {
  if (key.empty() || key.size() > 1024 || key.front() == '/' || key.back() == '/' || key == "." || key == ".."
      || key.starts_with("../") || key.starts_with("./") || key.ends_with("/..") || key.ends_with("/.")
      || key.find("/../") != std::string_view::npos || key.find("/./") != std::string_view::npos
      || key.find('\\') != std::string_view::npos) {
    return false;
  }
  return std::ranges::none_of(key, [](unsigned char ch) { return ch < 32 || ch == 127; });
}

bool digest(std::string_view sha) {
  return sha.size() == 64
         && std::ranges::all_of(sha, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

Result<void> validate(const Options& options) {
  if (!security::validate_url(options.endpoint, options.allow_plaintext) || options.bucket.size() < 3
      || options.bucket.size() > 63 || options.bucket.front() == '-' || options.bucket.back() == '-'
      || !std::ranges::all_of(options.bucket,
                              [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'; })
      || options.region.empty() || options.access_key_file.empty() || options.secret_key_file.empty()
      || options.endpoint.back() == '/' || options.concurrency == 0 || options.concurrency > 128
      || options.max_object_bytes == 0 || options.max_object_bytes > std::size_t{20} * 1024 * 1024
      || options.timeout.count() <= 0 || options.timeout.count() > 3600000 || options.acquire_timeout.count() <= 0
      || options.connect_timeout.count() <= 0 || options.connect_timeout.count() > 3600000 || options.read_attempts == 0
      || options.read_attempts > 5) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid S3 endpoint, credentials or transfer limits");
  }
  const Aws::Http::URI uri(options.endpoint.c_str());
  if ((uri.GetPath() != "/" && !uri.GetPath().empty()) || !uri.GetQueryString().empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "S3 endpoint must not contain a path or query");
  }
  return {};
}
}  // namespace detail
}  // namespace psim::platform::s3
