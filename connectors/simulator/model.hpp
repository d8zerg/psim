#pragma once

#include <psim/connector/v1/connector.pb.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <vector>

namespace psim::connectors::simulator {
namespace wire = ::psim::connector::v1;

struct Profile {
  std::size_t devices = 1000;
  std::size_t count = 100000;
  std::size_t batch = 500;
  std::size_t rate = 0;
  std::size_t burst_every = 10000;
  std::size_t burst_size = 1000;
  std::string scenario = "mixed";
  std::string source_prefix = "device-";
  std::string run_id = "default";
  bool conformance = false;
};

class Model final {
 public:
  explicit Model(Profile profile);
  wire::ConnectorEvent event(std::size_t index) const;
  wire::CommandResult command(const wire::Command& command, const std::stop_token& stop);
  std::vector<wire::StateObservation> snapshot(const std::stop_token& stop) const;
  std::uint64_t scheduled_nanoseconds(std::size_t index) const;

 private:
  Profile profile_;
  mutable std::mutex mutex_;
  std::vector<bool> armed_;
};
}  // namespace psim::connectors::simulator
