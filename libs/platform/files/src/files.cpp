#include "psim/platform/files.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::files {
namespace {
int open_file(const std::filesystem::path& path, int flags) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX file creation with a private mode.
  return ::open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
}

class Descriptor {
 public:
  Descriptor(const std::filesystem::path& path, int flags) : value_(open_file(path, flags)) {}

  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;

  ~Descriptor() {
    if (value_ >= 0) {
      (void)::close(value_);
    }
  }

  [[nodiscard]] int get() const { return value_; }

 private:
  int value_;
};

bool regular(int fd) {
  struct stat status{};
  return ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode);
}
}  // namespace

Result<std::string> read(const std::filesystem::path& path, std::size_t maximum) {
  const Descriptor file(path, O_RDONLY);
  if (file.get() < 0 || !regular(file.get())) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid input file");
  }
  std::string body;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(file.get(), buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      return fail(ErrorCode::kCommonUnavailable, "input read failed");
    }
    if (count == 0) {
      return body;
    }
    const auto size = static_cast<std::size_t>(count);
    if (body.size() > maximum || size > maximum - body.size()) {
      return fail(ErrorCode::kCommonInvalidArgument, "input exceeds size limit");
    }
    body.append(buffer.data(), size);
  }
}

Result<void> sync(const std::filesystem::path& path) {
  const Descriptor file(path, O_RDONLY);
  if (file.get() < 0 || ::fsync(file.get()) != 0) {
    return fail(ErrorCode::kCommonUnavailable, "output synchronization failed");
  }
  return {};
}

Result<void> append(const std::filesystem::path& path, std::string_view body) {
  const Descriptor file(path, O_WRONLY | O_APPEND | O_CREAT);
  if (file.get() < 0 || !regular(file.get()) || ::flock(file.get(), LOCK_EX) != 0) {
    return fail(ErrorCode::kCommonUnavailable, "cannot open journal");
  }
  std::size_t offset = 0;
  while (offset < body.size()) {
    const auto part = body.substr(offset);
    const auto count = ::write(file.get(), part.data(), part.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return fail(ErrorCode::kCommonUnavailable, "journal write failed");
    }
    offset += static_cast<std::size_t>(count);
  }
  if (::fsync(file.get()) != 0) {
    return fail(ErrorCode::kCommonUnavailable, "journal synchronization failed");
  }
  return sync(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
}
}  // namespace psim::platform::files
