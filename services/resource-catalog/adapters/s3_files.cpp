#include "s3_files.hpp"

#include <sys/stat.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <utility>

#include "file_store.hpp"
#include "model.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::resource_catalog {
namespace p = platform;

namespace {
p::Result<std::string> directory(const std::string& root) {
  auto path = root + "/" + p::stream::uuid7();
  if (::mkdir(path.c_str(), 0700) != 0) {
    return p::fail(p::ErrorCode::kCommonUnavailable, "cannot create private image transfer directory");
  }
  return path;
}
}  // namespace

boost::asio::awaitable<p::Result<Blob>> S3Files::put(std::string key, std::string bytes) {
  auto scratch = co_await blocking_->run([this] { return directory(directory_); });
  if (!scratch) {
    co_return std::unexpected(scratch.error());
  }
  const auto path = *scratch + "/upload";
  auto written = co_await blocking_->run([&bytes, &path]() -> p::Result<void> {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
      return p::fail(p::ErrorCode::kCommonUnavailable, "cannot stage image");
    }
    return {};
  });
  p::Result<p::s3::Object> result = written ? co_await client_->put(std::move(key), path)
                                            : p::Result<p::s3::Object>{std::unexpected(written.error())};
  (void)co_await blocking_->run([&scratch] { std::filesystem::remove_all(*scratch); });
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return Blob{.key = result->key, .size = result->size, .sha256 = result->sha256};
}

boost::asio::awaitable<p::Result<std::string>> S3Files::get(Blob expected) {
  auto scratch = co_await blocking_->run([this] { return directory(directory_); });
  if (!scratch) {
    co_return std::unexpected(scratch.error());
  }
  const auto path = *scratch + "/download";
  auto fetched = co_await client_->get({.key = expected.key, .size = expected.size, .sha256 = expected.sha256}, path);
  p::Result<std::string> result = p::fail(p::ErrorCode::kCommonUnavailable, "image download failed");
  if (fetched) {
    result = co_await blocking_->run([&path]() -> p::Result<std::string> {
      std::ifstream input(path, std::ios::binary);
      std::string body((std::istreambuf_iterator<char>(input)), {});
      if (input.bad() || body.size() > kMaxImageBytes) {
        return p::fail(p::ErrorCode::kCommonUnavailable, "cannot read verified image");
      }
      return body;
    });
  } else {
    result = std::unexpected(fetched.error());
  }
  (void)co_await blocking_->run([&scratch] { std::filesystem::remove_all(*scratch); });
  co_return result;
}

boost::asio::awaitable<p::Result<void>> S3Files::remove(std::string key) {
  co_return co_await client_->remove(std::move(key));
}
}  // namespace psim::resource_catalog
