#include <aws/core/utils/memory/AWSMemory.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): declares POSIX mkstemp in the global namespace
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/s3/client.hpp"

namespace psim::platform::s3::detail {
namespace {
bool write_all(int fd, const char* data, std::size_t size) {
  while (size != 0) {
    const auto written = ::write(fd, data, size);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return false;
    }
    const auto count = static_cast<std::size_t>(written);
    data += count;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): POSIX byte buffer
    size -= count;
  }
  return true;
}

std::pair<File, std::string> temporary(const std::string& pattern) {
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');
  File file(::mkstemp(name.data()));
  if (file.fd() < 0) {
    throw std::runtime_error("cannot create bounded transfer file");
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX descriptor flags
  if (::fcntl(file.fd(), F_SETFD, FD_CLOEXEC) < 0) {
    (void)::unlink(name.data());
    throw std::runtime_error("cannot protect transfer descriptor");
  }
  return {std::move(file), std::string(name.data())};
}
}  // namespace

bool Control::stopped() const {
  return cancelled.load(std::memory_order_relaxed) || std::chrono::steady_clock::now() >= deadline;
}

Error stopped_error(const Control& control) {
  return {control.cancelled.load(std::memory_order_relaxed) ? ErrorCode::kCommonUnavailable
                                                            : ErrorCode::kCommonDeadlineExceeded,
          "S3 transfer cancelled or deadline exceeded; write outcome may be unknown"};
}

std::string hex(const unsigned char* bytes, std::size_t size) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    result += kDigits[bytes[i] >> 4];   // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): fixed digest bytes
    result += kDigits[bytes[i] & 15U];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): fixed digest bytes
  }
  return result;
}

File::File(int fd) : fd_(fd) {}

File::~File() {
  if (fd_ >= 0) {
    (void)::close(fd_);
  }
}

File::File(File&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      (void)::close(fd_);
    }
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

FileBuffer::FileBuffer(File file, bool output, std::size_t limit, std::shared_ptr<Control> control)
    : file_(std::move(file)),
      output_(output),
      limit_(limit),
      control_(std::move(control)),
      hash_(EVP_MD_CTX_new(), EVP_MD_CTX_free) {
  if (!hash_ || EVP_DigestInit_ex(hash_.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("cannot initialize SHA-256");
  }
}

FileBuffer::int_type FileBuffer::underflow() {
  if (output_ || control_->stopped()) {
    return traits_type::eof();
  }
  const auto count = ::read(file_.fd(), buffer_.data(), buffer_.size());
  if (count <= 0) {
    failed_ = count < 0;
    return traits_type::eof();
  }
  setg(buffer_.data(), buffer_.data(),
       buffer_.data() + count);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): streambuf byte range
  return traits_type::to_int_type(*gptr());
}

std::streamsize FileBuffer::xsputn(const char* data, std::streamsize size) {
  if (size < 0 || !output_ || control_->stopped() || static_cast<std::size_t>(size) > limit_ - bytes_) {
    failed_ = true;
    return 0;
  }
  if (!write_all(file_.fd(), data, static_cast<std::size_t>(size))
      || EVP_DigestUpdate(hash_.get(), data, static_cast<std::size_t>(size)) != 1) {
    failed_ = true;
    return 0;
  }
  bytes_ += static_cast<std::size_t>(size);
  return size;
}

FileBuffer::int_type FileBuffer::overflow(int_type ch) {
  if (traits_type::eq_int_type(ch, traits_type::eof())) {
    return traits_type::not_eof(ch);
  }
  const auto value = traits_type::to_char_type(ch);
  return xsputn(&value, 1) == 1 ? ch : traits_type::eof();
}

FileBuffer::pos_type FileBuffer::seekoff(off_type offset, std::ios_base::seekdir direction,
                                         std::ios_base::openmode mode) {
  if (output_ || (mode & std::ios_base::in) == 0) {
    return {off_type(-1)};
  }
  if (direction == std::ios_base::cur && gptr() != nullptr) {
    offset -= egptr() - gptr();
  }
  int origin = SEEK_CUR;
  if (direction == std::ios_base::beg) {
    origin = SEEK_SET;
  }
  if (direction == std::ios_base::end) {
    origin = SEEK_END;
  }
  const auto result = ::lseek(file_.fd(), offset, origin);
  setg(nullptr, nullptr, nullptr);
  return {result};
}

FileBuffer::pos_type FileBuffer::seekpos(pos_type position, std::ios_base::openmode mode) {
  return seekoff(off_type(position), std::ios_base::beg, mode);
}

