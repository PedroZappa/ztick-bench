// SPDX-License-Identifier: MIT
/**
 * @file workload.cpp
 * @brief Workload model conversions between C++ objects and config-shaped data.
 */

#include "ztick/workload.hpp"

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
ConfigValue workload_to_config(const Workload &workload) {
  ConfigValue data = ConfigValue::object();
  data.set("level", ConfigValue::string(std::string{workload_level_name(workload.level)}));

  ConfigValue processors = ConfigValue::array();
  for (const auto &identifier : workload.processors) {
    processors.push_back(ConfigValue::string(identifier));
  }
  data.set("processors", std::move(processors));
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
  std::vector<std::string> processors;
  processors.reserve(elements->size());
  for (const auto &element : *elements) {
    const auto identifier = element.as_string();
    if (!identifier.has_value()) {
      record_error(error, "workload.processors entries must be strings");
      return std::nullopt;
    }
    processors.push_back(*identifier);
  }

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
