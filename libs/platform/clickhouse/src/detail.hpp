#pragma once
#include <clickhouse/block.h>
#include <clickhouse/query.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"

namespace psim::platform::clickhouse::detail {
[[nodiscard]] bool identifier(std::string_view value);
[[nodiscard]] Result<::clickhouse::Query> select_query(const Select& query, const PoolOptions& options);
[[nodiscard]] std::string fingerprint(std::string_view value);
[[nodiscard]] Result<std::string> encode(const ::clickhouse::Block& block, std::size_t limit);
[[nodiscard]] Result<::clickhouse::Block> decode(std::string_view bytes, std::size_t limit);
[[nodiscard]] Result<std::size_t> block_bytes(const ::clickhouse::Block& block, std::size_t limit);
}  // namespace psim::platform::clickhouse::detail
