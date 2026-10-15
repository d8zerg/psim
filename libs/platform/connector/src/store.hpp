#pragma once

#include <nlohmann/json.hpp>
#include <psim/connector/v1/connector.pb.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "psim/platform/connector/client.hpp"

namespace psim::platform::connector::detail {
[[nodiscard]] std::string uuid7();
[[nodiscard]] bool uuid(std::string_view text);
[[nodiscard]] nlohmann::json encode(const google::protobuf::Message& message);
void decode(const nlohmann::json& json, google::protobuf::Message* message);

struct Claim {
  bool execute = false;
  wire::CommandResult result;
};

class Store final {
 public:
  explicit Store(Options options);
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) = delete;
  Store& operator=(Store&&) = delete;
  wire::ConnectorEvent append(wire::ConnectorEvent event);
  [[nodiscard]] std::vector<wire::ConnectorEvent> pending(std::size_t count, std::size_t bytes) const;
  void acknowledge(const std::vector<std::string>& ids);
  [[nodiscard]] std::vector<wire::SourceCursor> cursors() const;
  Claim claim(const wire::Command& command);
  void complete(const wire::CommandResult& result);
  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] std::size_t bytes() const;

 private:
  void record(const nlohmann::json& operation, bool admission = false);
  void apply(const nlohmann::json& operation);
  void compact();
  void recover();
  [[nodiscard]] nlohmann::json snapshot() const;
  Options options_;
  int lock_ = -1;
  int file_ = -1;
  int directory_ = -1;
  std::size_t bytes_ = 0;
  bool failed_ = false;
  mutable std::mutex mutex_;
  std::map<std::string, wire::ConnectorEvent> events_;
  std::deque<std::string> order_;
  std::map<std::string, wire::SourceCursor> cursors_;
  std::map<std::string, nlohmann::json> commands_;
};
}  // namespace psim::platform::connector::detail
