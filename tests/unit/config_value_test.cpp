// SPDX-License-Identifier: MIT
/**
 * @file config_value_test.cpp
 * @brief Catch2 unit tests for the config-shaped value tree used by core models.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/config_value.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

/**
 * @brief Verify every factory produces the expected discriminant and value.
 * @note Expected tokens are literals so a changed mapping fails the suite.
 */
TEST_CASE("config value factories preserve type and scalar", "[core][config]") {
  using ztick::ConfigValue;
  using ztick::ConfigValueType;

  REQUIRE(ConfigValue::null().type() == ConfigValueType::null);
  REQUIRE(ConfigValue::boolean(true).type() == ConfigValueType::boolean);
  REQUIRE(ConfigValue::integer(42).type() == ConfigValueType::integer);
  REQUIRE(ConfigValue::real(1.5).type() == ConfigValueType::real);
  REQUIRE(ConfigValue::string("text").type() == ConfigValueType::string);
  REQUIRE(ConfigValue::object().type() == ConfigValueType::object);
  REQUIRE(ConfigValue::array().type() == ConfigValueType::array);

  REQUIRE(ConfigValue::boolean(true).as_boolean() == true);
  REQUIRE(ConfigValue::integer(-7).as_integer() == std::int64_t{-7});
  REQUIRE(ConfigValue::real(2.25).as_real() == 2.25);
  REQUIRE(ConfigValue::string("abc").as_string() == std::string{"abc"});

  ConfigValue::Array elements;
  elements.push_back(ConfigValue::integer(1));
  elements.push_back(ConfigValue::integer(2));
  const ConfigValue from_elements = ConfigValue::array(elements);
  REQUIRE(from_elements.type() == ConfigValueType::array);
  REQUIRE(from_elements.as_array()->size() == 2);
  REQUIRE(from_elements.as_array()->at(1).as_integer() == std::int64_t{2});
}

/**
 * @brief Verify typed accessors reject mismatched discriminants.
 */
TEST_CASE("config value accessors reject other types", "[core][config]") {
  const ztick::ConfigValue value = ztick::ConfigValue::integer(9);

  REQUIRE_FALSE(value.as_boolean().has_value());
  REQUIRE(value.as_integer().has_value());
  REQUIRE_FALSE(value.as_real().has_value());
  REQUIRE_FALSE(value.as_string().has_value());
  REQUIRE(value.as_object() == nullptr);
  REQUIRE(value.as_array() == nullptr);

  REQUIRE_FALSE(ztick::ConfigValue::null().as_integer().has_value());
  REQUIRE_FALSE(ztick::ConfigValue::boolean(false).as_string().has_value());
  REQUIRE(ztick::ConfigValue::object().as_object() != nullptr);
  REQUIRE(ztick::ConfigValue::array().as_array() != nullptr);
}

/**
 * @brief Verify object members can be inserted and replaced without duplication.
 */
TEST_CASE("config value object members are addressable", "[core][config]") {
  ztick::ConfigValue object = ztick::ConfigValue::object();

  REQUIRE(object.set("level", ztick::ConfigValue::string("processor")));
  REQUIRE(object.find("level") != nullptr);
  REQUIRE(object.find("level")->as_string() == std::string{"processor"});

  REQUIRE(object.set("level", ztick::ConfigValue::string("chain")));
  REQUIRE(object.find("level")->as_string() == std::string{"chain"});
  REQUIRE(object.as_object()->size() == 1);
  REQUIRE(object.find("missing") == nullptr);

  REQUIRE_FALSE(ztick::ConfigValue::array().set("k", ztick::ConfigValue::null()));
  REQUIRE(ztick::ConfigValue::array().find("k") == nullptr);
}

/**
 * @brief Verify mutable accessors allow in-place edits of nested config data.
 */
TEST_CASE("config value mutable accessors edit nested data", "[core][config]") {
  ztick::ConfigValue object = ztick::ConfigValue::object();
  object.set("count", ztick::ConfigValue::integer(1));
  ztick::ConfigValue items = ztick::ConfigValue::array();
  items.push_back(ztick::ConfigValue::string("old"));
  object.set("items", std::move(items));

  REQUIRE(object.find("count") != nullptr);
  *(object.find("count")) = ztick::ConfigValue::integer(2);
  REQUIRE(object.find("count")->as_integer() == std::int64_t{2});

  ztick::ConfigValue *mutable_items = object.find("items");
  REQUIRE(mutable_items != nullptr);
  REQUIRE(mutable_items->as_array() != nullptr);
  (*(mutable_items->as_array()))[0] = ztick::ConfigValue::string("new");
  REQUIRE(object.find("items")->as_array()->at(0).as_string() == std::string{"new"});
  REQUIRE(object.as_object()->size() == 2);

  REQUIRE(ztick::ConfigValue::array().as_object() == nullptr);
  REQUIRE(ztick::ConfigValue::object().as_array() == nullptr);
  REQUIRE(ztick::ConfigValue::array().find("key") == nullptr);
}

/**
 * @brief Verify array elements are appended in insertion order.
 */
TEST_CASE("config value arrays keep insertion order", "[core][config]") {
  ztick::ConfigValue array = ztick::ConfigValue::array();

  REQUIRE(array.push_back(ztick::ConfigValue::string("first")));
  REQUIRE(array.push_back(ztick::ConfigValue::string("second")));
  REQUIRE(array.as_array()->size() == 2);
  REQUIRE((*array.as_array())[0].as_string() == std::string{"first"});
  REQUIRE((*array.as_array())[1].as_string() == std::string{"second"});

  REQUIRE_FALSE(ztick::ConfigValue::object().push_back(ztick::ConfigValue::null()));
}

/**
 * @brief Verify equality compares nested config data by value.
 */
TEST_CASE("config values compare by contents", "[core][config]") {
  ztick::ConfigValue first = ztick::ConfigValue::object();
  first.set("n", ztick::ConfigValue::integer(1));
  ztick::ConfigValue items = ztick::ConfigValue::array();
  items.push_back(ztick::ConfigValue::string("a"));
  first.set("items", items);

  ztick::ConfigValue second = ztick::ConfigValue::object();
  second.set("n", ztick::ConfigValue::integer(1));
  ztick::ConfigValue copied_items = ztick::ConfigValue::array();
  copied_items.push_back(ztick::ConfigValue::string("a"));
  second.set("items", copied_items);

  REQUIRE(first == second);
  REQUIRE_FALSE(first != second);

  second.set("n", ztick::ConfigValue::integer(2));
  REQUIRE(first != second);
  REQUIRE_FALSE(first == second);
  REQUIRE(ztick::ConfigValue::integer(1) != ztick::ConfigValue::real(1.0));
}

/**
 * @brief Verify discriminant names are stable, including an unknown value.
 */
TEST_CASE("config value type names are stable", "[core][config]") {
  using ztick::ConfigValueType;

  REQUIRE(ztick::config_value_type_name(ConfigValueType::null) == "null");
  REQUIRE(ztick::config_value_type_name(ConfigValueType::boolean) == "boolean");
  REQUIRE(ztick::config_value_type_name(ConfigValueType::integer) == "integer");
  REQUIRE(ztick::config_value_type_name(ConfigValueType::real) == "real");
  REQUIRE(ztick::config_value_type_name(ConfigValueType::string) == "string");
  REQUIRE(ztick::config_value_type_name(ConfigValueType::object) == "object");
  REQUIRE(ztick::config_value_type_name(ConfigValueType::array) == "array");
  REQUIRE(ztick::config_value_type_name(static_cast<ConfigValueType>(99)) == "unknown");
}
