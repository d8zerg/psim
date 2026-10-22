#include "psim/domain/processing/mapping.hpp"

#include <gtest/gtest.h>

namespace psim::domain::processing {
TEST(MappingCondition, DecodesEscapesAndRejectsMalformedStrings) {
  auto value = mapping_condition(R"("\u0041" == "A")");
  ASSERT_TRUE(value);
  EXPECT_TRUE(*value);
  value = mapping_condition(R"("\ud83d\ude00" == "\ud83d\ude00")");
  ASSERT_TRUE(value);
  EXPECT_TRUE(*value);
  for (const auto* input :
       {R"("\u00e9" == "é")", R"("\u20ac" == "€")", R"("\ud83d\ude00" == "😀")", "true == !false", "true != false"}) {
    value = mapping_condition(input);
    ASSERT_TRUE(value) << input;
    EXPECT_TRUE(*value) << input;
  }
  value = mapping_condition(R"("\n\r\t\b\f\\\/\"" == "\n\r\t\b\f\\\/\"")");
  ASSERT_TRUE(value);
  EXPECT_TRUE(*value);
  for (const auto* input :
       {R"("\uD800" == "x")", R"("\uDC00" == "x")", R"("\uXX00" == "x")", R"("\q" == "x")", R"("unterminated)",
        R"("\u00" == "x")", R"("\ud800\u0041" == "x")", R"("\ud800\u00" == "x")", "\"raw\ncontrol\" == \"x\"",
        "\"trailing\\", "has(true", "true && \"text\"", "raw.raw_code.contains(\"x\"", "true || \"text\""}) {
    EXPECT_FALSE(mapping_condition(input)) << input;
  }
}
}  // namespace psim::domain::processing
