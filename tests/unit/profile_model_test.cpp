// SPDX-License-Identifier: MIT
/**
 * @file profile_model_test.cpp
 * @brief Catch2 unit tests for the profile model and its config round-trip.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/profile.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/** @brief Helpers that build config-shaped profile data with literal values. */
namespace {

/**
 * @brief Build a valid config document using literals independent of the model.
 * @return Config-shaped object with all four profile-owned sections.
 */
ztick::ConfigValue make_literal_config() {
  using ztick::ConfigValue;

  ConfigValue document = ConfigValue::object();

  ConfigValue profile = ConfigValue::object();
  profile.set("name", ConfigValue::string("Standard"));
  ConfigValue coverage = ConfigValue::array();
  coverage.push_back(ConfigValue::string("48000"));
  coverage.push_back(ConfigValue::string("128"));
  profile.set("coverage", coverage);
  ConfigValue checks = ConfigValue::array();
  checks.push_back(ConfigValue::string("stability"));
  profile.set("reliability_checks", checks);
  document.set("profile", profile);

  ConfigValue warmup = ConfigValue::object();
  warmup.set("iterations", ConfigValue::integer(200));
  ConfigValue execution = ConfigValue::object();
  execution.set("duration_mode", ConfigValue::string("duration"));
  execution.set("iterations", ConfigValue::integer(0));
  execution.set("duration_seconds", ConfigValue::real(25.0));
  execution.set("repetitions", ConfigValue::integer(3));
  execution.set("warmup", warmup);
  document.set("execution", execution);

  ConfigValue timing_levels = ConfigValue::array();
  timing_levels.push_back(ConfigValue::string("per_block"));
  timing_levels.push_back(ConfigValue::string("whole_run"));
  ConfigValue measurement = ConfigValue::object();
  measurement.set("detail", ConfigValue::string("per_block"));
  measurement.set("end_to_end", ConfigValue::boolean(true));
  measurement.set("timing_levels", timing_levels);
  document.set("measurement", measurement);

  ConfigValue validation = ConfigValue::object();
  validation.set("level", ConfigValue::string("Strict"));
  document.set("validation", validation);

  return document;
}

} // namespace

/**
 * @brief Verify duration mode tokens parse and render symmetrically.
 */
TEST_CASE("duration mode tokens round-trip", "[core][profile]") {
  using ztick::DurationMode;

  REQUIRE(ztick::parse_duration_mode("iterations") == DurationMode::iterations);
  REQUIRE(ztick::parse_duration_mode("duration") == DurationMode::duration);
  REQUIRE_FALSE(ztick::parse_duration_mode("wallclock").has_value());
  REQUIRE(ztick::duration_mode_name(DurationMode::iterations) == "iterations");
  REQUIRE(ztick::duration_mode_name(DurationMode::duration) == "duration");
  REQUIRE(ztick::duration_mode_name(static_cast<DurationMode>(99)) == "unknown");
}

/**
 * @brief Verify measurement detail tokens parse and render symmetrically.
 */
TEST_CASE("measurement detail tokens round-trip", "[core][profile]") {
  using ztick::MeasurementDetail;

  REQUIRE(ztick::parse_measurement_detail("aggregate") == MeasurementDetail::aggregate);
  REQUIRE(ztick::parse_measurement_detail("per_block") == MeasurementDetail::per_block);
  REQUIRE_FALSE(ztick::parse_measurement_detail("per_sample").has_value());
  REQUIRE(ztick::measurement_detail_name(MeasurementDetail::aggregate) == "aggregate");
  REQUIRE(ztick::measurement_detail_name(MeasurementDetail::per_block) == "per_block");
  REQUIRE(ztick::measurement_detail_name(static_cast<MeasurementDetail>(99)) == "unknown");
}

/**
 * @brief Verify validation level tokens parse and render symmetrically.
 */
TEST_CASE("validation level tokens round-trip", "[core][profile]") {
  using ztick::ValidationLevel;

  REQUIRE(ztick::parse_validation_level("Basic") == ValidationLevel::basic);
  REQUIRE(ztick::parse_validation_level("Standard") == ValidationLevel::standard);
  REQUIRE(ztick::parse_validation_level("Strict") == ValidationLevel::strict);
  REQUIRE_FALSE(ztick::parse_validation_level("basic").has_value());
  REQUIRE(ztick::validation_level_name(ValidationLevel::basic) == "Basic");
  REQUIRE(ztick::validation_level_name(ValidationLevel::standard) == "Standard");
  REQUIRE(ztick::validation_level_name(ValidationLevel::strict) == "Strict");
  REQUIRE(ztick::validation_level_name(static_cast<ValidationLevel>(99)) == "unknown");
}

