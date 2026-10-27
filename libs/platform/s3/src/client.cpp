#include "psim/platform/s3/client.hpp"

#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/core/http/HttpRequest.h>
#include <aws/core/http/Scheme.h>
#include <aws/core/utils/memory/AWSMemory.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/S3Errors.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/HeadObjectResult.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/this_coro.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <ios>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include "detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/credits.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/metrics.hpp"
#include "psim/platform/security/secret.hpp"

namespace psim::platform::s3 {
namespace {
Error error(const Aws::S3::S3Error& failure, const detail::Control& control) {
  if (control.stopped()) {
    return detail::stopped_error(control);
  }
  const auto status = static_cast<int>(failure.GetResponseCode());
  if (status == 404) {
    return {ErrorCode::kCommonNotFound, "S3 object not found"};
  }
  if (status == 401 || status == 403) {
    return {ErrorCode::kAuthForbidden, "S3 access denied"};
  }
  if (status == 409 || status == 412) {
    return {ErrorCode::kCommonConcurrentModification, "S3 object precondition failed"};
  }
  if (status >= 400 && status < 500 && status != 408 && status != 429) {
    return {ErrorCode::kCommonInvalidArgument, "S3 request rejected"};
  }
  return {ErrorCode::kCommonUnavailable, "S3 transport or service failure; write outcome may be unknown"};
}

class ErrorStream final : public std::iostream {
 public:
  ErrorStream() : std::iostream(nullptr) { rdbuf(&buffer_); }

 private:
  class Buffer final : public std::streambuf {
   private:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
      if (size < 0 || static_cast<std::size_t>(size) > 16384 - bytes_.size()) {
        return 0;
      }
      bytes_.append(data, static_cast<std::size_t>(size));
      setg(bytes_.data(), bytes_.data(),
           bytes_.data()
               + bytes_.size());  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded stream range
      return size;
    }

    int_type overflow(int_type ch) override {
      if (traits_type::eq_int_type(ch, traits_type::eof())) {
        return traits_type::not_eof(ch);
      }
      const char value = traits_type::to_char_type(ch);
      return xsputn(&value, 1) == 1 ? ch : traits_type::eof();
    }

    pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode mode) override {
      if ((mode & std::ios_base::in) == 0) {
        return {off_type(-1)};
      }
      off_type base = 0;
      if (direction == std::ios_base::end) {
        base = static_cast<off_type>(bytes_.size());
      } else if (direction == std::ios_base::cur && gptr() != nullptr) {
        base = gptr() - eback();
      }
      const auto position = base + offset;
      if (position < 0 || static_cast<std::size_t>(position) > bytes_.size()) {
        return {off_type(-1)};
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded stream range
      setg(bytes_.data(), bytes_.data() + position,
           bytes_.data()
               + bytes_.size());  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded stream range
      return {position};
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
      return seekoff(off_type(position), std::ios_base::beg, mode);
    }

    std::string bytes_{};
  } buffer_;
};

template <typename Request>
void configure(Request& request, const Options& options, const std::string& key,
               const std::shared_ptr<detail::Control>& control) {
  request.SetResponseStreamFactory([] { return Aws::New<ErrorStream>("psim-s3"); });
  request.SetBucket(options.bucket.c_str());
  request.SetKey(key.c_str());
  request.SetContinueRequestHandler([control](const Aws::Http::HttpRequest*) { return !control->stopped(); });
}

Result<Object> metadata(const Aws::S3::Model::HeadObjectResult& result, const std::string& key,
                        const Options& options) {
  const auto found = result.GetMetadata().find("sha256");
  if (result.GetContentLength() < 0 || std::cmp_greater(result.GetContentLength(), options.max_object_bytes)
      || found == result.GetMetadata().end() || !detail::digest(found->second) || result.GetETag().empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "S3 metadata has invalid size, SHA-256 or conditional token");
  }
  return Object{key, static_cast<std::uint64_t>(result.GetContentLength()), found->second, result.GetETag()};
}

Result<Object> head_native(Aws::S3::S3Client& client, const Options& options, const std::string& key,
                           const std::shared_ptr<detail::Control>& control) {
  Aws::S3::Model::HeadObjectRequest request;
  configure(request, options, key, control);
  const auto outcome = client.HeadObject(request);
  if (!outcome.IsSuccess()) {
    return std::unexpected(error(outcome.GetError(), *control));
  }
  return metadata(outcome.GetResult(), key, options);
}
}  // namespace

