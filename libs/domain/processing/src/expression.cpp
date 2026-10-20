#include "psim/domain/processing/expression.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace psim::domain::processing {
namespace {
enum class Op : std::uint8_t {
  kLiteral,
  kField,
  kNot,
  kNegate,
  kHas,
  kAnd,
  kOr,
  kEqual,
  kNotEqual,
  kLess,
  kLessEqual,
  kGreater,
  kGreaterEqual,
  kAdd,
  kSubtract,
  kMultiply,
  kDivide,
  kModulo,
  kChoose,
  kStartsWith,
  kEndsWith,
  kContains
};

struct Node {
  Op op = Op::kLiteral;
  ValueType type = ValueType::kBool;
  Value value = false;
  std::string field{};
  std::size_t left = 0, right = 0, third = 0, depth = 1;
};

ValueType type(const Value& value) {
  return static_cast<ValueType>(value.index());
}

struct Token {
  std::string text;
  bool literal = false;
  bool number = false;
};

bool space(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

bool digit(char value) {
  return value >= '0' && value <= '9';
}

bool letter(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

bool identifier_char(char value) {
  return digit(value) || letter(value) || value == '_';
}

class Parser {
 public:
  Parser(std::string_view source, const ExpressionSchema& schema) : source_(source), schema_(&schema) { next(); }

  std::expected<std::vector<Node>, std::string> parse(ValueType expected) {
    const auto root = expression(0);
    if (token_.literal || token_.number || !token_.text.empty()) {
      fail("unexpected trailing token");
    }
    if (nodes_[root].type != expected) {
      fail("expression result has the wrong type");
    }
    if (!error_.empty()) {
      return std::unexpected(error_);
    }
    // The root is last; node zero is a bounded error sentinel.
    return std::move(nodes_);
  }

 private:
  void fail(std::string message) {
    if (error_.empty()) {
      error_ = std::move(message);
    }
  }

  void next() {
    token_ = {};
    while (position_ < source_.size() && space(source_[position_])) {
      ++position_;
    }
    if (position_ == source_.size()) {
      return;
    }
    const auto ch = source_[position_++];
    if (ch == '\'' || ch == '"') {
      token_.literal = true;
      while (position_ < source_.size()) {
        auto value = source_[position_++];
        if (value == ch) {
          return;
        }
        if (value == '\\') {
          if (position_ == source_.size()) {
            break;
          }
          value = source_[position_++];
          switch (value) {
            case 'n':
              value = '\n';
              break;
            case 'r':
              value = '\r';
              break;
            case 't':
              value = '\t';
              break;
            case '\\':
            case '\'':
            case '"':
              break;
            default:
              fail("unsupported string escape");
              return;
          }
        }
        if (value == '\0') {
          fail("NUL is not allowed");
          return;
        }
        token_.text += value;
      }
      fail("unterminated string");
      return;
    }
    token_.text += ch;
    if (identifier_char(ch)) {
      token_.number = digit(ch);
      while (position_ < source_.size()) {
        const auto part = source_[position_];
        if (!identifier_char(part)) {
          break;
        }
        token_.text += source_[position_++];
      }
    } else if (position_ < source_.size()) {
      const auto pair = token_.text + source_[position_];
      if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" || pair == "||") {
        token_.text = pair;
        ++position_;
      }
    }
  }

  bool take(std::string_view text) {
    if (token_.literal || token_.text != text) {
      return false;
    }
    next();
    return true;
  }

  void expect(std::string_view text) {
    if (!take(text)) {
      fail("expected " + std::string(text));
    }
  }

  std::size_t add(Node node) {
    if (!error_.empty()) {
      return 0;
    }
    if (nodes_.size() >= 513 || node.depth > 32) {
      fail("expression exceeds node or depth limit");
      return 0;
    }
    nodes_.push_back(std::move(node));
    return nodes_.size() - 1;
  }

  std::size_t unary() {
    if (++nesting_ > 64) {
      fail("expression nesting limit");
      --nesting_;
      return 0;
    }
    const auto result = atom();
    --nesting_;
    return result;
  }

  std::size_t atom() {
    if (!error_.empty()) {
      return 0;
    }
    if (token_.literal) {
      auto value = token_.text;
      next();
      return add({.type = ValueType::kString, .value = std::move(value)});
    }
    if (token_.number) {
      std::int64_t value = 0;
      const auto parsed = std::from_chars(token_.text.data(), std::to_address(token_.text.end()), value);
      if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(token_.text.end())) {
        fail("invalid integer");
      }
      next();
      return add({.type = ValueType::kInt, .value = value});
    }
    if (take("true")) {
      return add({.value = true});
    }
    if (take("false")) {
      return add({.value = false});
    }
    if (take("(")) {
      const auto inner = expression(0);
      expect(")");
      return inner;
    }
    if (take("!")) {
      const auto child = unary();
      if (nodes_[child].type != ValueType::kBool) {
        fail("! requires bool");
      }
      return add({.op = Op::kNot, .left = child, .depth = nodes_[child].depth + 1});
    }
    if (take("-")) {
      if (token_.number) {
        const auto text = "-" + token_.text;
        std::int64_t value = 0;
        const auto parsed = std::from_chars(text.data(), std::to_address(text.end()), value);
        if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(text.end())) {
          fail("invalid integer");
        }
        next();
        return add({.type = ValueType::kInt, .value = value});
      }
      const auto child = unary();
      if (nodes_[child].type != ValueType::kInt) {
        fail("- requires int");
      }
      return add({.op = Op::kNegate, .type = ValueType::kInt, .left = child, .depth = nodes_[child].depth + 1});
    }
    if (take("has")) {
      expect("(");
      const auto child = expression(0);
      expect(")");
      if (nodes_[child].op != Op::kField) {
        fail("has requires a field");
      }
      return add({.op = Op::kHas, .left = child, .depth = nodes_[child].depth + 1});
    }
    if (token_.text.empty() || (!letter(token_.text.front()) && token_.text.front() != '_')) {
      fail("expected literal or field");
      return 0;
    }
    std::string field = token_.text;
    next();
    std::string method;
    while (take(".")) {
      const auto name = token_.text;
      if (token_.literal || name.empty() || !letter(name.front())) {
        fail("invalid field name");
        return 0;
      }
      next();
      if (take("(")) {
        method = name;
        break;
      }
      field += "." + name;
    }
    auto found = schema_->find(field);
    if (take("[")) {
      if (!token_.literal || token_.text.empty() || token_.text.size() > 256) {
        fail("map key must be a nonempty literal");
        return 0;
      }
      found = schema_->find(field + ".*");
      field += "." + token_.text;
      next();
      expect("]");
    }
    if (found == schema_->end()) {
      fail("unknown field: " + field);
      return 0;
    }
    auto result = add({.op = Op::kField, .type = found->second, .field = field});
    // A map selection may itself be a method receiver.
    if (method.empty() && take(".")) {
      method = token_.text;
      next();
      expect("(");
    }
    if (!method.empty()) {
      const auto argument = expression(0);
      expect(")");
      if (nodes_[result].type != ValueType::kString || nodes_[argument].type != ValueType::kString) {
        fail("string method requires strings");
      }
      Op op = Op::kContains;
      if (method == "startsWith") {
        op = Op::kStartsWith;
      } else if (method == "endsWith") {
        op = Op::kEndsWith;
      } else if (method != "contains") {
        fail("unsupported method");
      }
      result = add({.op = op,
                    .left = result,
                    .right = argument,
                    .depth = 1 + std::max(nodes_[result].depth, nodes_[argument].depth)});
    }
    return result;
  }

