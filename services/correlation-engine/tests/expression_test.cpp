#include "psim/domain/processing/expression.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace psim::domain::processing {
namespace {
ExpressionSchema schema() {
  return ExpressionSchema{{"event.severity", ValueType::kInt},
                          {"event.type", ValueType::kString},
                          {"event.attributes.*", ValueType::kString}};
}

std::optional<Value> resolve(std::string_view key) {
  if (key == "event.severity") {
    return std::int64_t{4};
  }
  if (key == "event.type") {
    return std::string("fire.detector.alarm");
  }
  return {};
}

TEST(RuleExpression, TypesOperatorsMethodsAndShortCircuit) {
  for (const auto* source :
       {"event.severity >= 3 && event.type.startsWith('fire.')", "!false || event.attributes['missing'] == 'x'",
        "!has(event.attributes['missing'])", "event.type.endsWith('alarm')", "event.type.contains('detector')",
        "(1 + 2 * 3) == 7", "7 / 2 == 3 && 7 % 2 == 1", "4 - 2 == 2", "-event.severity == -4", "false ? false : true",
        "1 != 2 && 1 < 2 && 2 <= 2 && 2 > 1", "'a' < 'b'", R"('a\n' == "a\n")"}) {
    auto compiled = Expression::compile(source, ValueType::kBool, schema());
    ASSERT_TRUE(compiled) << source;
    auto value = compiled->evaluate(resolve);
    ASSERT_TRUE(value) << source;
    EXPECT_EQ(*value, Value(true));
  }
  auto priority = Expression::compile("event.severity >= 4 ? 1 : 2", ValueType::kInt, schema());
  ASSERT_TRUE(priority);
  EXPECT_EQ(priority->evaluate(resolve).value(), Value(std::int64_t{1}));
  auto summary = Expression::compile("'Alarm: ' + event.type", ValueType::kString, schema());
  ASSERT_TRUE(summary);
  EXPECT_EQ(summary->evaluate(resolve).value(), Value(std::string("Alarm: fire.detector.alarm")));
}

TEST(RuleExpression, RejectsSyntaxTypeAndResourceErrors) {
  for (const auto* source : {"",
                             "true ''",
                             "event.unknown == 'x'",
                             "event.severity == '4'",
                             "event.type + 1",
                             "event.severity.matches('x')",
                             "event.type.matches('x')",
                             "event.type[1] == 'x'",
                             "has(true)",
                             "1 && 2",
                             "!3",
                             "-'x'",
                             "true < false",
                             "'a' - 'b'",
                             "true ? 1 : false",
                             "(true",
                             "'unterminated",
                             "'a\\u0001' == 'a'",
                             "9223372036854775808 == 1",
                             "-9223372036854775809 == 1",
                             "unknown(1)",
                             "true; false"}) {
    EXPECT_FALSE(Expression::compile(source, ValueType::kBool, schema())) << source;
  }
  EXPECT_FALSE(Expression::compile(std::string(4001, 'a'), ValueType::kBool, schema()));
  EXPECT_FALSE(Expression::compile(std::string(100, '!') + "true", ValueType::kBool, schema()));
  EXPECT_FALSE(Expression::compile(std::string(100, '(') + "true" + std::string(100, ')'), ValueType::kBool, schema()));
  std::string chain = "1";
  for (int i = 0; i < 40; ++i) {
    chain += " + 1";
  }
  EXPECT_FALSE(Expression::compile(chain, ValueType::kInt, schema()));
  std::string balanced = "1";
  for (int level = 0; level < 9; ++level) {
    const auto previous = balanced;
    balanced = "(";
    balanced.append(previous).append("+").append(previous).append(")");
  }
  ASSERT_LT(balanced.size(), 4000);
  EXPECT_FALSE(Expression::compile(balanced, ValueType::kInt, schema()));
}

TEST(RuleExpression, EvaluationFailsWithoutUndefinedBehavior) {
  for (const auto* source : {"1 / 0", "1 % 0", "9223372036854775807 + 1", "-9223372036854775808 - 1",
                             "9223372036854775807 * 2", "-9223372036854775808 / -1", "-(-9223372036854775808)"}) {
    auto compiled = Expression::compile(source, ValueType::kInt, schema());
    ASSERT_TRUE(compiled) << source;
    EXPECT_FALSE(compiled->evaluate(resolve)) << source;
  }
  auto missing = Expression::compile("event.attributes['absent']", ValueType::kString, schema());
  ASSERT_TRUE(missing);
  EXPECT_FALSE(missing->evaluate(resolve));
  auto bounded = Expression::compile("event.type + event.type", ValueType::kString, schema());
  ASSERT_TRUE(bounded);
  EXPECT_FALSE(bounded->evaluate(resolve, 1));
  EXPECT_FALSE(bounded->evaluate({}));
  EXPECT_FALSE(bounded->evaluate([](std::string_view) -> std::optional<Value> { return std::int64_t{1}; }));
  EXPECT_FALSE(bounded->evaluate([](std::string_view) -> std::optional<Value> { return std::string(5000, 'x'); }));
  EXPECT_FALSE(bounded->evaluate([](std::string_view) -> std::optional<Value> { return std::string(3000, 'x'); }));
  EXPECT_FALSE(bounded->evaluate(resolve, 3));
}

TEST(RuleExpression, InfersPriorityBoundsAndReferencedFields) {
  for (const auto* source : {"1", "true ? 1 : 4", "(true ? 1 : 2) * 2", "4 / 2", "5 % 3", "-(-2)", "3 - 1"}) {
    auto compiled = Expression::compile(source, ValueType::kInt, schema());
    ASSERT_TRUE(compiled) << source;
    auto bounds = compiled->integer_bounds();
    if (bounds) {
      EXPECT_GE(bounds->first, 1);
      EXPECT_LE(bounds->second, 4);
    } else {
      FAIL() << source;
    }
  }
  for (const auto* source : {"event.severity", "1 / 0", "1 / (true ? -1 : 1)", "9223372036854775807 + 1",
                             "-(-9223372036854775808)", "-9223372036854775808 / -1", "(true ? 1 : 4) % 2"}) {
    auto compiled = Expression::compile(source, ValueType::kInt, schema());
    ASSERT_TRUE(compiled) << source;
    EXPECT_FALSE(compiled->integer_bounds()) << source;
  }
  auto compiled =
      Expression::compile("event.severity > 1 && has(event.attributes['status'])", ValueType::kBool, schema());
  ASSERT_TRUE(compiled);
  EXPECT_EQ(compiled->fields(), (std::set<std::string>{"event.severity", "event.attributes.status"}));
  EXPECT_FALSE(compiled->integer_bounds());
  EXPECT_GT(compiled->cost(), 1);
}

TEST(RuleExpression, ChargesStringSearchAndComparisonToTheBudget) {
  auto search = Expression::compile("event.type.contains(event.type)", ValueType::kBool, schema());
  ASSERT_TRUE(search);
  EXPECT_FALSE(search->evaluate([](std::string_view) -> std::optional<Value> { return std::string(4096, 'x'); }));
  auto comparison = Expression::compile("event.type == event.type", ValueType::kBool, schema());
  ASSERT_TRUE(comparison);
  EXPECT_FALSE(comparison->evaluate(resolve, 3));
  EXPECT_EQ(comparison->evaluate(resolve).value(), Value(true));
}
}  // namespace
}  // namespace psim::domain::processing
