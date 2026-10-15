#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/client.hpp"
#include "support.hpp"

namespace psim::platform::s3::testing {
TEST(S3Component, RoundTripTwentyMiBMetadataImmutableKeysAndDeletion) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    auto client = take(co_await Client::create(&blocking, options()));
    const Files files;
    const auto source = files.write("input", std::size_t{20} * 1024 * 1024);
    const auto object = take(co_await client->put(files.key(), source));
    EXPECT_EQ(object.size, std::size_t{20} * 1024 * 1024);
    EXPECT_EQ(object.sha256.size(), 64);
    const auto head = take(co_await client->head(object.key));
    EXPECT_EQ(head.sha256, object.sha256);
    EXPECT_EQ(head.size, object.size);
    EXPECT_EQ(take(co_await client->put(object.key, source)).sha256, object.sha256);
    const auto different = files.write("different", 3);
    EXPECT_FALSE(co_await client->put(object.key, different));
    const auto downloaded = take(co_await client->get(object, files.path("output")));
    EXPECT_EQ(downloaded.sha256, object.sha256);
    EXPECT_EQ(std::filesystem::file_size(files.path("output")), object.size);
    EXPECT_FALSE(co_await client->get(object, files.path("output")));
    check(co_await client->remove(object.key));
    EXPECT_FALSE(co_await client->head(object.key));
    check(co_await client->remove(object.key));
    EXPECT_TRUE(files.clean());
  };
  run(scenario());
}

TEST(S3Component, WrongCredentialsReaderWritesAndBucketEscapeAreDenied) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    const Files files;
    const auto source = files.write("input", 128);
    auto writer = take(co_await Client::create(&blocking, options()));
    auto reader = take(co_await Client::create(&blocking, options("reader")));
    const auto object = take(co_await writer->put(files.key(), source));
    EXPECT_TRUE(co_await reader->head(object.key));
    auto denied = co_await reader->put(files.key() + "-reader", source);
    EXPECT_FALSE(denied);
    EXPECT_EQ(denied.error().code(), ErrorCode::kAuthForbidden);
    EXPECT_FALSE(co_await reader->remove(object.key));
    auto config = options();
    config.bucket = "forbidden-bucket";
    auto outside = take(co_await Client::create(&blocking, config));
    EXPECT_FALSE(co_await outside->put(files.key(), source));
    config = options();
    config.secret_key_file = "/repo/.cache/s3-tls/reader.secret";
    auto wrong = take(co_await Client::create(&blocking, config));
    const auto error = co_await wrong->head(object.key);
    EXPECT_FALSE(error);
    EXPECT_EQ(error.error().message().find("psim-dev"), std::string::npos);
    check(co_await writer->remove(object.key));
  };
  run(scenario());
}

TEST(S3Component, CorruptedBytesWithUnchangedDigestMetadataAreNeverPublished) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    const Files files;
    auto client = take(co_await Client::create(&blocking, options()));
    const auto expected = take(co_await client->put(files.key(), files.write("input", 128)));
    const auto config = options();
    check(co_await blocking.run([config, expected]() -> Result<void> {
      detail::Configuration native;
      native.endpointOverride = config.endpoint;
      native.region = config.region;
      native.useVirtualAddressing = false;
      native.caFile = config.tls.ca_file;
      native.tls = config.tls;
      native.retryStrategy = std::make_shared<Aws::Client::DefaultRetryStrategy>(0);
      const Aws::S3::S3Client writer(Aws::Auth::AWSCredentials("psim-dev-admin", "psim-dev-admin-secret"), nullptr,
                                     native);
      Aws::S3::Model::PutObjectRequest request;
      request.SetBucket(config.bucket);
      request.SetKey(expected.key);
      request.SetContentLength(128);
      request.AddMetadata("sha256", expected.sha256);
      request.SetBody(std::make_shared<std::stringstream>(std::string(128, 'x')));
      if (!writer.PutObject(request).IsSuccess()) {
        return fail(ErrorCode::kCommonInternal, "cannot seed corrupted S3 object");
      }
      return {};
    }));
    EXPECT_EQ(take(co_await client->head(expected.key)).sha256, expected.sha256);
    const auto result = co_await client->get(expected, files.path("corrupted"));
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::kCommonInvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(files.path("corrupted")));
    EXPECT_TRUE(files.clean());
    check(co_await client->remove(expected.key));
  };
  run(scenario());
}

TEST(S3Component, WrongCaAndWrongHostnameFailClosed) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    auto config = options();
    config.tls.ca_file.clear();
    auto foreign = take(co_await Client::create(&blocking, config));
    EXPECT_FALSE(co_await foreign->head("missing"));
    config = options();
    config.endpoint = "https://s3-wrong:8443";
    auto hostname = take(co_await Client::create(&blocking, config));
    EXPECT_FALSE(co_await hostname->head("missing"));
  };
  run(scenario());
}

TEST(S3Component, OversizeAndDigestMismatchNeverPublishFiles) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    const Files files;
    const auto source = files.write("input", 128);
    auto writer = take(co_await Client::create(&blocking, options()));
    auto object = take(co_await writer->put(files.key(), source));
    auto config = options();
    config.max_object_bytes = 64;
    auto bounded = take(co_await Client::create(&blocking, config));
    EXPECT_FALSE(co_await bounded->put(files.key() + "-oversize", source));
    EXPECT_FALSE(co_await bounded->head(object.key));
    object.sha256 = std::string(64, 'a');
    EXPECT_FALSE(co_await writer->get(object, files.path("bad")));
    EXPECT_FALSE(std::filesystem::exists(files.path("bad")));
    EXPECT_TRUE(files.clean());
    check(co_await writer->remove(object.key));
  };
  run(scenario());
}
}  // namespace psim::platform::s3::testing