  static int precedence(std::string_view value) {
    if (value == "||") {
      return 1;
    }
    if (value == "&&") {
      return 2;
    }
    if (value == "==" || value == "!=") {
      return 3;
    }
    if (value == "<" || value == "<=" || value == ">" || value == ">=") {
      return 4;
    }
    if (value == "+" || value == "-") {
      return 5;
    }
    if (value == "*" || value == "/" || value == "%") {
      return 6;
    }
    return -1;
  }

  std::size_t expression(int minimum) {
    if (++recursion_ > 64) {
      fail("expression recursion limit");
      --recursion_;
      return 0;
    }
    auto left = unary();
    while (!token_.literal && precedence(token_.text) >= minimum && error_.empty()) {
      const auto symbol = token_.text;
      const int level = precedence(symbol);
      next();
      const auto right = expression(level + 1);
      const auto lhs = nodes_[left].type;
      const auto rhs = nodes_[right].type;
      if (lhs != rhs) {
        fail("operator arguments have different types");
      }
      Op op = Op::kEqual;
      ValueType result = ValueType::kBool;
      if (symbol == "&&" || symbol == "||") {
        op = symbol == "&&" ? Op::kAnd : Op::kOr;
        if (lhs != ValueType::kBool) {
          fail("logical operator requires bool");
        }
      } else if (symbol == "==") {
        op = Op::kEqual;
      } else if (symbol == "!=") {
        op = Op::kNotEqual;
      } else if (symbol == "<") {
        op = Op::kLess;
      } else if (symbol == "<=") {
        op = Op::kLessEqual;
      } else if (symbol == ">") {
        op = Op::kGreater;
      } else if (symbol == ">=") {
        op = Op::kGreaterEqual;
      } else {
        result = lhs;
        if (symbol == "+") {
          op = Op::kAdd;
        } else if (symbol == "-") {
          op = Op::kSubtract;
        } else if (symbol == "*") {
          op = Op::kMultiply;
        } else if (symbol == "/") {
          op = Op::kDivide;
        } else {
          op = Op::kModulo;
        }
        if (lhs != ValueType::kInt && (op != Op::kAdd || lhs != ValueType::kString)) {
          fail("arithmetic requires integers");
        }
      }
      if (level == 4 && lhs == ValueType::kBool) {
        fail("bool values are not ordered");
      }
      left = add({.op = op,
                  .type = result,
                  .left = left,
                  .right = right,
                  .depth = 1 + std::max(nodes_[left].depth, nodes_[right].depth)});
    }
    if (minimum == 0 && take("?")) {
      const auto yes = expression(0);
      expect(":");
      const auto no = expression(0);
      if (nodes_[left].type != ValueType::kBool || nodes_[yes].type != nodes_[no].type) {
        fail("invalid conditional types");
      }
      left = add({.op = Op::kChoose,
                  .type = nodes_[yes].type,
                  .left = left,
                  .right = yes,
                  .third = no,
                  .depth = 1 + std::max({nodes_[left].depth, nodes_[yes].depth, nodes_[no].depth})});
    }
    --recursion_;
    return left;
  }

