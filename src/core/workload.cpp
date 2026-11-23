// SPDX-License-Identifier: MIT
/**
 * @file workload.cpp
 * @brief Workload model conversions between C++ objects and config-shaped data.
 */

#include "ztick/workload.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace ztick {

namespace {

/**
 * @brief Attach a diagnostic to an optional error receiver.
 * @param error Receiver to write, or `nullptr` to ignore the diagnostic.
 * @param message Human-readable explanation of the failure.
 */
void record_error(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
}

} // namespace

std::optional<WorkloadLevel> parse_workload_level(std::string_view token) noexcept {
  if (token == "processor") {
    return WorkloadLevel::processor;
  }
  if (token == "chain") {
    return WorkloadLevel::chain;
  }
  if (token == "engine") {
    return WorkloadLevel::engine;
  }
  return std::nullopt;
}

std::string_view workload_level_name(WorkloadLevel level) noexcept {
  switch (level) {
  case WorkloadLevel::processor:
    return "processor";
  case WorkloadLevel::chain:
    return "chain";
  case WorkloadLevel::engine:
    return "engine";
  }
  return "unknown";
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
std::uint64_t required_capability(WorkloadLevel level) noexcept {
  switch (level) {
  case WorkloadLevel::processor:
    return ZTICK_CAPABILITY_PROCESSOR;
  case WorkloadLevel::chain:
    return ZTICK_CAPABILITY_CHAIN;
  case WorkloadLevel::engine:
    return ZTICK_CAPABILITY_ENGINE;
  }
  return 0U;
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ConfigValue workload_to_config(const Workload &workload) {
  ConfigValue data = ConfigValue::object();
  data.set("level", ConfigValue::string(std::string{workload_level_name(workload.level)}));

  ConfigValue::Array identifiers;
  identifiers.reserve(workload.processors.size());
  std::transform(workload.processors.begin(), workload.processors.end(),
                 std::back_inserter(identifiers),
                 [](const std::string &identifier) { return ConfigValue::string(identifier); });
  data.set("processors", ConfigValue::array(std::move(identifiers)));
  data.set("generator", workload.generator);
  return data;
}

// cppcheck-suppress unusedFunction
std::optional<Workload> workload_from_config(const ConfigValue &data, std::string *error) {
  if (data.type() != ConfigValueType::object) {
    record_error(error, "workload section must be an object");
    return std::nullopt;
  }

  const ConfigValue *level_value = data.find("level");
  if (level_value == nullptr) {
    record_error(error, "workload.level is required");
    return std::nullopt;
  }
  const auto level_token = level_value->as_string();
  if (!level_token.has_value()) {
    record_error(error, "workload.level must be a string");
    return std::nullopt;
  }
  const auto level = parse_workload_level(*level_token);
  if (!level.has_value()) {
    record_error(error, "workload.level has an unknown value");
    return std::nullopt;
  }

  const ConfigValue *processors_value = data.find("processors");
  if (processors_value == nullptr) {
    record_error(error, "workload.processors is required");
    return std::nullopt;
  }
  const auto *elements = processors_value->as_array();
  if (elements == nullptr) {
    record_error(error, "workload.processors must be an array");
    return std::nullopt;
  }
  const bool all_strings =
      std::all_of(elements->begin(), elements->end(),
                  [](const ConfigValue &element) { return element.as_string().has_value(); });
  if (!all_strings) {
    record_error(error, "workload.processors entries must be strings");
    return std::nullopt;
  }
  std::vector<std::string> processors;
  processors.reserve(elements->size());
  std::transform(elements->begin(), elements->end(), std::back_inserter(processors),
                 [](const ConfigValue &element) { return *element.as_string(); });

  const ConfigValue *generator_value = data.find("generator");
  if (generator_value == nullptr) {
    record_error(error, "workload.generator is required");
    return std::nullopt;
  }
  if (generator_value->type() != ConfigValueType::object) {
    record_error(error, "workload.generator must be an object");
    return std::nullopt;
  }

  Workload workload;
  workload.level = *level;
  workload.processors = std::move(processors);
  workload.generator = *generator_value;
  return workload;
}

} // namespace ztick
