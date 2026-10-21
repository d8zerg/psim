#pragma once
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::event_history {
std::unique_ptr<platform::clickhouse::Pool> history_connection(const platform::runtime::Context& context,
                                                               bool reader = false);
std::unique_ptr<platform::runtime::Component> history_http(const platform::runtime::Context& context);
std::unique_ptr<platform::runtime::Component> history_retention(const platform::runtime::Context& context);
}  // namespace psim::event_history
