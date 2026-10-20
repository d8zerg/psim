#pragma once

#include <span>
#include <string>
#include <unordered_map>

#include "psim/config/v1/config.pb.h"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/processing/v1/event.pb.h"

namespace psim::event_history {
namespace p = platform;

/// Retention is captured into prepared native blocks and remains stable on recovery.
class Events {
 public:
  p::Result<void> config(const config::v1::ConfigRecord& record);
  p::Result<p::clickhouse::Converted> convert(std::span<const p::kafka::Message* const> messages) const;
  p::Result<p::clickhouse::Converted> convert_records(std::span<const processing::v1::EventRecord> records) const;

 private:
  std::unordered_map<std::string, config::v1::RetentionSettings> retention_;
};

p::clickhouse::Migration migration(const std::string& cluster);
}  // namespace psim::event_history
