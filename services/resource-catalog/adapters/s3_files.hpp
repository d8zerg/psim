#pragma once

#include <memory>
#include <string>

#include "file_store.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/s3/client.hpp"

namespace psim::resource_catalog {
class S3Files final : public FileStore {
 public:
  S3Files(platform::s3::Client* client, platform::async::BlockingPool* blocking, std::string directory)
      : client_(client), blocking_(blocking), directory_(std::move(directory)) {}

  boost::asio::awaitable<platform::Result<Blob>> put(std::string key, std::string bytes) override;
  boost::asio::awaitable<platform::Result<std::string>> get(Blob expected) override;
  boost::asio::awaitable<platform::Result<void>> remove(std::string key) override;

 private:
  platform::s3::Client* client_;
  platform::async::BlockingPool* blocking_;
  std::string directory_;
};
}  // namespace psim::resource_catalog