/**
 * @brief Verify a hand-built config document produces the expected profile.
 */
TEST_CASE("profile_from_config reads literal values", "[core][profile]") {
  std::string error;
  const auto profile = ztick::profile_from_config(make_literal_config(), &error);

  REQUIRE(profile.has_value());
  REQUIRE(error.empty());
  REQUIRE(profile->name == std::string{"Standard"});
  REQUIRE(profile->duration_mode == ztick::DurationMode::duration);
  REQUIRE(profile->iterations == 0);
  REQUIRE(profile->duration_seconds == 25.0);
  REQUIRE(profile->repetitions == 3);
  REQUIRE(profile->warmup_iterations == 200);
  REQUIRE(profile->coverage == std::vector<std::string>{"48000", "128"});
  REQUIRE(profile->detail == ztick::MeasurementDetail::per_block);
  REQUIRE(profile->end_to_end);
  REQUIRE(profile->timing_levels == std::vector<std::string>{"per_block", "whole_run"});
  REQUIRE(profile->validation_level == ztick::ValidationLevel::strict);
  REQUIRE(profile->reliability_checks == std::vector<std::string>{"stability"});
}

/**
 * @brief Verify profiles round-trip in both directions through config data.
 */
TEST_CASE("profiles round-trip from config", "[core][profile]") {
  const ztick::ConfigValue original = make_literal_config();
  const auto profile = ztick::profile_from_config(original);
  REQUIRE(profile.has_value());

  const ztick::ConfigValue rendered = ztick::profile_to_config(*profile);
  REQUIRE(rendered == original);

  const auto reloaded = ztick::profile_from_config(rendered);
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->name == profile->name);
  REQUIRE(reloaded->duration_mode == profile->duration_mode);
  REQUIRE(reloaded->iterations == profile->iterations);
  REQUIRE(reloaded->duration_seconds == profile->duration_seconds);
  REQUIRE(reloaded->repetitions == profile->repetitions);
  REQUIRE(reloaded->warmup_iterations == profile->warmup_iterations);
  REQUIRE(reloaded->coverage == profile->coverage);
  REQUIRE(reloaded->detail == profile->detail);
  REQUIRE(reloaded->end_to_end == profile->end_to_end);
  REQUIRE(reloaded->timing_levels == profile->timing_levels);
  REQUIRE(reloaded->validation_level == profile->validation_level);
  REQUIRE(reloaded->reliability_checks == profile->reliability_checks);
}

/**
 * @brief Verify the built-in profile defaults render and reload unchanged.
 */
TEST_CASE("default profile round-trips", "[core][profile]") {
  const ztick::Profile expected;
  const ztick::ConfigValue data = ztick::profile_to_config(expected);
  const auto actual = ztick::profile_from_config(data);

  REQUIRE(actual.has_value());
  REQUIRE(actual->name == expected.name);
  REQUIRE(actual->duration_mode == expected.duration_mode);
  REQUIRE(actual->iterations == expected.iterations);
  REQUIRE(actual->duration_seconds == expected.duration_seconds);
  REQUIRE(actual->repetitions == expected.repetitions);
  REQUIRE(actual->warmup_iterations == expected.warmup_iterations);
  REQUIRE(actual->coverage == expected.coverage);
  REQUIRE(actual->detail == expected.detail);
  REQUIRE(actual->end_to_end == expected.end_to_end);
  REQUIRE(actual->timing_levels == expected.timing_levels);
  REQUIRE(actual->validation_level == expected.validation_level);
  REQUIRE(actual->reliability_checks == expected.reliability_checks);
}

/**
 * @brief Verify missing or malformed sections are rejected.
 */
TEST_CASE("profile_from_config rejects bad sections", "[core][profile]") {
  using ztick::ConfigValue;

  std::string error;
  REQUIRE_FALSE(ztick::profile_from_config(ConfigValue::array(), &error).has_value());
  REQUIRE_FALSE(error.empty());

  ConfigValue missing_profile = make_literal_config();
  REQUIRE(ztick::profile_from_config(missing_profile).has_value());
  ConfigValue no_profile_section = ConfigValue::object();
  REQUIRE_FALSE(ztick::profile_from_config(no_profile_section).has_value());

  ConfigValue profile_not_object = make_literal_config();
  profile_not_object.set("profile", ConfigValue::array());
  REQUIRE_FALSE(ztick::profile_from_config(profile_not_object).has_value());

  ConfigValue no_name = make_literal_config();
  no_name.set("profile", ConfigValue::object());
  REQUIRE_FALSE(ztick::profile_from_config(no_name).has_value());

  ConfigValue no_execution = make_literal_config();
  no_execution.set("execution", ConfigValue::integer(1));
  REQUIRE_FALSE(ztick::profile_from_config(no_execution).has_value());

  ConfigValue no_measurement = make_literal_config();
  no_measurement.set("measurement", ConfigValue::null());
  REQUIRE_FALSE(ztick::profile_from_config(no_measurement).has_value());

  ConfigValue no_validation = make_literal_config();
  no_validation.set("validation", ConfigValue::array());
  REQUIRE_FALSE(ztick::profile_from_config(no_validation).has_value());
}

