#include "detail.hpp"

#include <clickhouse/base/input.h>
#include <clickhouse/base/output.h>
#include <clickhouse/base/wire_format.h>
#include <clickhouse/block.h>
#include <clickhouse/columns/factory.h>
#include <clickhouse/query.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::clickhouse::detail {
namespace {
struct BoundedOutput final : public ::clickhouse::OutputStream {
 public:
  explicit BoundedOutput(std::size_t limit) : limit_(limit) {}

  std::string finish() { return std::move(bytes_); }

 private:
  std::size_t DoWrite(const void* data,
                      std::size_t size) override {  // NOLINT(readability-identifier-naming): upstream API
    if (size > limit_ - bytes_.size()) {
      throw std::length_error("ClickHouse block exceeds configured bound");
    }
    bytes_.append(static_cast<const char*>(data), size);
    return size;
  }

  std::string bytes_;
  std::size_t limit_;
};
}  // namespace

bool identifier(std::string_view value) {
  if (value.empty() || value.size() > 128
      || (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_')) {
    return false;
  }
  return std::ranges::all_of(value, [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '_'; });
}

Result<::clickhouse::Query> select_query(const Select& query, const PoolOptions& options) {
  if (!identifier(query.table) || !identifier(query.time_column) || query.tenant.empty() || query.tenant.size() > 256
      || query.from_ms < 0 || query.from_ms >= query.to_ms || query.columns.empty() || query.columns.size() > 128
      || query.equals.size() > 32 || query.limit == 0 || query.limit > options.max_result_rows) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse query scope or limits");
  }
  // psim-arch: allow-sql (only validated identifiers form SQL; values use native parameters)
  std::string sql = "SELECT ";
  for (const auto& column : query.columns) {
    if (!identifier(column)) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse column");
    }
    if (sql != "SELECT ") {
      sql += ", ";
    }
    sql += "`" + column + "`";
  }
  // psim-arch: allow-sql (validated identifiers or generated test names only)
  sql += " FROM `" + query.table + "`";
  if (query.final) {
    sql += " FINAL";
  }
  // psim-arch: allow-sql (validated identifiers or generated test names only)
  sql += " WHERE tenant_id = {psim_tenant:String} AND `" + query.time_column
         + "` >= fromUnixTimestamp64Milli({psim_from:Int64}) AND `" + query.time_column
         + "` < fromUnixTimestamp64Milli({psim_to:Int64})";
  ::clickhouse::QueryParams params{{"psim_tenant", query.tenant},
                                   {"psim_from", std::to_string(query.from_ms)},
                                   {"psim_to", std::to_string(query.to_ms)}};
  std::size_t index = 0;
  for (const auto& [column, value] : query.equals) {
    if (!identifier(column) || value.size() > 65536) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse predicate");
    }
    const auto parameter = "psim_value_" + std::to_string(index++);
    // psim-arch: allow-sql (validated identifiers or generated test names only)
    sql += " AND `" + column + "` = {";
    sql += parameter + ":String}";
    params.emplace(parameter, value);
  }
  // psim-arch: allow-sql (validated identifiers or generated test names only)
  sql += " LIMIT {psim_limit:UInt64}";
  params.emplace("psim_limit", std::to_string(query.limit));
  ::clickhouse::Query native(sql);
  native.SetParams(std::move(params));
  native.SetSetting("max_execution_time",
                    {std::to_string(std::max<std::int64_t>(1, (options.query_timeout.count() + 999) / 1000)), 1});
  native.SetSetting("max_result_rows", {std::to_string(options.max_result_rows), 1});
  native.SetSetting("max_result_bytes", {std::to_string(options.max_result_bytes), 1});
  native.SetSetting("result_overflow_mode", {"throw", 1});
  native.SetSetting("max_block_size", {"8192", 1});
  return native;
}

std::string fingerprint(std::string_view value) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  if (EVP_Digest(value.data(), value.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("SHA-256 failed");
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string result;
  result.reserve(static_cast<std::size_t>(size) * 2);
  for (unsigned int index = 0; index < size; ++index) {
    result += kHex[digest[index] >> 4];
    result += kHex[digest[index] & 15U];
  }
  return result;
}

Result<std::string> encode(const ::clickhouse::Block& block, std::size_t limit) {
  try {
    BoundedOutput output(limit);
    ::clickhouse::WireFormat::WriteUInt64(output, block.GetColumnCount());
    ::clickhouse::WireFormat::WriteUInt64(output, block.GetRowCount());
    for (std::size_t index = 0; index < block.GetColumnCount(); ++index) {
      const auto column = block[index];
      if (!identifier(block.GetColumnName(index)) || column->Size() != block.GetRowCount()) {
        return fail(ErrorCode::kCommonInvalidArgument, "inconsistent ClickHouse block");
      }
      ::clickhouse::WireFormat::WriteString(output, block.GetColumnName(index));
      ::clickhouse::WireFormat::WriteString(output, column->Type()->GetName());
      column->Save(&output);
    }
    return output.finish();
  } catch (const std::exception&) {
    return fail(ErrorCode::kCommonRateLimited, "ClickHouse block serialization failed or exceeds limit");
  }
}

Result<::clickhouse::Block> decode(std::string_view bytes, std::size_t limit) {
  if (bytes.size() > limit) {
    return fail(ErrorCode::kCommonRateLimited, "ClickHouse journal block exceeds limit");
  }
  try {
    ::clickhouse::ArrayInput input(bytes.data(), bytes.size());
    std::uint64_t columns = 0;
    std::uint64_t rows = 0;
    if (!::clickhouse::WireFormat::ReadUInt64(input, &columns) || !::clickhouse::WireFormat::ReadUInt64(input, &rows)
        || columns > 256 || rows > 1000000) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse journal block header");
    }
    ::clickhouse::Block block;
    for (std::uint64_t index = 0; index < columns; ++index) {
      std::string name;
      std::string type;
      if (!::clickhouse::WireFormat::ReadString(input, &name) || !::clickhouse::WireFormat::ReadString(input, &type)
          || !identifier(name)) {
        return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse journal column");
      }
      auto column = ::clickhouse::CreateColumnByType(type);
      if (!column || !column->Load(&input, static_cast<std::size_t>(rows))) {
        return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse journal column data");
      }
      block.AppendColumn(name, column);
    }
    if (!input.Exhausted()) {
      return fail(ErrorCode::kCommonInvalidArgument, "trailing ClickHouse journal bytes");
    }
    return block;
  } catch (const std::exception&) {
    return fail(ErrorCode::kCommonInvalidArgument, "cannot decode ClickHouse journal block");
  }
}

Result<std::size_t> block_bytes(const ::clickhouse::Block& block, std::size_t limit) {
  auto bytes = encode(block, limit);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  return bytes->size();
}
}  // namespace psim::platform::clickhouse::detail