struct Client::State {
  State(async::BlockingPool* pool, Options config, Metrics* observers)
      : blocking(pool), options(std::move(config)), metrics(observers), capacity(options.concurrency) {}

  async::BlockingPool* blocking;
  Options options;
  Metrics* metrics;
  async::Semaphore capacity;
  std::shared_ptr<Aws::S3::S3Client> client{};
  bool closed = false;

  template <typename T, typename Function>
  asio::awaitable<Result<T>> run(Function function) {
    if (closed) {
      co_return fail(ErrorCode::kCommonUnavailable, "S3 client is closed");
    }
    const auto started = std::chrono::steady_clock::now();
    auto control = std::make_shared<detail::Control>();
    control->deadline = started + options.timeout;
    async::SystemClock clock;
    auto permit =
        co_await async::with_deadline(&clock, std::min(options.acquire_timeout, options.timeout), capacity.acquire());
    if (!permit) {
      co_return std::unexpected(permit.error());
    }
    if (!*permit || closed) {
      co_return fail(ErrorCode::kCommonUnavailable, "S3 client closed or admission cancelled");
    }
    const auto incoming = co_await asio::this_coro::cancellation_state;
    if (incoming.cancelled() != asio::cancellation_type::none) {
      co_return fail(ErrorCode::kCommonUnavailable, "S3 operation cancelled before native I/O");
    }
    co_await asio::this_coro::reset_cancellation_state([control](asio::cancellation_type type) {
      if (type != asio::cancellation_type::none) {
        control->cancelled.store(true, std::memory_order_relaxed);
      }
      return asio::cancellation_type::none;
    });
    auto result = co_await blocking->run(
        [native = client, config = options, control, fn = std::move(function)]() mutable -> Result<T> {
          try {
            if (control->stopped()) {
              return std::unexpected(detail::stopped_error(*control));
            }
            return fn(*native, config, control);
          } catch (const std::exception&) {
            return fail(ErrorCode::kCommonInternal, "S3 stream or SDK operation failed");
          }
        });
    co_await asio::this_coro::reset_cancellation_state();
    if (metrics != nullptr) {
      metrics->completed(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(), 0,
                         result.has_value());
    }
    co_return result;
  }
};

Client::Client(std::shared_ptr<State> state) : state_(std::move(state)) {}

Client::~Client() {
  close();
}

void Client::close() {
  state_->closed = true;
  state_->capacity.close();
}

asio::awaitable<Result<std::unique_ptr<Client>>> Client::create(async::BlockingPool* blocking, Options options,
                                                                Metrics* metrics) {
  if (!detail::initialized()) {
    co_return fail(ErrorCode::kCommonPreconditionRequired,
                   "S3 SDK lifetime must be initialized before creating clients");
  }
  const auto valid = detail::validate(options);
  if (!valid) {
    co_return std::unexpected(valid.error());
  }
  if (blocking == nullptr) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "S3 requires a blocking pool");
  }
  auto state = std::make_shared<State>(blocking, std::move(options), metrics);
  auto created = co_await blocking->run([state]() -> Result<void> {
    auto access = security::read_secret(state->options.access_key_file);
    auto secret = security::read_secret(state->options.secret_key_file);
    if (!access || !secret) {
      return fail(ErrorCode::kCommonInvalidArgument, "S3 credential files cannot be read securely");
    }
    const auto valid_credential = [](std::string_view value) {
      return !value.empty() && std::ranges::all_of(value, [](unsigned char ch) { return ch > 32 && ch < 127; });
    };
    if (!valid_credential(access->view()) || !valid_credential(secret->view())) {
      return fail(ErrorCode::kCommonInvalidArgument, "S3 credentials must contain nonempty printable tokens");
    }
    detail::Configuration config;
    config.endpointOverride = state->options.endpoint;
    config.region = state->options.region;
    config.scheme =
        state->options.endpoint.starts_with("https://") ? Aws::Http::Scheme::HTTPS : Aws::Http::Scheme::HTTP;
    config.useVirtualAddressing = false;
    config.verifySSL = true;
    config.tls = state->options.tls;
    config.caFile = state->options.tls.ca_file;
    config.connectTimeoutMs = static_cast<long>(state->options.connect_timeout.count());
    config.httpRequestTimeoutMs = static_cast<long>(state->options.timeout.count());
    config.requestTimeoutMs = static_cast<long>(state->options.timeout.count());
    config.maxConnections = static_cast<unsigned>(state->options.concurrency);
    config.retryStrategy = std::make_shared<Aws::Client::DefaultRetryStrategy>(0);
    config.followRedirects = Aws::Client::FollowRedirectsPolicy::NEVER;
    config.checksumConfig.requestChecksumCalculation = Aws::Client::RequestChecksumCalculation::WHEN_REQUIRED;
    config.checksumConfig.responseChecksumValidation = Aws::Client::ResponseChecksumValidation::WHEN_REQUIRED;
    const Aws::Auth::AWSCredentials credentials(std::string(access->view()), std::string(secret->view()));
    state->client = std::make_shared<Aws::S3::S3Client>(credentials, nullptr, config);
    return {};
  });
  if (!created) {
    co_return std::unexpected(created.error());
  }
  co_return std::unique_ptr<Client>(
      new Client(std::move(state)));  // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