  std::string_view source_;
  const ExpressionSchema* schema_;
  std::size_t position_ = 0, nesting_ = 0, recursion_ = 0;
  Token token_;
  std::string error_;
  std::vector<Node> nodes_{{}};
};

using Evaluated = std::expected<Value, std::string>;

Evaluated invalid(std::string message) {
  return std::unexpected(std::move(message));
}

Evaluated evaluate(const std::vector<Node>& nodes, std::size_t index, const Resolve& resolve, std::size_t& budget) {
  if (budget == 0) {
    return invalid("expression cost limit exceeded");
  }
  --budget;
  const auto& node = nodes[index];
  if (node.op == Op::kLiteral) {
    return node.value;
  }
  if (node.op == Op::kField) {
    auto value = resolve(node.field);
    if (!value) {
      return invalid("missing field: " + node.field);
    }
    if (type(*value) != node.type) {
      return invalid("field type mismatch");
    }
    if (auto* text = std::get_if<std::string>(&*value); text != nullptr && text->size() > 4096) {
      return invalid("field value exceeds string limit");
    }
    return *value;
  }
  if (node.op == Op::kHas) {
    return Value(resolve(nodes[node.left].field).has_value());
  }
  auto lhs = evaluate(nodes, node.left, resolve, budget);
  if (!lhs) {
    return lhs;
  }
  if (node.op == Op::kNot) {
    return Value(!std::get<bool>(*lhs));
  }
  if (node.op == Op::kNegate) {
    const auto number = std::get<std::int64_t>(*lhs);
    return number == std::numeric_limits<std::int64_t>::min() ? invalid("integer overflow") : Evaluated(Value(-number));
  }
  if (node.op == Op::kAnd && !std::get<bool>(*lhs)) {
    return Value(false);
  }
  if (node.op == Op::kOr && std::get<bool>(*lhs)) {
    return Value(true);
  }
  if (node.op == Op::kChoose) {
    return evaluate(nodes, std::get<bool>(*lhs) ? node.right : node.third, resolve, budget);
  }
  auto rhs = evaluate(nodes, node.right, resolve, budget);
  if (!rhs) {
    return rhs;
  }
  if (const auto* first = std::get_if<std::string>(&*lhs)) {
    const auto& second = std::get<std::string>(*rhs);
    const auto bytes = node.op == Op::kContains ? first->size() * std::max<std::size_t>(second.size(), 1)
                                                : first->size() + second.size();
    const auto work = 1 + (bytes / 64);
    if (budget < work) {
      return invalid("expression cost limit exceeded");
    }
    budget -= work;
  }
  switch (node.op) {
    case Op::kAnd:
    case Op::kOr:
      return rhs;
    case Op::kEqual:
      return Value(*lhs == *rhs);
    case Op::kNotEqual:
      return Value(*lhs != *rhs);
    case Op::kLess:
      return Value(*lhs < *rhs);
    case Op::kLessEqual:
      return Value(*lhs <= *rhs);
    case Op::kGreater:
      return Value(*lhs > *rhs);
    case Op::kGreaterEqual:
      return Value(*lhs >= *rhs);
    default:
      break;
  }
  if (node.type == ValueType::kString || node.op == Op::kStartsWith || node.op == Op::kEndsWith
      || node.op == Op::kContains) {
    const auto& first = std::get<std::string>(*lhs);
    const auto& second = std::get<std::string>(*rhs);
    if (node.op == Op::kStartsWith) {
      return Value(first.starts_with(second));
    }
    if (node.op == Op::kEndsWith) {
      return Value(first.ends_with(second));
    }
    if (node.op == Op::kContains) {
      return Value(first.find(second) != std::string::npos);
    }
    if (first.size() + second.size() > 4096) {
      return invalid("expression string result exceeds limit");
    }
    return Value(first + second);
  }
  const auto first = std::get<std::int64_t>(*lhs);
  const auto second = std::get<std::int64_t>(*rhs);
  std::int64_t result = 0;
  bool overflow = false;
  if (node.op == Op::kAdd) {
    overflow = __builtin_add_overflow(first, second, &result);
  } else if (node.op == Op::kSubtract) {
    overflow = __builtin_sub_overflow(first, second, &result);
  } else if (node.op == Op::kMultiply) {
    overflow = __builtin_mul_overflow(first, second, &result);
  } else {
    if (second == 0 || (first == std::numeric_limits<std::int64_t>::min() && second == -1)) {
      return invalid("invalid integer division");
    }
    result = node.op == Op::kDivide ? first / second : first % second;
  }
  return overflow ? invalid("integer overflow") : Evaluated(Value(result));
}

