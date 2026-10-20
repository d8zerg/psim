#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace psim::domain::processing {
enum class ValueType : std::uint8_t { kBool, kInt, kString };
using Value = std::variant<bool, std::int64_t, std::string>;
using ExpressionSchema = std::map<std::string, ValueType, std::less<>>;
using Resolve = std::function<std::optional<Value>(std::string_view)>;

/// Immutable, typed CEL subset. No I/O, iteration, dynamic calls or implicit conversions.
class Expression {
 public:
  static std::expected<Expression, std::string> compile(std::string_view source, ValueType expected,
                                                        const ExpressionSchema& schema);
  [[nodiscard]] std::expected<Value, std::string> evaluate(const Resolve& resolve, std::size_t budget = 4096) const;
  [[nodiscard]] std::size_t cost() const;
  [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> integer_bounds() const;
  [[nodiscard]] std::set<std::string> fields() const;

 private:
  struct Program;

  explicit Expression(std::shared_ptr<const Program> program) : program_(std::move(program)) {}

  std::shared_ptr<const Program> program_;
};
}  // namespace psim::domain::processing