asio::awaitable<Result<Object>> Client::head(std::string key) {
  auto state = state_;
  if (!detail::valid_key(key)) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid S3 key");
  }
  co_return co_await state->run<Object>(
      [key = std::move(key), metrics = state->metrics](Aws::S3::S3Client& client, const Options& config,
                                                       const auto& control) -> Result<Object> {
        Result<Object> result = fail(ErrorCode::kCommonUnavailable, "S3 read did not run");
        for (unsigned int attempt = 0; attempt < config.read_attempts; ++attempt) {
          if (attempt != 0 && metrics != nullptr) {
            metrics->retried();
          }
          result = head_native(client, config, key, control);
          if (result || result.error().code() != ErrorCode::kCommonUnavailable || control->stopped()) {
            break;
          }
        }
        return result;
      });
}

asio::awaitable<Result<ListPage>> Client::list(std::string continuation, std::size_t max_keys) {
  if (continuation.size() > 4096 || max_keys == 0 || max_keys > 1000) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid S3 continuation token");
  }
  co_return co_await state_->run<ListPage>(
      [token = std::move(continuation), max_keys](Aws::S3::S3Client& client, const Options& config,
                                                  const auto& control) -> Result<ListPage> {
        Aws::S3::Model::ListObjectsV2Request request;
        request.SetBucket(config.bucket);
        request.SetMaxKeys(static_cast<int>(max_keys));
        if (!token.empty()) {
          request.SetContinuationToken(token);
        }
        request.SetContinueRequestHandler([control](const Aws::Http::HttpRequest*) { return !control->stopped(); });
        const auto outcome = client.ListObjectsV2(request);
        if (!outcome.IsSuccess()) {
          return std::unexpected(error(outcome.GetError(), *control));
        }
        ListPage page;
        for (const auto& object : outcome.GetResult().GetContents()) {
          if (!detail::valid_key(object.GetKey())) {
            return fail(ErrorCode::kCommonInvalidArgument, "bucket contains an invalid object key");
          }
          page.keys.push_back(object.GetKey());
        }
        if (outcome.GetResult().GetIsTruncated()) {
          page.continuation = outcome.GetResult().GetNextContinuationToken();
          if (page.continuation.empty() || page.continuation == token) {
            return fail(ErrorCode::kCommonInvalidArgument, "S3 enumeration made no progress");
          }
        }
        return page;
      });
}

