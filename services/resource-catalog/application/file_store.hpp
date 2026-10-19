#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"

namespace psim::resource_catalog {
struct Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct Blob {
  std::string key;
  std::uint64_t size = 0;
  std::string sha256;
};

[[nodiscard]] platform::Result<Image> validate_image(std::string_view type, std::string_view bytes);

class FileStore {
 public:
  FileStore() = default;
  FileStore(const FileStore&) = delete;
  FileStore& operator=(const FileStore&) = delete;
  FileStore(FileStore&&) = delete;
  FileStore& operator=(FileStore&&) = delete;
  virtual ~FileStore() = default;
  virtual boost::asio::awaitable<platform::Result<Blob>> put(std::string key, std::string bytes) = 0;
  virtual boost::asio::awaitable<platform::Result<std::string>> get(Blob expected) = 0;
  virtual boost::asio::awaitable<platform::Result<void>> remove(std::string key) = 0;
};
}  // namespace psim::resource_catalog
