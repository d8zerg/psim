#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error_codes.hpp"

namespace psim::platform {

/// Metadata of a catalog code (HTTP and gRPC status, retryability, English title).
[[nodiscard]] const ErrorCodeInfo& info(ErrorCode code) noexcept;

/// Catalog code by its name, for example "COMMON_NOT_FOUND".
[[nodiscard]] std::optional<ErrorCode> parse_error_code(std::string_view name) noexcept;

/// An error crossing a port or an API boundary: a catalog code and a message for operators.
/// The message never contains secrets or personal data (SR-09); `detail` adds context for logs.
class Error {
 public:
  Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

  [[nodiscard]] std::string_view name() const noexcept { return info(code_).name; }

  [[nodiscard]] const std::string& message() const noexcept { return message_; }

  [[nodiscard]] bool retryable() const noexcept { return info(code_).retryable; }

  /// Prefix the message with what was being done: "load config: file not found".
  [[nodiscard]] Error context(std::string_view what) && {
    message_ = std::string(what) + ": " + message_;
    return std::move(*this);
  }

  friend bool operator==(const Error&, const Error&) = default;

 private:
  ErrorCode code_;
  std::string message_;
};

template <typename T>
using Result = std::expected<T, Error>;

/// Failed Result in one expression: `return fail(ErrorCode::kCommonNotFound, "device 42")`.
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
  return std::unexpected<Error>(Error(code, std::move(message)));
}

}  // namespace psim::platform