asio::awaitable<Result<Object>> Client::put(std::string key, std::string source_file, WriteMode mode) {
  auto state = state_;
  if (!detail::valid_key(key) || source_file.empty() || source_file.find('\0') != std::string::npos) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid S3 key or source path");
  }
  co_return co_await state->run<Object>(
      [key = std::move(key), path = std::move(source_file), mode, metrics = state->metrics](
          Aws::S3::S3Client& client, const Options& config, const auto& control) -> Result<Object> {
        auto source = detail::snapshot(path, config.max_object_bytes, control);
        if (!source) {
          return std::unexpected(source.error());
        }
        for (unsigned int attempt = 0; attempt < 2; ++attempt) {
          source->body->clear();
          source->body->seekg(0);
          Aws::S3::Model::PutObjectRequest request;
          configure(request, config, key, control);
          request.SetBody(source->body);
          request.SetContentLength(static_cast<long long>(source->size));
          request.SetIfNoneMatch("*");
          request.AddMetadata("sha256", source->sha256.c_str());
          request.SetChecksumSHA256(source->checksum_base64.c_str());
          request.SetContentType("application/octet-stream");
          const auto outcome = client.PutObject(request);
          if (outcome.IsSuccess()) {
            if (metrics != nullptr) {
              metrics->transferred(static_cast<std::size_t>(source->size));
            }
            return Object{key, source->size, source->sha256, outcome.GetResult().GetETag()};
          }
          const auto failure = error(outcome.GetError(), *control);
          if (mode == WriteMode::kWriteOnly) {
            return std::unexpected(failure);
          }
          if (failure.code() != ErrorCode::kCommonUnavailable
              && failure.code() != ErrorCode::kCommonConcurrentModification) {
            return std::unexpected(failure);
          }
          if (control->stopped()) {
            return std::unexpected(detail::stopped_error(*control));
          }
          auto found = head_native(client, config, key, control);
          if (found) {
            if (found->size == source->size && found->sha256 == source->sha256) {
              return found;
            }
            return fail(ErrorCode::kCommonConcurrentModification, "S3 immutable key already contains different bytes");
          }
          if (found.error().code() != ErrorCode::kCommonNotFound || attempt != 0) {
            return std::unexpected(failure);
          }
          if (metrics != nullptr) {
            metrics->retried();
          }
        }
        return fail(ErrorCode::kCommonUnavailable, "S3 upload outcome remains unknown");
      });
}

asio::awaitable<Result<Object>> Client::get(Object expected, std::string destination_file) {
  auto state = state_;
  if (!detail::valid_key(expected.key) || !detail::digest(expected.sha256)
      || expected.size > state->options.max_object_bytes || destination_file.empty()
      || destination_file.find('\0') != std::string::npos) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid S3 expected object or destination");
  }
  co_return co_await state->run<Object>(
      [expected = std::move(expected), destination = std::move(destination_file), metrics = state->metrics](
          Aws::S3::S3Client& client, const Options& config, const auto& control) -> Result<Object> {
        auto actual = head_native(client, config, expected.key, control);
        if (!actual) {
          return std::unexpected(actual.error());
        }
        if (actual->sha256 != expected.sha256 || actual->size != expected.size) {
          return fail(ErrorCode::kCommonConcurrentModification, "S3 object differs from expected SHA-256 or size");
        }
        for (unsigned int attempt = 0; attempt < config.read_attempts; ++attempt) {
          detail::Download download(destination, config.max_object_bytes, control);
          Aws::S3::Model::GetObjectRequest request;
          configure(request, config, expected.key, control);
          request.SetIfMatch(actual->etag.c_str());
          request.SetResponseStreamFactory([&download] { return download.stream(); });
          auto outcome = client.GetObject(request);
          if (outcome.IsSuccess()) {
            const auto committed = download.commit(expected);
            if (!committed) {
              return std::unexpected(committed.error());
            }
            if (metrics != nullptr) {
              metrics->transferred(static_cast<std::size_t>(expected.size));
            }
            return actual;
          }
          const auto failure = error(outcome.GetError(), *control);
          if (failure.code() != ErrorCode::kCommonUnavailable || control->stopped()
              || attempt + 1 == config.read_attempts) {
            return std::unexpected(failure);
          }
          if (metrics != nullptr) {
            metrics->retried();
          }
        }
        return fail(ErrorCode::kCommonUnavailable, "S3 download retries exhausted");
      });
}

asio::awaitable<Result<void>> Client::remove(std::string key) {
  auto state = state_;
  if (!detail::valid_key(key)) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid S3 key");
  }
  co_return co_await state->run<void>(
      [key = std::move(key)](Aws::S3::S3Client& client, const Options& config, const auto& control) -> Result<void> {
        Aws::S3::Model::DeleteObjectRequest request;
        configure(request, config, key, control);
        const auto outcome = client.DeleteObject(request);
        if (!outcome.IsSuccess()) {
          return std::unexpected(error(outcome.GetError(), *control));
        }
        return {};
      });
}
}  // namespace psim::platform::s3
