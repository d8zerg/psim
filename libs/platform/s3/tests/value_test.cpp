#include <aws/core/utils/memory/AWSMemory.h>
#include <gtest/gtest.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>

#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/s3/metrics.hpp"
#include "support.hpp"

namespace psim::platform::s3::testing {
namespace {
std::shared_ptr<detail::Control> control() {
  auto result = std::make_shared<detail::Control>();
  result->deadline = std::chrono::steady_clock::now() + Duration(30000);
  return result;
}
}  // namespace

TEST(S3Values, KeysEndpointsAndLimitsAreValidatedBeforeIo) {
  for (const auto* key : {"", "../other", "a/../b", "a/./b", "/absolute", "trailing/", "a\\b"}) {
    EXPECT_FALSE(detail::valid_key(key));
  }
  EXPECT_FALSE(detail::valid_key(std::string("a\0b", 3)));
  EXPECT_TRUE(detail::valid_key("tenant/object with space"));
  EXPECT_TRUE(detail::digest(std::string(64, 'a')));
  EXPECT_FALSE(detail::digest(std::string(64, 'G')));
  Options config{
      .endpoint = "https://s3:8443", .bucket = "psim-plans", .access_key_file = "access", .secret_key_file = "secret"};
  EXPECT_TRUE(detail::validate(config));
  config.endpoint = "http://s3:8333";
  EXPECT_FALSE(detail::validate(config));
  config.allow_plaintext = true;
  EXPECT_TRUE(detail::validate(config));
  config.endpoint = "https://user:password@s3:8443";
  EXPECT_FALSE(detail::validate(config));
  config.endpoint = "https://s3:8443/path";
  EXPECT_FALSE(detail::validate(config));
  config.endpoint = "https://s3:8443";
  config.read_attempts = 100;
  EXPECT_FALSE(detail::validate(config));
}

TEST(S3Values, SnapshotIsSeekableImmutableAndUsesKnownSha256) {
  const Files files;
  const auto path = files.path("source");
  {
    std::ofstream output(path, std::ios::binary);
    output << "abc";
  }
  auto snapshot = take(detail::snapshot(path, 3, control()));
  EXPECT_EQ(snapshot.size, 3);
  EXPECT_EQ(snapshot.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  {
    std::ofstream output(path, std::ios::binary);
    output << "changed";
  }
  std::string read(3, '\0');
  snapshot.body->read(read.data(), 3);
  EXPECT_EQ(read, "abc");
  snapshot.body->clear();
  snapshot.body->seekg(0);
  EXPECT_EQ(snapshot.body->get(), 'a');
  snapshot.body->seekg(-1, std::ios::end);
  EXPECT_EQ(snapshot.body->get(), 'c');
  EXPECT_FALSE(detail::snapshot(path, 3, control()));
  EXPECT_FALSE(detail::snapshot(files.path("missing"), 3, control()));
}

TEST(S3Values, DownloadRequiresDigestAndNeverOverwritesDestination) {
  const Files files;
  const Object expected{
      .key = "a", .size = 3, .sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
  {
    detail::Download download(files.path("ok"), 3, control());
    Aws::UniquePtr<detail::FileStream> stream(download.stream());
    stream->write("abc", 3);
    check(download.commit(expected));
  }
  {
    detail::Download download(files.path("ok"), 3, control());
    Aws::UniquePtr<detail::FileStream> stream(download.stream());
    stream->write("abc", 3);
    EXPECT_FALSE(download.commit(expected));
  }
  {
    detail::Download download(files.path("bad"), 3, control());
    Aws::UniquePtr<detail::FileStream> stream(download.stream());
    stream->write("abd", 3);
    EXPECT_FALSE(download.commit(expected));
    EXPECT_FALSE(std::filesystem::exists(files.path("bad")));
  }
  EXPECT_TRUE(files.clean());
}

TEST(S3Values, SizeAndCancellationDiscardPartialDownloads) {
  const Files files;
  auto state = control();
  detail::Download download(files.path("cancelled"), 2, state);
  Aws::UniquePtr<detail::FileStream> stream(download.stream());
  stream->write("abc", 3);
  EXPECT_TRUE(stream->buffer().failed());
  state->cancelled.store(true);
  EXPECT_TRUE(state->stopped());
  EXPECT_FALSE(detail::snapshot(files.write("input", 1), 2, state));
  EXPECT_EQ(detail::stopped_error(*state).code(), ErrorCode::kCommonUnavailable);
  state->cancelled.store(false);
  state->deadline = std::chrono::steady_clock::now();
  EXPECT_EQ(detail::stopped_error(*state).code(), ErrorCode::kCommonDeadlineExceeded);
}

TEST(S3Values, MetricsHaveNoSecretOrObjectLabels) {
  prometheus::Registry registry;
  Metrics metrics(registry);
  metrics.completed(0.1, 3, true);
  metrics.completed(0.2, 0, false);
  metrics.retried();
  metrics.transferred(2);
  const auto text = prometheus::TextSerializer{}.Serialize(registry.Collect());
  EXPECT_NE(text.find("psim_s3_transferred_bytes_total 5"), std::string::npos);
  EXPECT_NE(text.find("psim_s3_errors_total 1"), std::string::npos);
}

TEST(S3Values, CreationRejectsInvalidSecretsAndClosedClientsStopBeforeIo) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(1, 2);
    const Files files;
    const Options config{.endpoint = "https://localhost:8443",
                         .bucket = "psim-plans",
                         .access_key_file = files.path("access"),
                         .secret_key_file = files.path("secret")};
    EXPECT_FALSE(co_await Client::create(nullptr, config));
    EXPECT_FALSE(co_await Client::create(&blocking, config));
    for (const auto& path : {config.access_key_file, config.secret_key_file}) {
      {
        const std::ofstream output(path);
      }
      std::filesystem::permissions(path, std::filesystem::perms::owner_read);
    }
    EXPECT_FALSE(co_await Client::create(&blocking, config));
    for (const auto& path : {config.access_key_file, config.secret_key_file}) {
      std::filesystem::permissions(path, std::filesystem::perms::owner_write);
      {
        std::ofstream output(path);
        output << "test-token";
      }
      std::filesystem::permissions(path, std::filesystem::perms::owner_read);
    }
    auto client = take(co_await Client::create(&blocking, config));
    client->close();
    const auto result = co_await client->head("object");
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::kCommonUnavailable);
    EXPECT_FALSE(co_await client->head("../invalid"));
    EXPECT_FALSE(co_await client->put("object", ""));
    EXPECT_FALSE(co_await client->get(Object{.key = "object", .sha256 = ""}, files.path("output")));
    EXPECT_FALSE(co_await client->remove("../invalid"));
  };
  run(scenario());
}
}  // namespace psim::platform::s3::testing
