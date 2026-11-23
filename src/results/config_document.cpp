// SPDX-License-Identifier: MIT
/**
 * @file config_document.cpp
 * @brief Implementation of the portable config document schema checks declared
 *        in @ref config_document.hpp.
 */

#include "ztick/config_document.hpp"

#include <ztick/version.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ztick {

namespace {

/** @brief Required top-level config fields and their JSON value types. */
constexpr std::array<std::pair<std::string_view, ConfigValueType>, 10> kRequiredFields{{
    {"config_version", ConfigValueType::string},
    {"sdk_version", ConfigValueType::string},
    {"integration", ConfigValueType::object},
    {"workload", ConfigValueType::object},
    {"dimensions", ConfigValueType::object},
    {"execution", ConfigValueType::object},
    {"profile", ConfigValueType::object},
    {"measurement", ConfigValueType::object},
    {"validation", ConfigValueType::object},
    {"parallelism", ConfigValueType::object},
}};

} // namespace

std::optional<SemanticVersion> parse_semantic_version(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  std::array<int, 3> parts{0, 0, 0};
  std::size_t position = 0;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    if (position >= text.size() || text[position] < '0' || text[position] > '9') {
      return std::nullopt;
    }
    int value = 0;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
      value = value * 10 + (text[position] - '0');
      if (value > 1000000) {
        return std::nullopt;
      }
      ++position;
    }
    parts[part] = value;
    if (part + 1 < parts.size()) {
      if (position >= text.size() || text[position] != '.') {
        return std::nullopt;
      }
      ++position;
    }
  }
  if (position != text.size()) {
    return std::nullopt;
  }
  return SemanticVersion{parts[0], parts[1], parts[2]};
}

bool sdk_version_supported(const SemanticVersion &version) noexcept {
  return version.major == ZTICK_SDK_VERSION_MAJOR && version.minor <= ZTICK_SDK_VERSION_MINOR;
}

bool validate_config_document(const ConfigValue &document, std::string *error) {
  if (document.type() != ConfigValueType::object) {
    if (error != nullptr) {
      *error = "config document must be an object";
    }
    return false;
  }
  for (const auto &[name, expected_type] : kRequiredFields) {
    const ConfigValue *field = document.find(name);
    if (field == nullptr) {
      if (error != nullptr) {
        *error = "missing config field: " + std::string(name);
      }
      return false;
    }
    if (field->type() != expected_type) {
      if (error != nullptr) {
        *error =
            std::string(name) + " must be " + std::string(config_value_type_name(expected_type));
      }
      return false;
    }
  }

  const std::string schema_version = *(document.find("config_version")->as_string());
  if (schema_version != kConfigSchemaVersion) {
    if (error != nullptr) {
      *error = "unsupported config_version: " + schema_version;
    }
    return false;
  }

  const std::string sdk_version_text = *(document.find("sdk_version")->as_string());
  const std::optional<SemanticVersion> sdk_version = parse_semantic_version(sdk_version_text);
  if (!sdk_version.has_value()) {
    if (error != nullptr) {
      *error = "malformed sdk_version: " + sdk_version_text;
    }
    return false;
  }
  if (!sdk_version_supported(*sdk_version)) {
    if (error != nullptr) {
      *error = "unsupported sdk_version: " + sdk_version_text;
    }
    return false;
  }

  const ConfigValue *integration = document.find("integration");
  for (const std::string_view key : {"id", "version"}) {
    const ConfigValue *field = integration->find(key);
    if (field == nullptr || field->type() != ConfigValueType::string ||
        field->as_string()->empty()) {
      if (error != nullptr) {
        *error = "integration." + std::string(key) + " must be a non-empty string";
      }
      return false;
    }
  }
  return true;
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ConfigParseResult parse_config_document(std::string_view text) {
  ConfigParseResult result;
  const JsonReadResult json = json_read(text);
  if (!json.ok) {
    result.error = json.error;
    result.offset = json.offset;
    return result;
  }
  if (!validate_config_document(json.value, &result.error)) {
    return result;
  }
  result.document = json.value;
  result.offset = json.offset;
  result.ok = true;
  return result;
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ConfigWriteResult write_config_document(const ConfigValue &document) {
  ConfigWriteResult result;
  if (!validate_config_document(document, &result.error)) {
    return result;
  }
  const JsonWriteResult json = json_write(document);
  if (!json.ok) {
    result.error = json.error;
    result.offset = json.offset;
    return result;
  }
  result.text = std::move(json.text);
  result.offset = result.text.size();
  result.ok = true;
  return result;
}

} // namespace ztick
