#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/catalog/v1/catalog.pb.h"
#include "psim/catalog/v1/maintenance.pb.h"
#include "psim/domain/processing/expression.hpp"
#include "psim/platform/error.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/internal.pb.h"
#include "psim/processing/v1/rules.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace psim::correlation::rules {
namespace p = platform;
namespace expr = domain::processing;
using Rule = processing::v1::CorrelationRule;
using Event = processing::v1::EventRecord;
using Signal = processing::v1::Signal;
using Filter = processing::v1::EventFilter;

struct Violation {
  std::string field;
  std::string message;
};

struct Limits {
  std::int64_t max_window_ms = 86400000;
  std::size_t max_events = 100000;
  std::size_t max_signals = 10000;
  std::size_t max_keys = 10000;
};

struct CompiledFilter {
  Filter filter;
  expr::Expression condition;
};

struct SummaryPart {
  std::string literal{};
  std::optional<expr::Expression> expression{};
};

struct CompiledRule {
  Rule rule;
  CompiledFilter filter;
  std::vector<CompiledFilter> parts{};
  std::optional<CompiledFilter> trigger{};
  std::optional<expr::Expression> priority{};
  std::vector<SummaryPart> summary{};
  std::set<std::string> referenced_fields{};
};

struct Validation {
  std::vector<Violation> violations{};
  std::optional<CompiledRule> compiled{};
};

struct Context {
  std::map<std::string, catalog::v1::Location> locations{};
  std::vector<catalog::v1::MaintenanceWindow> maintenance{};
  std::vector<Event> subjects{};
};

struct Decision {
  Signal signal;
  std::string id;
  bool suppressed = false;
  std::vector<std::string> maintenance_ids{};
  std::vector<std::pair<std::string, std::uint64_t>> maintenance_versions{};
};

struct ReplayResult {
  std::size_t processed = 0;
  std::vector<Decision> decisions{};
};

expr::ExpressionSchema expression_schema();
p::Result<Rule> parse_document(std::string_view document, bool yaml = false);
p::Result<std::set<std::string>> load_types(const std::string& file);
Validation compile(const Rule& rule, const std::set<std::string>& taxonomy, Limits limits = {});
p::Result<ReplayResult> replay(const CompiledRule& rule, std::vector<Event> events, const Context& context,
                               std::string tenant, std::int64_t from_ms, std::int64_t to_ms, Limits limits = {});

std::string correlation_key(const Event& event, const Context& context, processing::v1::CorrelationScope scope);
p::Result<bool> selects(const CompiledRule& rule, const Event& event, const Context& context, bool inventory = false);
// Both entry points use the same matching and signal identity implementation.
p::Result<std::vector<Decision>> live_step(const CompiledRule& rule, processing::v1::LiveKeyState& state,
                                           const std::vector<Event>& events, const Context& context,
                                           const std::string& tenant, std::int64_t until_ms,
                                           const Event* inventory = nullptr, Limits limits = {});
}  // namespace psim::correlation::rules