std::string FileBuffer::sha256() {
  std::array<unsigned char, EVP_MAX_MD_SIZE> value{};
  unsigned int size = 0;
  if (EVP_DigestFinal_ex(hash_.get(), value.data(), &size) != 1) {
    throw std::runtime_error("SHA-256 finalization failed");
  }
  return hex(value.data(), size);
}

FileStream::FileStream(File file, bool output, std::size_t limit, std::shared_ptr<Control> control)
    : std::iostream(nullptr), buffer_(std::move(file), output, limit, std::move(control)) {
  rdbuf(&buffer_);
}

Result<Snapshot> snapshot(const std::string& path, std::size_t limit, std::shared_ptr<Control> control) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open without O_CREAT
  const File source(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  struct stat status{};
  if (source.fd() < 0 || ::fstat(source.fd(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0) {
    return fail(ErrorCode::kCommonInvalidArgument, "S3 source must be a readable regular file");
  }
  if (std::cmp_greater(status.st_size, limit)) {
    return fail(ErrorCode::kCommonInvalidArgument, "S3 object exceeds configured limit");
  }
  auto [file, name] = temporary("/tmp/psim-s3-XXXXXX");
  (void)::unlink(name.c_str());
  FileBuffer output(std::move(file), true, limit, control);
  std::array<char, kChunk> chunk{};
  for (;;) {
    if (control->stopped()) {
      return std::unexpected(stopped_error(*control));
    }
    const auto count = ::read(source.fd(), chunk.data(), chunk.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      return fail(ErrorCode::kCommonInternal, "cannot read S3 source file");
    }
    if (count == 0) {
      break;
    }
    if (output.sputn(chunk.data(), count) != count) {
      return fail(ErrorCode::kCommonInvalidArgument, "S3 source exceeds limit or snapshot write failed");
    }
  }
  const auto size = output.bytes();
  const auto sha = output.sha256();
  // Duplicate only the descriptor; both descriptors refer to the same bounded snapshot.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX duplicate with close-on-exec
  File input(::fcntl(output.fd(), F_DUPFD_CLOEXEC, 0));
  if (input.fd() < 0 || ::lseek(input.fd(), 0, SEEK_SET) < 0) {
    return fail(ErrorCode::kCommonInternal, "cannot rewind S3 snapshot");
  }
  std::array<unsigned char, 32> digest_bytes{};
  for (std::size_t i = 0; i < digest_bytes.size(); ++i) {
    const auto digit = [](char ch) {
      return ch <= '9' ? ch - '0' : ch - 'a' + 10;
    };
    digest_bytes[i] = static_cast<unsigned char>((digit(sha[i * 2]) * 16) + digit(sha[(i * 2) + 1]));
  }
  std::array<unsigned char, 48> encoded{};
  const auto encoded_size = EVP_EncodeBlock(encoded.data(), digest_bytes.data(), static_cast<int>(digest_bytes.size()));
  return Snapshot{std::make_shared<FileStream>(std::move(input), false, limit, control), size, sha,
                  std::string(encoded.begin(), std::next(encoded.begin(), encoded_size))};
}

Download::Download(std::string destination, std::size_t limit, std::shared_ptr<Control> control)
    : destination_(std::move(destination)), limit_(limit), control_(std::move(control)) {
  auto [file, name] = temporary(destination_ + ".psim-XXXXXX");
  file_ = std::move(file);
  temporary_ = std::move(name);
}

Download::~Download() {
  if (!temporary_.empty()) {
    (void)::unlink(temporary_.c_str());
  }
}

FileStream* Download::stream() {
  if (stream_ != nullptr) {
    throw std::runtime_error("SDK attempted a hidden response retry");
  }
  stream_ = Aws::New<FileStream>("psim-s3", std::move(file_), true, limit_, control_);
  return stream_;
}

Result<void> Download::commit(const Object& expected) {
  if (stream_ == nullptr || stream_->buffer().failed() || stream_->buffer().bytes() != expected.size
      || stream_->buffer().sha256() != expected.sha256) {
    return fail(ErrorCode::kCommonInvalidArgument, "S3 download length or SHA-256 mismatch");
  }
  if (control_->stopped()) {
    return std::unexpected(stopped_error(*control_));
  }
  if (::fsync(stream_->buffer().fd()) < 0 || ::link(temporary_.c_str(), destination_.c_str()) < 0) {
    return fail(ErrorCode::kCommonInternal, "cannot atomically publish S3 download; destination must not exist");
  }
  return {};
}
}  // namespace psim::platform::s3::detail
