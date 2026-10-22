#pragma once
#include "facts.hpp"

namespace psim::projection {
boost::asio::awaitable<p::Result<std::size_t>> rebuild(p::pg::Pool* source, p::clickhouse::Pool* destination,
                                                       std::string tenant, std::int64_t from, std::int64_t to);
}  // namespace psim::projection
