#pragma once
#include "psim/platform/security/http.hpp"
#include "repository.hpp"

namespace psim::audit_service {
struct IdentityOptions {
  std::string base;
  std::string client;
  std::string secret{};
  std::string security_tenant;
  p::security::HttpOptions http;
};

/// Poll the configured realm's durable security journal; never import credentials or representations.
class IdentitySource {
 public:
  IdentitySource(p::pg::Pool* pool, Encode encode, IdentityOptions options)
      : pool_(pool), encode_(std::move(encode)), options_(std::move(options)) {}

  asio::awaitable<p::Result<void>> poll();

 private:
  p::pg::Pool* pool_;
  Encode encode_;
  IdentityOptions options_;
};
}  // namespace psim::audit_service
