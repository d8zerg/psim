#include "psim/platform/security/secret.hpp"

#include <fcntl.h>
#include <openssl/crypto.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::security {
Secret::Secret(std::string_view value) : bytes_(value.begin(), value.end()) {}

Secret::Secret(Secret&& other) noexcept : bytes_(std::move(other.bytes_)) {}

Secret& Secret::operator=(Secret&& other) noexcept {
  if (this != &other) {
    wipe();
    bytes_ = std::move(other.bytes_);
  }
  return *this;
}

Secret::~Secret() {
  wipe();
}

void Secret::wipe() noexcept {
  if (!bytes_.empty()) {
    OPENSSL_cleanse(bytes_.data(), bytes_.size());
    bytes_.clear();
  }
}

std::string_view Secret::view() const noexcept {
  return {bytes_.data(), bytes_.size()};
}

namespace {
struct Descriptor {
  explicit Descriptor(int value) : fd(value) {}

  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;
  int fd;

  ~Descriptor() { (void)::close(fd); }
};

struct Buffer {
  explicit Buffer(std::size_t size) : bytes(size) {}

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;
  std::vector<char> bytes;

  ~Buffer() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
};
}  // namespace

Result<Secret> read_secret(std::string_view path, std::size_t max_bytes) {
  if (path.empty() || path.find('\0') != std::string_view::npos || max_bytes == 0
      || max_bytes > std::size_t{1024} * 1024) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid secret file reference");
  }
  const std::string filename(path);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open without O_CREAT
  const auto fd = ::open(filename.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  const Descriptor file{fd};
  struct stat status{};
  if (file.fd < 0) {
    return fail(ErrorCode::kCommonInvalidArgument, "secret file not readable");
  }
  if (::fstat(file.fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != ::geteuid()
      || (status.st_mode & 07777U) != 0400U) {
    return fail(ErrorCode::kCommonInvalidArgument, "secret file must be regular, owned by the service user, mode 0400");
  }
  if (status.st_size < 0 || std::cmp_greater(status.st_size, max_bytes)) {
    return fail(ErrorCode::kCommonInvalidArgument, "secret file exceeds size limit");
  }
  Buffer buffer{max_bytes + 1};
  std::size_t size = 0;
  while (size < buffer.bytes.size()) {
    const auto count = ::read(file.fd, &buffer.bytes.at(size), buffer.bytes.size() - size);
    if (count == -1 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      return fail(ErrorCode::kCommonInvalidArgument, "secret file read failed");
    }
    if (count == 0) {
      break;
    }
    size += static_cast<std::size_t>(count);
  }
  if (size > max_bytes) {
    return fail(ErrorCode::kCommonInvalidArgument, "secret file exceeds size limit");
  }
  return Secret(std::string_view(buffer.bytes.data(), size));
}
}  // namespace psim::platform::security
