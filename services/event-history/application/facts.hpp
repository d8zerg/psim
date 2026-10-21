#pragma once
#include "events.hpp"
#include "psim/incident/v1/events.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace psim::event_history {
class Facts {
 public:
  p::Result<void> config(const config::v1::ConfigRecord& record);
  p::Result<p::clickhouse::Converted> convert(std::span<const p::kafka::Message* const> messages) const;
  p::Result<p::clickhouse::Converted> signals(std::span<const processing::v1::SignalRecord> records) const;
  [[nodiscard]] std::int64_t retention(std::string_view tenant) const;

 private:
  std::unordered_map<std::string, std::pair<std::uint64_t, std::int64_t>> retention_;
};

p::clickhouse::Migration facts_migration(const std::string& cluster);
[[nodiscard]] bool valid_signal(const processing::v1::SignalRecord& record);
}  // namespace psim::event_history
