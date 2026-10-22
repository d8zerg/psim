#pragma once
#include <string>

#include "psim/platform/security/jwt.hpp"

namespace psim::platform::identity {
struct DirectoryOptions {
  std::string base{};
  std::string client{};
  std::string secret{};
  std::string roles_client{};
  security::HttpOptions http{};
};

/// Keycloak read-only directory. Never uses browser presence as account eligibility.
class Directory {
 public:
  explicit Directory(DirectoryOptions options) : options_(std::move(options)) {}

  boost::asio::awaitable<Result<security::Principal>> lookup(std::string subject) const;

 private:
  DirectoryOptions options_;
};
}  // namespace psim::platform::identity
