#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "psim/catalog/v1/catalog.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/internal.pb.h"

namespace psim::normalizer {
namespace p = platform;

struct Taxon {
  common::v1::EventClass event_class;
  common::v1::Severity severity;
};

using Taxonomy = std::unordered_map<std::string, Taxon>;

struct Prepared {
  processing::v1::EventRecord event;
  processing::v1::NormalizerStateRecord state;
  std::string key;
};

/// Pure in-memory transformation and transactional source cursors. Only committed cursors
/// survive rollback; catalog/config updates apply between input batches on the same shard.
class Model {
 public:
  explicit Model(Taxonomy taxonomy, std::size_t max_entries = 1000000);
  p::Result<void> catalog(const catalog::v1::CatalogRecord& record);
  p::Result<void> config(const config::v1::ConfigRecord& record);
  p::Result<std::optional<Prepared>> prepare(const ingest::v1::RawEventRecord& raw) const;
  p::Result<void> restore(const processing::v1::NormalizerStateRecord& record, std::int32_t partition);
  void stage(Prepared prepared, std::int32_t partition);
  [[nodiscard]] std::vector<std::pair<std::int32_t, processing::v1::NormalizerStateRecord>> pending_records() const;
  void commit();
  void abort();
  void revoke(std::int32_t partition);

 private:
  struct Cursor {
    processing::v1::NormalizerStateRecord record;
    std::int32_t partition;
  };

  Taxonomy taxonomy_;
  std::size_t max_entries_;
  std::size_t pending_new_ = 0;
  std::unordered_map<std::string, catalog::v1::CatalogRecord> catalog_;
  std::unordered_map<std::string, std::string> sources_;
  std::unordered_map<std::string, processing::v1::MappingRuleSet> mappings_;
  std::unordered_map<std::string, config::v1::ProcessingSettings> settings_;
  std::unordered_map<std::string, Cursor> committed_, pending_;
  std::unordered_map<std::string, google::protobuf::Timestamp> confirmed_, pending_confirmed_;
};

p::Result<Taxonomy> load_taxonomy(const std::string& path);
}  // namespace psim::normalizer
