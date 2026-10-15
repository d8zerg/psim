#pragma once
#include <aws/s3/S3Client.h>
#include <aws/s3/S3ClientConfiguration.h>
#include <openssl/evp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/s3/client.hpp"

namespace psim::platform::s3::detail {
constexpr std::size_t kChunk = std::size_t{64} * 1024;

struct Control {
  std::atomic_bool cancelled{false};
  std::chrono::steady_clock::time_point deadline;
  [[nodiscard]] bool stopped() const;
};

struct Configuration : Aws::S3::S3ClientConfiguration {
  Configuration()
      : Aws::S3::S3ClientConfiguration(Aws::Client::ClientConfigurationInitValues{.shouldDisableIMDS = true}) {}

  security::TlsOptions tls{};
};

[[nodiscard]] bool initialized();
[[nodiscard]] bool valid_key(std::string_view key);
[[nodiscard]] bool digest(std::string_view sha);
[[nodiscard]] Result<void> validate(const Options& options);
[[nodiscard]] std::string hex(const unsigned char* bytes, std::size_t size);
[[nodiscard]] Error stopped_error(const Control& control);

class File {
 public:
  explicit File(int fd = -1);
  ~File();
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;

  [[nodiscard]] int fd() const { return fd_; }

 private:
  int fd_;
};

class FileBuffer final : public std::streambuf {
 public:
  FileBuffer(File file, bool output, std::size_t limit, std::shared_ptr<Control> control);
  [[nodiscard]] std::string sha256();

  [[nodiscard]] std::size_t bytes() const { return bytes_; }

  [[nodiscard]] bool failed() const { return failed_; }

  [[nodiscard]] int fd() const { return file_.fd(); }

 private:
  int_type underflow() override;
  std::streamsize xsputn(const char* data, std::streamsize size) override;
  int_type overflow(int_type ch) override;
  pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode mode) override;
  pos_type seekpos(pos_type position, std::ios_base::openmode mode) override;
  File file_;
  bool output_;
  std::size_t limit_;
  std::size_t bytes_ = 0;
  bool failed_ = false;
  std::shared_ptr<Control> control_;
  std::array<char, kChunk> buffer_{};
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash_;
};

class FileStream final : public std::iostream {
 public:
  FileStream(File file, bool output, std::size_t limit, std::shared_ptr<Control> control);

  [[nodiscard]] FileBuffer& buffer() { return buffer_; }

 private:
  FileBuffer buffer_;
};

struct Snapshot {
  std::shared_ptr<FileStream> body;
  std::uint64_t size;
  std::string sha256;
  std::string checksum_base64;
};

[[nodiscard]] Result<Snapshot> snapshot(const std::string& path, std::size_t limit, std::shared_ptr<Control> control);

class Download {
 public:
  Download(std::string destination, std::size_t limit, std::shared_ptr<Control> control);
  ~Download();
  Download(const Download&) = delete;
  Download& operator=(const Download&) = delete;
  Download(Download&&) = delete;
  Download& operator=(Download&&) = delete;
  [[nodiscard]] FileStream* stream();
  [[nodiscard]] Result<void> commit(const Object& expected);

 private:
  std::string destination_;
  std::string temporary_;
  File file_;
  FileStream* stream_ = nullptr;
  std::size_t limit_;
  std::shared_ptr<Control> control_;
};
}  // namespace psim::platform::s3::detail
