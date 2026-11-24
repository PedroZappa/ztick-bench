// SPDX-License-Identifier: MIT
/**
 * @file profile.cpp
 * @brief Profile model conversions between C++ objects and config-shaped data.
 */

#include "ztick/profile.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztick {

namespace {

/**
 * @brief Store a diagnostic in an optional error receiver.
 * @param error Receiver to write, or `nullptr` to ignore the diagnostic.
 * @param message Explanation to store.
 */
void record_error(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

/**
 * @brief Require an object-valued section of the config document.
 * @param data Root config-shaped object.
 * @param key Section name.
 * @param section Receives the section value on success.
 * @param error Optional diagnostic receiver.
 * @return True when the section exists and is an object.
 */
bool require_section(const ConfigValue &data, std::string_view key, const ConfigValue *&section,
                     std::string *error) {
  section = data.find(key);
  if (section == nullptr) {
    record_error(error, "config section is missing: " + std::string{key});
    return false;
  }
  if (section->type() != ConfigValueType::object) {
    record_error(error, "config section must be an object: " + std::string{key});
    return false;
  }
  return true;
}

/**
 * @brief Require a string-valued member of a config section.
 * @param section Config-shaped object to read.
 * @param key Member name.
 * @param label Diagnostic label for the member.
 * @param value Receives the string on success.
 * @param error Optional diagnostic receiver.
 * @return True when the member exists and is a string.
 */
bool require_string(const ConfigValue &section, std::string_view key, const char *label,
                    std::string &value, std::string *error) {
  const ConfigValue *member = section.find(key);
  if (member == nullptr) {
    record_error(error, std::string{label} + " is required");
    return false;
  }
  const auto parsed = member->as_string();
  if (!parsed.has_value()) {
    record_error(error, std::string{label} + " must be a string");
    return false;
  }
  value = *parsed;
  return true;
}

/**
 * @brief Require a non-negative integer member of a config section.
 * @param section Config-shaped object to read.
 * @param key Member name.
 * @param label Diagnostic label for the member.
 * @param value Receives the integer on success.
 * @param error Optional diagnostic receiver.
 * @return True when the member exists, is an integer and is non-negative.
 */
bool require_nonnegative_integer(const ConfigValue &section, std::string_view key,
                                 const char *label, std::int64_t &value, std::string *error) {
  const ConfigValue *member = section.find(key);
  if (member == nullptr) {
    record_error(error, std::string{label} + " is required");
    return false;
  }
  const auto parsed = member->as_integer();
  if (!parsed.has_value() || *parsed < 0) {
    record_error(error, std::string{label} + " must be a non-negative integer");
    return false;
  }
  value = *parsed;
  return true;
}

/**
 * @brief Require a non-negative real member of a config section.
 * @param section Config-shaped object to read.
 * @param key Member name.
 * @param label Diagnostic label for the member.
 * @param value Receives the real number on success.
 * @param error Optional diagnostic receiver.
 * @return True when the member exists, is real and is non-negative.
 */
bool require_nonnegative_real(const ConfigValue &section, std::string_view key, const char *label,
                              double &value, std::string *error) {
  const ConfigValue *member = section.find(key);
  if (member == nullptr) {
    record_error(error, std::string{label} + " is required");
    return false;
  }
  const auto parsed = member->as_real();
  if (!parsed.has_value() || !(*parsed >= 0.0)) {
    record_error(error, std::string{label} + " must be a non-negative real number");
    return false;
  }
  value = *parsed;
  return true;
}

/**
 * @brief Require a boolean member of a config section.
 * @param section Config-shaped object to read.
 * @param key Member name.
 * @param label Diagnostic label for the member.
 * @param value Receives the boolean on success.
 * @param error Optional diagnostic receiver.
 * @return True when the member exists and is a boolean.
 */
bool require_boolean(const ConfigValue &section, std::string_view key, const char *label,
                     bool &value, std::string *error) {
  const ConfigValue *member = section.find(key);
  if (member == nullptr) {
    record_error(error, std::string{label} + " is required");
    return false;
  }
  const auto parsed = member->as_boolean();
  if (!parsed.has_value()) {
    record_error(error, std::string{label} + " must be a boolean");
    return false;
  }
  value = *parsed;
  return true;
}

/**
 * @brief Read an optional list of strings from a config section.
 * @param section Config-shaped object to read.
 * @param key Member name.
 * @param label Diagnostic label for the member.
 * @param values Receives the list; left unchanged when the member is absent.
 * @param error Optional diagnostic receiver.
 * @return True when absent or a valid array of strings.
 */
bool read_optional_string_list(const ConfigValue &section, std::string_view key, const char *label,
                               std::vector<std::string> &values, std::string *error) {
  const ConfigValue *member = section.find(key);
  if (member == nullptr) {
    return true;
  }
  const auto *elements = member->as_array();
  if (elements == nullptr) {
    record_error(error, std::string{label} + " must be an array");
    return false;
  }
  std::vector<std::string> parsed;
  parsed.reserve(elements->size());
  for (const auto &element : *elements) {
    const auto text = element.as_string();
    if (!text.has_value()) {
      record_error(error, std::string{label} + " entries must be strings");
      return false;
    }
    parsed.push_back(*text);
  }
  values = std::move(parsed);
  return true;
}

/**
 * @brief Render a list of strings as a config array.
 * @param values Strings to render.
 * @return Array value holding one string element per input value.
 */
ConfigValue string_list_to_config(const std::vector<std::string> &values) {
  ConfigValue::Array elements;
  elements.reserve(values.size());
  std::transform(values.begin(), values.end(), std::back_inserter(elements),
                 [](const std::string &value) { return ConfigValue::string(value); });
  return ConfigValue::array(std::move(elements));
}

} // namespace

std::optional<DurationMode> parse_duration_mode(std::string_view token) noexcept {
  if (token == "iterations") {
    return DurationMode::iterations;
  }
  if (token == "duration") {
    return DurationMode::duration;
  }
  return std::nullopt;
}

std::string_view duration_mode_name(DurationMode mode) noexcept {
  switch (mode) {
  case DurationMode::iterations:
    return "iterations";
  case DurationMode::duration:
    return "duration";
  }
  return "unknown";
}

std::optional<MeasurementDetail> parse_measurement_detail(std::string_view token) noexcept {
  if (token == "aggregate") {
    return MeasurementDetail::aggregate;
  }
  if (token == "per_block") {
    return MeasurementDetail::per_block;
  }
  return std::nullopt;
}

std::string_view measurement_detail_name(MeasurementDetail detail) noexcept {
  switch (detail) {
  case MeasurementDetail::aggregate:
    return "aggregate";
  case MeasurementDetail::per_block:
    return "per_block";
  }
  return "unknown";
}

std::optional<ValidationLevel> parse_validation_level(std::string_view token) noexcept {
  if (token == "Basic") {
    return ValidationLevel::basic;
  }
  if (token == "Standard") {
    return ValidationLevel::standard;
  }
  if (token == "Strict") {
    return ValidationLevel::strict;
  }
  return std::nullopt;
}

std::string_view validation_level_name(ValidationLevel level) noexcept {
  switch (level) {
  case ValidationLevel::basic:
    return "Basic";
  case ValidationLevel::standard:
    return "Standard";
  case ValidationLevel::strict:
    return "Strict";
  }
  return "unknown";
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ConfigValue profile_to_config(const Profile &profile) {
  ConfigValue document = ConfigValue::object();

  ConfigValue profile_section = ConfigValue::object();
  profile_section.set("name", ConfigValue::string(profile.name));
  profile_section.set("coverage", string_list_to_config(profile.coverage));
  profile_section.set("reliability_checks", string_list_to_config(profile.reliability_checks));
  document.set("profile", std::move(profile_section));

  ConfigValue warmup = ConfigValue::object();
  warmup.set("iterations",
             ConfigValue::integer(static_cast<std::int64_t>(profile.warmup_iterations)));
  ConfigValue execution = ConfigValue::object();
  execution.set("duration_mode",
                ConfigValue::string(std::string{duration_mode_name(profile.duration_mode)}));
  execution.set("iterations", ConfigValue::integer(static_cast<std::int64_t>(profile.iterations)));
  execution.set("duration_seconds", ConfigValue::real(profile.duration_seconds));
  execution.set("repetitions",
                ConfigValue::integer(static_cast<std::int64_t>(profile.repetitions)));
  execution.set("warmup", std::move(warmup));
  document.set("execution", std::move(execution));

  ConfigValue measurement = ConfigValue::object();
  measurement.set("detail",
                  ConfigValue::string(std::string{measurement_detail_name(profile.detail)}));
  measurement.set("end_to_end", ConfigValue::boolean(profile.end_to_end));
  measurement.set("timing_levels", string_list_to_config(profile.timing_levels));
  document.set("measurement", std::move(measurement));

  ConfigValue validation = ConfigValue::object();
  validation.set("level",
                 ConfigValue::string(std::string{validation_level_name(profile.validation_level)}));
  document.set("validation", std::move(validation));

  return document;
}

std::optional<Profile> profile_from_config(const ConfigValue &data, std::string *error) {
  if (data.type() != ConfigValueType::object) {
    record_error(error, "config document must be an object");
    return std::nullopt;
  }

  const ConfigValue *profile_section = nullptr;
  const ConfigValue *execution_section = nullptr;
  const ConfigValue *measurement_section = nullptr;
  const ConfigValue *validation_section = nullptr;
  if (!require_section(data, "profile", profile_section, error) ||
      !require_section(data, "execution", execution_section, error) ||
      !require_section(data, "measurement", measurement_section, error) ||
      !require_section(data, "validation", validation_section, error)) {
    return std::nullopt;
  }

  Profile profile;
  if (!require_string(*profile_section, "name", "profile.name", profile.name, error) ||
      !read_optional_string_list(*profile_section, "coverage", "profile.coverage", profile.coverage,
                                 error) ||
      !read_optional_string_list(*profile_section, "reliability_checks",
                                 "profile.reliability_checks", profile.reliability_checks, error)) {
    return std::nullopt;
  }

  std::string mode_token;
  std::string detail_token;
  std::string level_token;
  std::int64_t iterations = 0;
  std::int64_t repetitions = 0;
  std::int64_t warmup_iterations = 0;
  if (!require_string(*execution_section, "duration_mode", "execution.duration_mode", mode_token,
                      error) ||
      !require_nonnegative_integer(*execution_section, "iterations", "execution.iterations",
                                   iterations, error) ||
      !require_nonnegative_real(*execution_section, "duration_seconds",
                                "execution.duration_seconds", profile.duration_seconds, error) ||
      !require_nonnegative_integer(*execution_section, "repetitions", "execution.repetitions",
                                   repetitions, error)) {
    return std::nullopt;
  }
  const auto duration_mode = parse_duration_mode(mode_token);
  if (duration_mode == std::nullopt) {
    record_error(error, "execution.duration_mode has an unknown value");
    return std::nullopt;
  }
  profile.duration_mode = *duration_mode;
  profile.iterations = static_cast<std::uint64_t>(iterations);
  constexpr auto kUint32Max = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
  if (repetitions > kUint32Max) {
    record_error(error, "execution.repetitions is out of range");
    return std::nullopt;
  }
  profile.repetitions = static_cast<std::uint32_t>(repetitions);

  const ConfigValue *warmup = execution_section->find("warmup");
  if (warmup != nullptr) {
    if (warmup->type() != ConfigValueType::object) {
      record_error(error, "execution.warmup must be an object");
      return std::nullopt;
    }
    const ConfigValue *warmup_iterations_value = warmup->find("iterations");
    if (warmup_iterations_value != nullptr) {
      const auto parsed = warmup_iterations_value->as_integer();
      if (!parsed.has_value() || *parsed < 0 || *parsed > kUint32Max) {
        record_error(error, "execution.warmup.iterations must be a non-negative 32-bit integer");
        return std::nullopt;
      }
      warmup_iterations = *parsed;
    }
  }
  profile.warmup_iterations = static_cast<std::uint32_t>(warmup_iterations);

  if (!require_string(*measurement_section, "detail", "measurement.detail", detail_token, error) ||
      !require_boolean(*measurement_section, "end_to_end", "measurement.end_to_end",
                       profile.end_to_end, error) ||
      !read_optional_string_list(*measurement_section, "timing_levels", "measurement.timing_levels",
                                 profile.timing_levels, error) ||
      !require_string(*validation_section, "level", "validation.level", level_token, error)) {
    return std::nullopt;
  }

  const auto detail = parse_measurement_detail(detail_token);
  if (detail == std::nullopt) {
    record_error(error, "measurement.detail has an unknown value");
    return std::nullopt;
  }
  profile.detail = *detail;

  const auto validation_level = parse_validation_level(level_token);
  if (validation_level == std::nullopt) {
    record_error(error, "validation.level has an unknown value");
    return std::nullopt;
  }
  profile.validation_level = *validation_level;

  return profile;
}

} // namespace ztick
