#pragma once
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::projection {
namespace p = platform;
std::unique_ptr<p::clickhouse::Pool> connection(const p::runtime::Context& context, bool reader = false);
std::unique_ptr<p::runtime::Component> reports_http(const p::runtime::Context& context);
}  // namespace psim::projection
