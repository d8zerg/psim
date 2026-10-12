#include "sql.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::pg::detail {
bool identifier(std::string_view value) {
  return !value.empty() && value.size() <= 63 && value.front() >= 'a' && value.front() <= 'z'
         && std::ranges::all_of(
             value, [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
}

Result<std::string> schema_sql(std::string_view schema, std::string_view sql) {
  if (!identifier(schema)) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL service schema");
  }
  std::string statement(sql);
  std::size_t offset = 0;
  const std::string quoted = "\"" + std::string(schema) + "\"";
  while ((offset = statement.find("@schema@", offset)) != std::string::npos) {
    statement.replace(offset, 8, quoted);
    offset += quoted.size();
  }
  return statement;
}

std::string fingerprint(std::string_view value) {
  std::array<unsigned char, 32> bytes{};
  std::size_t length = 0;
  if (EVP_Q_digest(nullptr, "SHA256", nullptr, value.data(), value.size(), bytes.data(), &length) != 1
      || length != bytes.size()) {
    return {};
  }
  std::string result;
  for (const auto byte : bytes) {
    result += std::format("{:02x}", byte);
  }
  return result;
}
}  // namespace psim::platform::pg::detail
