// SPDX-License-Identifier: MIT
/**
 * @file json_test.cpp
 * @brief Catch2 tests for the dependency-free JSON reader and writer.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

/**
 * @brief Verify every JSON value form is read with the right type and content.
 */
TEST_CASE("json reads all value forms", "[results][json]") {
  using ztick::ConfigValue;
  using ztick::ConfigValueType;

  const std::string_view text =
      R"({"zero":0,"neg":-7,"real":2.5,"exp":1e2,"yes":true,"no":false,"nil":null,)"
      R"("text":"value","array":[1,2],"object":{"nested":3}})";
  const auto result = ztick::json_read(text);
  REQUIRE(result.valid());
  REQUIRE(result.value.type() == ConfigValueType::object);

  const ConfigValue *zero = result.value.find("zero");
  REQUIRE(zero != nullptr);
  REQUIRE(zero->type() == ConfigValueType::integer);
  REQUIRE(*(zero->as_integer()) == 0);

  const ConfigValue *neg = result.value.find("neg");
  REQUIRE(neg != nullptr);
  REQUIRE(*(neg->as_integer()) == -7);

  const ConfigValue *real = result.value.find("real");
  REQUIRE(real != nullptr);
  REQUIRE(real->type() == ConfigValueType::real);
  REQUIRE(*(real->as_real()) == 2.5);

  const ConfigValue *exp = result.value.find("exp");
  REQUIRE(exp != nullptr);
  REQUIRE(exp->type() == ConfigValueType::real);
  REQUIRE(*(exp->as_real()) == 100.0);

  const ConfigValue *yes = result.value.find("yes");
  REQUIRE(yes != nullptr);
  REQUIRE(*(yes->as_boolean()) == true);

  const ConfigValue *no = result.value.find("no");
  REQUIRE(no != nullptr);
  REQUIRE(*(no->as_boolean()) == false);

  const ConfigValue *nil = result.value.find("nil");
  REQUIRE(nil != nullptr);
  REQUIRE(nil->type() == ConfigValueType::null);

  const ConfigValue *text_value = result.value.find("text");
  REQUIRE(text_value != nullptr);
  REQUIRE(*(text_value->as_string()) == "value");

  const ConfigValue *array = result.value.find("array");
  REQUIRE(array != nullptr);
  REQUIRE(array->as_array() != nullptr);
  REQUIRE(array->as_array()->size() == 2);

  const ConfigValue *object = result.value.find("object");
  REQUIRE(object != nullptr);
  REQUIRE(object->as_object() != nullptr);
  REQUIRE(*(object->find("nested")->as_integer()) == 3);
}

/**
 * @brief Verify string escapes, surrogate pairs and write/read string equality.
 */
TEST_CASE("json handles escapes and Unicode", "[results][json]") {
  const std::string_view encoded = R"("line\nquote\"slash\/tab\tunicode\u00e9pair\uD83D\uDE00")";
  const std::string expected = "line\nquote\"slash/tab\tunicode\xC3\xA9"
                               "pair\xF0\x9F\x98\x80";

  const auto result = ztick::json_read(encoded);
  REQUIRE(result.valid());
  REQUIRE(result.value.type() == ztick::ConfigValueType::string);
  REQUIRE(*(result.value.as_string()) == expected);

  const auto written = ztick::json_write(result.value);
  REQUIRE(written.valid());
  const auto reparsed = ztick::json_read(written.text);
  REQUIRE(reparsed.valid());
  REQUIRE(reparsed.value == result.value);
}

/**
 * @brief Verify malformed documents are rejected with a non-empty explanation.
 */
TEST_CASE("json rejects malformed input", "[results][json]") {
  const std::array<std::string_view, 12> invalid{{
      "",
      "{",
      "[1,]",
      "{\"a\" 1}",
      "01",
      "1.",
      "1e",
      "truex",
      "\"abc",
      "\"\\q\"",
      "\"\\uD800\"",
      "{\"a\":1} trailing",
  }};
  for (const std::string_view text : invalid) {
    const auto result = ztick::json_read(text);
    REQUIRE_FALSE(result.valid());
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(result.value.type() == ztick::ConfigValueType::null);
  }
}

/**
 * @brief Verify writer escaping, integer/real preservation and round-trip equality.
 */
TEST_CASE("json writes deterministic compact documents", "[results][json]") {
  ztick::ConfigValue value = ztick::ConfigValue::object();
  REQUIRE(value.set("integer", ztick::ConfigValue::integer(1)));
  REQUIRE(value.set("real", ztick::ConfigValue::real(1.0)));
  REQUIRE(value.set("control", ztick::ConfigValue::string(std::string(1, '\x01'))));

  const auto written = ztick::json_write(value);
  REQUIRE(written.valid());
  REQUIRE(written.text.find("\"integer\":1") != std::string::npos);
  REQUIRE(written.text.find("\"real\":1.0") != std::string::npos);
  REQUIRE(written.text.find("\\u0001") != std::string::npos);

  const auto reparsed = ztick::json_read(written.text);
  REQUIRE(reparsed.valid());
  REQUIRE(reparsed.value == value);
  REQUIRE(reparsed.value.find("real")->type() == ztick::ConfigValueType::real);
}

/**
 * @brief Verify non-finite reals cannot be serialized into JSON.
 */
TEST_CASE("json rejects non-finite real values", "[results][json]") {
  const auto infinity =
      ztick::json_write(ztick::ConfigValue::real(std::numeric_limits<double>::infinity()));
  REQUIRE_FALSE(infinity.valid());
  REQUIRE_FALSE(infinity.error.empty());

  const auto nan =
      ztick::json_write(ztick::ConfigValue::real(std::numeric_limits<double>::quiet_NaN()));
  REQUIRE_FALSE(nan.valid());
  REQUIRE_FALSE(nan.error.empty());
}

/**
 * @brief Verify oversized integer literals survive as real values.
 */
TEST_CASE("json keeps oversized integers as reals", "[results][json]") {
  const auto result = ztick::json_read("9223372036854775808");
  REQUIRE(result.valid());
  REQUIRE(result.value.type() == ztick::ConfigValueType::real);
  REQUIRE(std::isfinite(*(result.value.as_real())));
}
