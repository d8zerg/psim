#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "psim/config/v1/config.pb.h"
#include "rules.hpp"

namespace psim::correlation::live {
namespace p = platform;
using State = processing::v1::LiveKeyState;

struct Options {
  std::int64_t idle_ms = 30000;
  std::int64_t lateness_ms = 10000;
  std::int64_t clock_skew_ms = 5000;
  std::int64_t replay_age_ms = 3600000;
  std::size_t max_entries = 100000;
  std::size_t max_state_bytes = 1048576;
  std::optional<std::int64_t> watermark_limit_ms{};
};

struct Result {
  std::vector<rules::Decision> decisions{};
  bool late = false;
  bool corrected = false;
};

class Control {
 public:
  explicit Control(std::set<std::string> taxonomy, std::size_t max_entries = 100000);
  p::Result<void> config(const config::v1::ConfigRecord& record);
  p::Result<void> catalog(const catalog::v1::CatalogRecord& record);
  p::Result<std::shared_ptr<const rules::CompiledRule>> compiled(const std::string& tenant, const rules::Rule& rule);
  const rules::Context& context(const std::string& tenant) const;

  const std::map<std::string, processing::v1::RuleSet>& sets() const { return sets_; }

  const std::vector<std::shared_ptr<const rules::CompiledRule>>& active(const std::string& tenant) const;

  std::uint64_t revision() const { return revision_; }

  Options options(const std::string& tenant, Options defaults) const;

 private:
  std::set<std::string> taxonomy_;
  std::size_t max_entries_;
  std::map<std::string, processing::v1::RuleSet> sets_;
  std::map<std::string, config::v1::ProcessingSettings> settings_;
  std::map<std::string, rules::Context> contexts_;
  std::map<std::string, catalog::v1::Device> devices_;

  struct CachedRule {
    std::shared_ptr<const rules::CompiledRule> value;
    std::string wire;
  };

  std::map<std::string, CachedRule> compiled_;
  std::map<std::string, std::vector<std::shared_ptr<const rules::CompiledRule>>> active_;
  std::uint64_t revision_ = 0;
};

p::Result<Result> advance(const rules::CompiledRule& rule, State& saved, const rules::Event* incoming,
                          const rules::Context& context, const std::string& tenant, std::int64_t now_ms,
                          bool inventory = false, Options options = {});
std::string state_key(const std::string& tenant, const rules::Rule& rule, const std::string& correlation_key);
}  // namespace psim::correlation::live