/**
 * @brief Verify malformed execution settings are rejected with a diagnostic.
 */
TEST_CASE("profile_from_config rejects bad execution values", "[core][profile]") {
  using ztick::ConfigValue;

  std::string error;
  auto check_bad = [&error](ConfigValue data) {
    error.clear();
    REQUIRE_FALSE(ztick::profile_from_config(data, &error).has_value());
    REQUIRE_FALSE(error.empty());
  };

  ConfigValue no_mode = make_literal_config();
  no_mode.set("execution", ConfigValue::object());
  check_bad(no_mode);

  ConfigValue unknown_mode = make_literal_config();
  ztick::ConfigValue execution = *unknown_mode.find("execution");
  execution.set("duration_mode", ConfigValue::string("wallclock"));
  unknown_mode.set("execution", execution);
  check_bad(unknown_mode);

  ConfigValue negative_iterations = make_literal_config();
  execution = *negative_iterations.find("execution");
  execution.set("iterations", ConfigValue::integer(-1));
  negative_iterations.set("execution", execution);
  check_bad(negative_iterations);

  ConfigValue bad_duration = make_literal_config();
  execution = *bad_duration.find("execution");
  execution.set("duration_seconds", ConfigValue::real(std::nan("")));
  bad_duration.set("execution", execution);
  check_bad(bad_duration);

  ConfigValue huge_repetitions = make_literal_config();
  execution = *huge_repetitions.find("execution");
  execution.set("repetitions",
                ConfigValue::integer(
                    static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1));
  huge_repetitions.set("execution", execution);
  check_bad(huge_repetitions);

  ConfigValue bad_warmup = make_literal_config();
  execution = *bad_warmup.find("execution");
  execution.set("warmup", ConfigValue::array());
  bad_warmup.set("execution", execution);
  check_bad(bad_warmup);

  ConfigValue bad_warmup_count = make_literal_config();
  execution = *bad_warmup_count.find("execution");
  ztick::ConfigValue warmup = ConfigValue::object();
  warmup.set("iterations", ConfigValue::integer(-5));
  execution.set("warmup", warmup);
  bad_warmup_count.set("execution", execution);
  check_bad(bad_warmup_count);
}

/**
 * @brief Verify malformed measurement, validation and list values are rejected.
 */
TEST_CASE("profile_from_config rejects bad settings", "[core][profile]") {
  using ztick::ConfigValue;

  std::string error;
  auto check_bad = [&error](ConfigValue data) {
    error.clear();
    REQUIRE_FALSE(ztick::profile_from_config(data, &error).has_value());
    REQUIRE_FALSE(error.empty());
  };

  ConfigValue bad_detail = make_literal_config();
  ztick::ConfigValue measurement = *bad_detail.find("measurement");
  measurement.set("detail", ConfigValue::string("per_sample"));
  bad_detail.set("measurement", measurement);
  check_bad(bad_detail);

  ConfigValue bad_end_to_end = make_literal_config();
  measurement = *bad_end_to_end.find("measurement");
  measurement.set("end_to_end", ConfigValue::integer(1));
  bad_end_to_end.set("measurement", measurement);
  check_bad(bad_end_to_end);

  ConfigValue bad_timing_levels = make_literal_config();
  measurement = *bad_timing_levels.find("measurement");
  measurement.set("timing_levels", ConfigValue::object());
  bad_timing_levels.set("measurement", measurement);
  check_bad(bad_timing_levels);

  ConfigValue bad_level = make_literal_config();
  ztick::ConfigValue validation = ConfigValue::object();
  validation.set("level", ConfigValue::string("Paranoid"));
  bad_level.set("validation", validation);
  check_bad(bad_level);

  ConfigValue bad_coverage = make_literal_config();
  ztick::ConfigValue profile = *bad_coverage.find("profile");
  profile.set("coverage", ConfigValue::string("48000"));
  bad_coverage.set("profile", profile);
  check_bad(bad_coverage);

  ConfigValue bad_checks = make_literal_config();
  profile = *bad_checks.find("profile");
  ztick::ConfigValue checks = ConfigValue::array();
  checks.push_back(ConfigValue::integer(7));
  profile.set("reliability_checks", checks);
  bad_checks.set("profile", profile);
  check_bad(bad_checks);
}
