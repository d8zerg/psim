#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

#include "psim/platform/error.hpp"

namespace psim::platform::kafka {

namespace asio = boost::asio;

struct SchemaRegistryOptions {
  /// Confluent-compatible API: "http://registry:8080/apis/ccompat/v7" (TLS - step 2.6).
  std::string url;
  std::chrono::milliseconds timeout{5000};
};

/// Thin read-only client of the Schema Registry (ADR-003): services resolve the ids of their
/// subjects at start and cache them; schemas are registered only by the installer and CI.
class SchemaRegistry {
 public:
  [[nodiscard]] static Result<SchemaRegistry> create(const SchemaRegistryOptions& options);

  /// Id of the latest version of `subject` ("<topic>-value", TopicNameStrategy).
  asio::awaitable<Result<std::int32_t>> latest_id(std::string subject);

  /// Schema text by id (cached: ids are immutable).
  asio::awaitable<Result<std::string>> schema(std::int32_t id);

 private:
  SchemaRegistry(std::string host, std::string port, std::string base, std::chrono::milliseconds timeout);
  asio::awaitable<Result<std::string>> get(std::string path);

  std::string host_;
  std::string port_;
  std::string base_;
  std::chrono::milliseconds timeout_;
  std::map<std::int32_t, std::string> schemas_;
};

}  // namespace psim::platform::kafka
