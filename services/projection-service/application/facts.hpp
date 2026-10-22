#pragma once
#include "psim/incident/v1/events.pb.h"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/response/v1/events.pb.h"

namespace psim::projection {
namespace p = platform;

struct Fact {
  std::string tenant{}, incident{}, site{}, zone{}, type{}, assignee{};
  std::uint64_t version = 0;
  std::int64_t created = 0, acknowledged = 0, resolved = 0, ack_target = 0, resolve_target = 0;
  std::uint32_t escalation = 0;
  std::uint8_t kind = 1, priority = 0, state = 0, resolution = 0;
};

p::Result<Fact> fact(std::string tenant, const incident::v1::Incident& incident);
p::Result<Fact> fact(std::string tenant, const response::v1::SlaPolicyPinned& targets);
p::Result<p::clickhouse::Converted> convert(std::span<const p::kafka::Message* const> messages);
::clickhouse::Block block(std::span<const Fact> facts);
p::clickhouse::Migration migration(const std::string& cluster);
}  // namespace psim::projection
