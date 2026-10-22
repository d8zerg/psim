#include "psim/domain/processing/mapping.hpp"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace psim::domain::processing {
namespace {
using Value = std::variant<bool, std::optional<std::string>>;

class Parser {
 public:
  Parser(std::string_view input, const MappingInput& raw) : input_(input), raw_(&raw) {}

  std::expected<bool, std::string> run() {
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
  static std::expected<bool, std::string> invalid() {
    return std::unexpected("invalid or unsupported mapping condition");
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

  std::optional<std::uint32_t> hex() {
    if (input_.size() - position_ < 4) {
      return {};
    }
    const auto text = input_.substr(position_, 4);
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(text.begin(), text.end(), result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.end()) {
      return {};
    }
    position_ += 4;
    return result;
  }

  std::optional<std::string> string_atom() {
    space();
    if (position_ < input_.size() && input_[position_] == '"') {
      ++position_;
      std::string output;
      while (position_ < input_.size()) {
        const auto ch = input_[position_++];
        if (ch == '"') {
          return output;
        }
        if (static_cast<unsigned char>(ch) < 32) {
          bad_ = true;
          return {};
        }
        if (ch != '\\') {
          output += ch;
          continue;
        }
        if (position_ == input_.size()) {
          break;
        }
        const auto escaped = input_[position_++];
        switch (escaped) {
          case '"':
          case '\\':
          case '/':
            output += escaped;
            break;
          case 'b':
            output += '\b';
            break;
          case 'f':
            output += '\f';
            break;
          case 'n':
            output += '\n';
            break;
          case 'r':
            output += '\r';
            break;
          case 't':
            output += '\t';
            break;
          case 'u': {
            auto code = hex();
            if (!code) {
              bad_ = true;
              return {};
            }
            if (*code >= 0xD800 && *code <= 0xDBFF) {
              if (input_.substr(position_, 2) != "\\u") {
                bad_ = true;
                return {};
              }
              position_ += 2;
              const auto low = hex();
              if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                bad_ = true;
                return {};
              }
              *code = 0x10000 + ((*code - 0xD800) << 10U) + (*low - 0xDC00);
            } else if (*code >= 0xDC00 && *code <= 0xDFFF) {
              bad_ = true;
              return {};
            }
            if (*code < 0x80) {
              output += static_cast<char>(*code);
            } else if (*code < 0x800) {
              output += static_cast<char>(0xC0U | (*code >> 6U));
              output += static_cast<char>(0x80U | (*code & 0x3FU));
            } else if (*code < 0x10000) {
              output += static_cast<char>(0xE0U | (*code >> 12U));
              output += static_cast<char>(0x80U | ((*code >> 6U) & 0x3FU));
              output += static_cast<char>(0x80U | (*code & 0x3FU));
            } else {
              output += static_cast<char>(0xF0U | (*code >> 18U));
              output += static_cast<char>(0x80U | ((*code >> 12U) & 0x3FU));
              output += static_cast<char>(0x80U | ((*code >> 6U) & 0x3FU));
              output += static_cast<char>(0x80U | (*code & 0x3FU));
            }
            break;
          }
          default:
            bad_ = true;
            return {};
        }
      }
      bad_ = true;
      return {};
    }
    if (take("raw.raw_code")) {
      return std::string(raw_->raw_code);
    }
    if (take("raw.source_ref")) {
      return std::string(raw_->source_ref);
    }
    if (take("raw.attributes[")) {
      const auto name = string_atom();
      if (!take("]") || !name) {
        bad_ = true;
        return {};
      }
      return raw_->attribute ? raw_->attribute(*name) : std::nullopt;
    }
    bad_ = true;
    return {};
  }

  std::string_view input_;
  const MappingInput* raw_;
  std::size_t position_ = 0;
  bool bad_ = false;
};
}  // namespace

std::expected<bool, std::string> mapping_condition(std::string_view expression, const MappingInput& raw) {
  return Parser(expression, raw).run();
}
}  // namespace psim::domain::processing