using Bounds = std::pair<std::int64_t, std::int64_t>;

std::optional<Bounds> bounds(const std::vector<Node>& nodes, std::size_t index) {
  const auto& node = nodes[index];
  if (node.type != ValueType::kInt) {
    return {};
  }
  if (node.op == Op::kLiteral) {
    const auto value = std::get<std::int64_t>(node.value);
    return Bounds{value, value};
  }
  if (node.op == Op::kChoose) {
    const auto yes = bounds(nodes, node.right);
    const auto no = bounds(nodes, node.third);
    if (!yes || !no) {
      return {};
    }
    return Bounds{std::min(yes->first, no->first), std::max(yes->second, no->second)};
  }
  const auto lhs = bounds(nodes, node.left);
  if (!lhs) {
    return {};
  }
  if (node.op == Op::kNegate) {
    if (lhs->first == std::numeric_limits<std::int64_t>::min()) {
      return {};
    }
    return Bounds{-lhs->second, -lhs->first};
  }
  const auto rhs = bounds(nodes, node.right);
  if (!rhs) {
    return {};
  }
  Bounds result{std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min()};
  for (const auto first : {lhs->first, lhs->second}) {
    for (const auto second : {rhs->first, rhs->second}) {
      std::int64_t value = 0;
      bool overflow = false;
      if (node.op == Op::kAdd) {
        overflow = __builtin_add_overflow(first, second, &value);
      } else if (node.op == Op::kSubtract) {
        overflow = __builtin_sub_overflow(first, second, &value);
      } else if (node.op == Op::kMultiply) {
        overflow = __builtin_mul_overflow(first, second, &value);
      } else if (node.op == Op::kDivide || node.op == Op::kModulo) {
        if (rhs->first <= 0 && rhs->second >= 0) {
          return {};
        }
        if (first == std::numeric_limits<std::int64_t>::min() && second == -1) {
          return {};
        }
        if (node.op == Op::kModulo && (lhs->first != lhs->second || rhs->first != rhs->second)) {
          return {};
        }
        value = node.op == Op::kDivide ? first / second : first % second;
      } else {
        return {};
      }
      if (overflow) {
        return {};
      }
      result.first = std::min(result.first, value);
      result.second = std::max(result.second, value);
    }
  }
  return result;
}
}  // namespace

struct Expression::Program {
  std::vector<Node> nodes;
};

std::expected<Expression, std::string> Expression::compile(std::string_view source, ValueType expected,
                                                           const ExpressionSchema& schema) {
  if (source.empty() || source.size() > 4000) {
    return std::unexpected("expression must contain 1..4000 bytes");
  }
  auto nodes = Parser(source, schema).parse(expected);
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  return Expression(std::make_shared<Program>(Program{.nodes = std::move(*nodes)}));
}

std::expected<Value, std::string> Expression::evaluate(const Resolve& resolve, std::size_t budget) const {
  if (!resolve) {
    return std::unexpected("expression resolver required");
  }
  return processing::evaluate(program_->nodes, program_->nodes.size() - 1, resolve, budget);
}

std::size_t Expression::cost() const {
  return program_->nodes.size() - 1;
}

std::optional<std::pair<std::int64_t, std::int64_t>> Expression::integer_bounds() const {
  return bounds(program_->nodes, program_->nodes.size() - 1);
}

std::set<std::string> Expression::fields() const {
  std::set<std::string> result;
  for (const auto& node : program_->nodes) {
    if (node.op == Op::kField) {
      result.insert(node.field);
    }
  }
  return result;
}
}  // namespace psim::domain::processing
