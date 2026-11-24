// SPDX-License-Identifier: MIT
/**
 * @file config_value.cpp
 * @brief Implementation of the config-shaped value tree declared in
 *        @ref config_value.hpp.
 */

#include "ztick/config_value.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace ztick {

std::string_view config_value_type_name(ConfigValueType type) noexcept {
  switch (type) {
  case ConfigValueType::null:
    return "null";
  case ConfigValueType::boolean:
    return "boolean";
  case ConfigValueType::integer:
    return "integer";
  case ConfigValueType::real:
    return "real";
  case ConfigValueType::string:
    return "string";
  case ConfigValueType::object:
    return "object";
  case ConfigValueType::array:
    return "array";
  }
  return "unknown";
}

ConfigValue::ConfigValue(Storage storage) noexcept : storage_(std::move(storage)) {}

ConfigValue ConfigValue::null() noexcept { return ConfigValue(Storage{}); }

ConfigValue ConfigValue::boolean(bool value) noexcept { return ConfigValue(Storage{value}); }

ConfigValue ConfigValue::integer(std::int64_t value) noexcept {
  return ConfigValue(Storage{value});
}

ConfigValue ConfigValue::real(double value) noexcept { return ConfigValue(Storage{value}); }

ConfigValue ConfigValue::string(std::string value) noexcept {
  return ConfigValue(Storage{std::move(value)});
}

ConfigValue ConfigValue::object() noexcept { return ConfigValue(Storage{Object{}}); }

ConfigValue ConfigValue::array() noexcept { return ConfigValue(Storage{Array{}}); }

ConfigValue ConfigValue::array(Array elements) noexcept {
  return ConfigValue(Storage{std::move(elements)});
}

ConfigValueType ConfigValue::type() const noexcept {
  // The Storage variant alternatives are ordered to match ConfigValueType.
  return static_cast<ConfigValueType>(storage_.index());
}

std::optional<bool> ConfigValue::as_boolean() const noexcept {
  const auto *value = std::get_if<bool>(&storage_);
  return value == nullptr ? std::nullopt : std::optional<bool>{*value};
}

std::optional<std::int64_t> ConfigValue::as_integer() const noexcept {
  const auto *value = std::get_if<std::int64_t>(&storage_);
  return value == nullptr ? std::nullopt : std::optional<std::int64_t>{*value};
}

std::optional<double> ConfigValue::as_real() const noexcept {
  const auto *value = std::get_if<double>(&storage_);
  return value == nullptr ? std::nullopt : std::optional<double>{*value};
}

std::optional<std::string> ConfigValue::as_string() const {
  const auto *value = std::get_if<std::string>(&storage_);
  return value == nullptr ? std::nullopt : std::optional<std::string>{*value};
}

const ConfigValue::Object *ConfigValue::as_object() const noexcept {
  return std::get_if<Object>(&storage_);
}

ConfigValue::Object *ConfigValue::as_object() noexcept { return std::get_if<Object>(&storage_); }

const ConfigValue::Array *ConfigValue::as_array() const noexcept {
  return std::get_if<Array>(&storage_);
}

ConfigValue::Array *ConfigValue::as_array() noexcept { return std::get_if<Array>(&storage_); }

const ConfigValue *ConfigValue::find(std::string_view key) const noexcept {
  const auto *members = as_object();
  if (members == nullptr) {
    return nullptr;
  }
  const auto it = std::find_if(members->begin(), members->end(),
                               [key](const Member &member) { return member.first == key; });
  return it == members->end() ? nullptr : &it->second;
}

ConfigValue *ConfigValue::find(std::string_view key) noexcept {
  auto *members = as_object();
  if (members == nullptr) {
    return nullptr;
  }
  const auto it = std::find_if(members->begin(), members->end(),
                               [key](const Member &member) { return member.first == key; });
  return it == members->end() ? nullptr : &it->second;
}

bool ConfigValue::set(std::string key, ConfigValue value) {
  auto *members = std::get_if<Object>(&storage_);
  if (members == nullptr) {
    return false;
  }
  const auto it = std::find_if(members->begin(), members->end(),
                               [key](const Member &member) { return member.first == key; });
  if (it != members->end()) {
    it->second = std::move(value);
    return true;
  }
  members->emplace_back(std::move(key), std::move(value));
  return true;
}

bool ConfigValue::push_back(ConfigValue value) {
  auto *elements = std::get_if<Array>(&storage_);
  if (elements == nullptr) {
    return false;
  }
  elements->push_back(std::move(value));
  return true;
}

bool operator==(const ConfigValue &lhs, const ConfigValue &rhs) {
  return lhs.storage_ == rhs.storage_;
}

bool operator!=(const ConfigValue &lhs, const ConfigValue &rhs) { return !(lhs == rhs); }

} // namespace ztick
