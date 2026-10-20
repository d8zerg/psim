#include "condition.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::normalizer {
namespace {
using Value = std::variant<bool, std::optional<std::string>>;

class Parser {
 public:
  Parser(std::string_view input, const ingest::v1::RawEvent& raw) : input_(input), raw_(&raw) {}

  platform::Result<bool> run() {
    if (input_.empty()) {
      return true;
    }
    if (input_.size() > 4000) {
      return invalid();
    }
    auto value = expression(0);
    space();
    if (bad_ || position_ != input_.size() || !std::holds_alternative<bool>(value)) {
      return invalid();
    }
    return std::get<bool>(value);
  }

 private:
  static platform::Result<bool> invalid() {
    return platform::fail(platform::ErrorCode::kProcessingPayloadInvalid, "invalid or unsupported mapping condition");
  }

  void space() {
    while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
      ++position_;
    }
  }

  bool take(std::string_view text) {
    space();
    if (!input_.substr(position_).starts_with(text)) {
      return false;
    }
    position_ += text.size();
    return true;
  }

  bool boolean(const Value& value) {
    if (const auto* result = std::get_if<bool>(&value)) {
      return *result;
    }
    bad_ = true;
    return false;
  }

  std::optional<std::string> string_value(const Value& value) {
    if (const auto* result = std::get_if<std::optional<std::string>>(&value)) {
      return *result;
    }
    bad_ = true;
    return {};
  }

  Value expression(std::size_t depth) {
    if (depth > 32) {
      bad_ = true;
      return false;
    }
    auto result = conjunction(depth + 1);
    while (take("||")) {
      auto right = conjunction(depth + 1);
      const auto left_bool = boolean(result);
      const auto right_bool = boolean(right);
      result = left_bool || right_bool;
    }
    return result;
  }

  Value conjunction(std::size_t depth) {
    auto result = comparison(depth);
    while (take("&&")) {
      auto right = comparison(depth);
      const auto left_bool = boolean(result);
      const auto right_bool = boolean(right);
      result = left_bool && right_bool;
    }
    return result;
  }

  Value comparison(std::size_t depth) {
    auto left = atom(depth);
    const bool equal = take("==");
    const bool unequal = !equal && take("!=");
    if (!equal && !unequal) {
      return left;
    }
    const auto right = atom(depth);
    if (left.index() != right.index()) {
      bad_ = true;
      return false;
    }
    if (std::holds_alternative<bool>(left)) {
      return equal == (boolean(left) == boolean(right));
    }
    const auto lhs = string_value(left);
    const auto rhs = string_value(right);
    return lhs && rhs && (equal == (*lhs == *rhs));
  }

  Value atom(std::size_t depth) {
    if (depth > 32) {
      bad_ = true;
      return false;
    }
    if (take("!")) {
      const auto value = atom(depth + 1);
      return !boolean(value);
    }
    if (take("(")) {
      auto value = expression(depth + 1);
      if (!take(")")) {
        bad_ = true;
      }
      return value;
    }
    if (take("true")) {
      return true;
    }
    if (take("false")) {
      return false;
    }
    if (take("has(")) {
      const auto value = atom(depth + 1);
      if (!take(")")) {
        bad_ = true;
      }
      return string_value(value).has_value();
    }
    Value result = string_atom();
    if (take(".")) {
      enum class Method : std::uint8_t { kStarts, kEnds, kContains };
      Method method{};
      if (take("startsWith(")) {
        method = Method::kStarts;
      } else if (take("endsWith(")) {
        method = Method::kEnds;
      } else if (take("contains(")) {
        method = Method::kContains;
      } else {
        bad_ = true;
        return false;
      }
      const auto argument = string_atom();
      if (!take(")")) {
        bad_ = true;
      }
      const auto text = string_value(result);
      const auto pattern = string_value(argument);
      if (!text || !pattern) {
        return false;
      }
      if (method == Method::kStarts) {
        return text->starts_with(*pattern);
      }
      if (method == Method::kEnds) {
        return text->ends_with(*pattern);
      }
      return text->contains(*pattern);
    }
    return result;
  }

  std::optional<std::string> string_atom() {
    space();
    if (position_ < input_.size() && input_[position_] == '"') {
      const auto start = position_++;
      bool escaped = false;
      while (position_ < input_.size()) {
        const auto ch = input_[position_++];
        if (ch == '"' && !escaped) {
          const auto json = nlohmann::json::parse(input_.substr(start, position_ - start), nullptr, false);
          if (!json.is_string()) {
            bad_ = true;
            return {};
          }
          return json.get<std::string>();
        }
        escaped = ch == '\\' && !escaped;
      }
      bad_ = true;
      return {};
    }
    if (take("raw.raw_code")) {
      return raw_->raw_code();
    }
    if (take("raw.source_ref")) {
      return raw_->source_ref();
    }
    if (take("raw.attributes[")) {
      const auto name = string_atom();
      if (!take("]") || !name) {
        bad_ = true;
        return {};
      }
      const auto value = raw_->attributes().find(*name);
      return value == raw_->attributes().end() ? std::nullopt : std::optional<std::string>(value->second);
    }
    bad_ = true;
    return {};
  }

  std::string_view input_;
  const ingest::v1::RawEvent* raw_;
  std::size_t position_ = 0;
  bool bad_ = false;
};
}  // namespace

platform::Result<bool> condition(std::string_view expression, const ingest::v1::RawEvent& raw) {
  return Parser(expression, raw).run();
}
}  // namespace psim::normalizer
