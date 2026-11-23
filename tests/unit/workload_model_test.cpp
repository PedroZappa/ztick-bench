// SPDX-License-Identifier: MIT
/**
 * @file workload_model_test.cpp
 * @brief Catch2 unit tests for workload models and their config round-trip.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/workload.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

/** @brief Helpers that build config-shaped workload data for the tests. */
namespace {

/**
 * @brief Build a minimal generator settings object.
 * @return Config-shaped object with a generator kind and frequency.
 */
ztick::ConfigValue make_generator() {
  ztick::ConfigValue generator = ztick::ConfigValue::object();
  generator.set("kind", ztick::ConfigValue::string("sine"));
  generator.set("frequency_hz", ztick::ConfigValue::real(1000.0));
  return generator;
}

/**
 * @brief Build a valid workload section for a given level and processor list.
 * @param level Token for the workload level.
 * @param processors Processor identifiers to include.
 * @return Config-shaped workload section.
 */
ztick::ConfigValue make_config(std::string_view level, const std::vector<std::string> &processors) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("level", ztick::ConfigValue::string(std::string{level}));
  ztick::ConfigValue names = ztick::ConfigValue::array();
  for (const auto &name : processors) {
    names.push_back(ztick::ConfigValue::string(name));
  }
  data.set("processors", names);
  data.set("generator", make_generator());
  return data;
}

} // namespace

/**
 * @brief Verify workload level tokens parse and render symmetrically.
 */
TEST_CASE("workload level tokens round-trip", "[core][workload]") {
  using ztick::WorkloadLevel;

  REQUIRE(ztick::parse_workload_level("processor") == WorkloadLevel::processor);
  REQUIRE(ztick::parse_workload_level("chain") == WorkloadLevel::chain);
  REQUIRE(ztick::parse_workload_level("engine") == WorkloadLevel::engine);
  REQUIRE_FALSE(ztick::parse_workload_level("suite").has_value());

  REQUIRE(ztick::workload_level_name(WorkloadLevel::processor) == "processor");
  REQUIRE(ztick::workload_level_name(WorkloadLevel::chain) == "chain");
  REQUIRE(ztick::workload_level_name(WorkloadLevel::engine) == "engine");
  REQUIRE(ztick::workload_level_name(static_cast<WorkloadLevel>(99)) == "unknown");
}

/**
 * @brief Verify each workload level maps to exactly one SDK capability bit.
 */
TEST_CASE("workload levels map to required capabilities", "[core][workload]") {
  using ztick::WorkloadLevel;

  REQUIRE(ztick::required_capability(WorkloadLevel::processor) == ZTICK_CAPABILITY_PROCESSOR);
  REQUIRE(ztick::required_capability(WorkloadLevel::chain) == ZTICK_CAPABILITY_CHAIN);
  REQUIRE(ztick::required_capability(WorkloadLevel::engine) == ZTICK_CAPABILITY_ENGINE);
  REQUIRE(ztick::required_capability(static_cast<WorkloadLevel>(99)) == 0U);
  REQUIRE((ZTICK_CAPABILITY_KNOWN_MASK & ZTICK_CAPABILITY_CHAIN) == ZTICK_CAPABILITY_CHAIN);
}

/**
 * @brief Verify a workload renders into the documented config section shape.
 */
TEST_CASE("workload_to_config emits documented fields", "[core][workload]") {
  ztick::Workload workload;
  workload.level = ztick::WorkloadLevel::chain;
  workload.processors = {"eq", "compressor"};
  workload.generator = make_generator();

  const ztick::ConfigValue data = ztick::workload_to_config(workload);

  REQUIRE(data.type() == ztick::ConfigValueType::object);
  REQUIRE(data.find("level")->as_string() == std::string{"chain"});
  const auto *processors = data.find("processors")->as_array();
  REQUIRE(processors != nullptr);
  REQUIRE(processors->size() == 2);
  REQUIRE((*processors)[0].as_string() == std::string{"eq"});
  REQUIRE((*processors)[1].as_string() == std::string{"compressor"});
  REQUIRE(data.find("generator")->find("kind")->as_string() == std::string{"sine"});
}

/**
 * @brief Verify all three workload levels round-trip through config-shaped data.
 */
TEST_CASE("workloads round-trip from config", "[core][workload]") {
  const std::vector<std::pair<std::string, std::vector<std::string>>> cases{
      {"processor", {"eq"}},
      {"chain", {"eq", "compressor", "limiter"}},
      {"engine", {"integrated_engine"}},
  };

  for (const auto &[level, processors] : cases) {
    const ztick::ConfigValue original = make_config(level, processors);
    const auto workload = ztick::workload_from_config(original);
    REQUIRE(workload.has_value());
    REQUIRE(workload->level == ztick::parse_workload_level(level).value());
    REQUIRE(workload->processors == processors);

    const ztick::ConfigValue rendered = ztick::workload_to_config(*workload);
    REQUIRE(rendered == original);

    const auto reloaded = ztick::workload_from_config(rendered);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->level == workload->level);
    REQUIRE(reloaded->processors == workload->processors);
    REQUIRE(reloaded->generator == workload->generator);
  }
}

/**
 * @brief Verify malformed workload sections are rejected with a diagnostic.
 */
TEST_CASE("workload_from_config rejects malformed data", "[core][workload]") {
  using ztick::ConfigValue;

  std::string error;
  REQUIRE_FALSE(ztick::workload_from_config(ConfigValue::array(), &error).has_value());
  REQUIRE_FALSE(error.empty());

  ConfigValue missing_level = ConfigValue::object();
  missing_level.set("processors", ConfigValue::array());
  missing_level.set("generator", make_generator());
  REQUIRE_FALSE(ztick::workload_from_config(missing_level).has_value());

  ConfigValue wrong_level_type = make_config("processor", {"eq"});
  wrong_level_type.set("level", ConfigValue::integer(1));
  error.clear();
  REQUIRE_FALSE(ztick::workload_from_config(wrong_level_type, &error).has_value());
  REQUIRE(error.find("string") != std::string::npos);

  ConfigValue unknown_level = make_config("suite", {"eq"});
  REQUIRE_FALSE(ztick::workload_from_config(unknown_level).has_value());

  ConfigValue no_processors = make_config("processor", {"eq"});
  no_processors.set("processors", ConfigValue::object());
  REQUIRE_FALSE(ztick::workload_from_config(no_processors).has_value());

  ConfigValue bad_processor = make_config("processor", {"eq"});
  ConfigValue names = ConfigValue::array();
  names.push_back(ConfigValue::integer(3));
  bad_processor.set("processors", names);
  REQUIRE_FALSE(ztick::workload_from_config(bad_processor).has_value());

  ConfigValue bad_generator = make_config("processor", {"eq"});
  bad_generator.set("generator", ConfigValue::integer(0));
  error.clear();
  REQUIRE_FALSE(ztick::workload_from_config(bad_generator, &error).has_value());
  REQUIRE(error.find("object") != std::string::npos);

  ConfigValue absent_generator = ConfigValue::object();
  absent_generator.set("level", ConfigValue::string("processor"));
  absent_generator.set("processors", ConfigValue::array());
  REQUIRE_FALSE(ztick::workload_from_config(absent_generator).has_value());

  ConfigValue empty_processors = make_config("engine", {});
  REQUIRE(ztick::workload_from_config(empty_processors).has_value());
}
