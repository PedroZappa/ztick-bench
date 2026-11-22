// SPDX-License-Identifier: MIT
/**
 * @file validation_test.cpp
 * @brief Catch2 tests for the post-timing validation phase.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/execution.hpp>
#include <ztick/validation.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

/** @brief Helpers for the validation tests. */
namespace {

/**
 * @brief Build a deterministic clock advancing one second per read.
 * @param calls Receives the number of clock reads.
 * @return Clock suitable for asserting that validation performs no timing calls.
 */
ztick::Clock advancing_clock(int &calls) {
  return [&calls]() { return static_cast<double>(++calls); };
}

/**
 * @brief Collect check identifiers from a report in execution order.
 * @param report Report to inspect.
 * @return Check identifiers in the order they were recorded.
 */
std::vector<std::string> check_names(const ztick::ValidationReport &report) {
  std::vector<std::string> names;
  names.reserve(report.outcomes.size());
  std::transform(report.outcomes.begin(), report.outcomes.end(), std::back_inserter(names),
                 [](const ztick::CheckOutcome &outcome) { return outcome.check; });
  return names;
}

/**
 * @brief Build a clean output case with a deterministic reference.
 * @return Case whose output matches its reference and lies inside its range.
 */
ztick::ValidationCase matching_case() {
  ztick::ValidationCase input;
  input.label = "primary";
  input.actual = {1.0, 2.0, 3.0};
  input.reference = {1.0, 2.0, 3.0};
  input.reference_source = ztick::ReferenceSource::deterministic_generation;
  input.min_value = -1.0;
  input.max_value = 4.0;
  return input;
}

} // namespace

/**
 * @brief Verify validation metadata tokens are stable.
 */
TEST_CASE("validation metadata tokens are stable", "[core][validation]") {
  REQUIRE(ztick::check_status_name(ztick::CheckStatus::passed) == "passed");
  REQUIRE(ztick::check_status_name(ztick::CheckStatus::failed) == "failed");
  REQUIRE(ztick::check_status_name(ztick::CheckStatus::not_applicable) == "not_applicable");
  REQUIRE(ztick::check_status_name(ztick::CheckStatus::unavailable) == "unavailable");

  REQUIRE(ztick::reference_source_name(ztick::ReferenceSource::none) == "none");
  REQUIRE(ztick::reference_source_name(ztick::ReferenceSource::adapter_data) == "adapter_data");
  REQUIRE(ztick::reference_source_name(ztick::ReferenceSource::workload_file) == "workload_file");
  REQUIRE(ztick::reference_source_name(ztick::ReferenceSource::deterministic_generation) ==
          "deterministic_generation");
  REQUIRE(ztick::reference_source_name(ztick::ReferenceSource::reference_implementation) ==
          "reference_implementation");
}

/**
 * @brief Verify the tolerance formula against manually computed allowances.
 */
TEST_CASE("tolerance comparison matches independent values", "[core][validation]") {
  const ztick::Tolerance tolerance{0.01, 0.02};
  // |reference| = 100 gives allowance 0.01 + 0.02 * 100 = 2.01.
  REQUIRE(ztick::within_tolerance(101.99, 100.0, tolerance));
  REQUIRE(ztick::within_tolerance(98.01, 100.0, tolerance));
  REQUIRE_FALSE(ztick::within_tolerance(102.02, 100.0, tolerance));
  // Negative references use the absolute value, giving the same allowance.
  REQUIRE(ztick::within_tolerance(-101.99, -100.0, tolerance));
  REQUIRE_FALSE(ztick::within_tolerance(-102.02, -100.0, tolerance));

  REQUIRE_FALSE(ztick::within_tolerance(std::numeric_limits<double>::quiet_NaN(), 0.0, tolerance));
  REQUIRE_FALSE(ztick::within_tolerance(0.0, std::numeric_limits<double>::infinity(), tolerance));
  REQUIRE_FALSE(ztick::within_tolerance(0.0, 0.0, ztick::Tolerance{-1.0, 0.0}));
  REQUIRE_FALSE(ztick::within_tolerance(
      0.0, 0.0, ztick::Tolerance{0.0, std::numeric_limits<double>::infinity()}));
}

/**
 * @brief Verify Basic finite and range checks, including skip reporting.
 */
TEST_CASE("basic checks detect non-finite and out-of-range output", "[core][validation]") {
  ztick::ValidationCase input = matching_case();
  const auto finite = ztick::check_finite(input, ztick::ValidationLevel::basic);
  REQUIRE(finite.check == "finite_output");
  REQUIRE(finite.level == ztick::ValidationLevel::basic);
  REQUIRE(finite.status == ztick::CheckStatus::passed);
  REQUIRE(finite.checked_samples == 3U);

  const auto in_range = ztick::check_range(input, ztick::ValidationLevel::basic);
  REQUIRE(in_range.status == ztick::CheckStatus::passed);

  input.actual[1] = std::numeric_limits<double>::quiet_NaN();
  const auto non_finite = ztick::check_finite(input, ztick::ValidationLevel::basic);
  REQUIRE(non_finite.status == ztick::CheckStatus::failed);
  REQUIRE(non_finite.reason == "non_finite_output");
  REQUIRE(non_finite.observed_error == Catch::Approx(1.0));

  input.actual[1] = 2.0;
  input.actual[2] = std::numeric_limits<double>::infinity();
  const auto infinite = ztick::check_finite(input, ztick::ValidationLevel::basic);
  REQUIRE(infinite.status == ztick::CheckStatus::failed);
  REQUIRE(infinite.observed_error == Catch::Approx(1.0));

  input.actual[2] = 9.0;
  const auto outside = ztick::check_range(input, ztick::ValidationLevel::basic);
  REQUIRE(outside.status == ztick::CheckStatus::failed);
  REQUIRE(outside.reason == "out_of_range");
  REQUIRE(outside.observed_error == Catch::Approx(1.0));

  ztick::ValidationCase unbounded = matching_case();
  unbounded.min_value.reset();
  unbounded.max_value.reset();
  const auto skipped = ztick::check_range(unbounded, ztick::ValidationLevel::basic);
  REQUIRE(skipped.status == ztick::CheckStatus::not_applicable);
  REQUIRE(skipped.reason == "no_range_configured");

  ztick::ValidationCase empty = matching_case();
  empty.actual.clear();
  const auto empty_output = ztick::check_finite(empty, ztick::ValidationLevel::basic);
  REQUIRE(empty_output.status == ztick::CheckStatus::failed);
  REQUIRE(empty_output.reason == "empty_output");
}

/**
 * @brief Verify Standard reference comparison records error and method metadata.
 */
TEST_CASE("reference comparison applies tolerances and records context", "[core][validation]") {
  const ztick::Tolerance tolerance{0.1, 0.01};
  ztick::ValidationCase input = matching_case();
  input.actual[2] = 3.05; // allowance is 0.1 + 0.01 * 3 = 0.13.

  const auto passed = ztick::check_reference(input, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(passed.status == ztick::CheckStatus::passed);
  REQUIRE(passed.check == "reference_comparison");
  REQUIRE(passed.level == ztick::ValidationLevel::standard);
  REQUIRE(passed.observed_error == Catch::Approx(0.05));
  REQUIRE(passed.checked_samples == 3U);
  REQUIRE(passed.tolerance == tolerance);
  REQUIRE(passed.reference_source == ztick::ReferenceSource::deterministic_generation);

  input.actual[2] = 3.2; // error 0.2 exceeds the 0.13 allowance.
  const auto failed = ztick::check_reference(input, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(failed.status == ztick::CheckStatus::failed);
  REQUIRE(failed.reason == "reference_mismatch");
  REQUIRE(failed.observed_error == Catch::Approx(0.2));

  ztick::ValidationCase no_reference = matching_case();
  no_reference.reference.clear();
  no_reference.reference_source = ztick::ReferenceSource::none;
  const auto skipped =
      ztick::check_reference(no_reference, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(skipped.status == ztick::CheckStatus::not_applicable);
  REQUIRE(skipped.reason == "no_reference");

  ztick::ValidationCase mismatched = matching_case();
  mismatched.reference.pop_back();
  const auto unavailable =
      ztick::check_reference(mismatched, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(unavailable.status == ztick::CheckStatus::unavailable);
  REQUIRE(unavailable.reason == "reference_length_mismatch");
}

/**
 * @brief Verify expected-change detection for a designated reference implementation.
 */
TEST_CASE("expected change detection distinguishes changed output", "[core][validation]") {
  const ztick::Tolerance tolerance{0.01, 0.0};
  ztick::ValidationCase input = matching_case();
  input.reference_source = ztick::ReferenceSource::reference_implementation;

  const auto skipped =
      ztick::check_expected_change(input, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(skipped.status == ztick::CheckStatus::not_applicable);
  REQUIRE(skipped.reason == "no_change_expected");

  input.expected_change = true;
  const auto unchanged =
      ztick::check_expected_change(input, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(unchanged.status == ztick::CheckStatus::failed);
  REQUIRE(unchanged.reason == "output_unchanged");

  input.actual[0] = 1.5;
  const auto changed =
      ztick::check_expected_change(input, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(changed.status == ztick::CheckStatus::passed);
  REQUIRE(changed.observed_error == Catch::Approx(0.5));

  ztick::ValidationCase no_reference = matching_case();
  no_reference.expected_change = true;
  no_reference.reference.clear();
  const auto unavailable =
      ztick::check_expected_change(no_reference, tolerance, ztick::ValidationLevel::standard);
  REQUIRE(unavailable.status == ztick::CheckStatus::unavailable);
  REQUIRE(unavailable.reason == "no_reference");
}

/**
 * @brief Verify adapter custom checks are only evaluated at Strict level.
 */
TEST_CASE("adapter custom check reports absence and failure", "[core][validation]") {
  const ztick::ValidationCase input = matching_case();
  const auto absent = ztick::check_custom(input, {}, ztick::ValidationLevel::strict);
  REQUIRE(absent.status == ztick::CheckStatus::not_applicable);
  REQUIRE(absent.reason == "no_custom_check");

  const ztick::CustomCheck accepts = [](std::string_view label,
                                        const std::vector<double> &samples) {
    return label == "primary" && !samples.empty();
  };
  const auto passed = ztick::check_custom(input, accepts, ztick::ValidationLevel::strict);
  REQUIRE(passed.check == "adapter_custom_check");
  REQUIRE(passed.level == ztick::ValidationLevel::strict);
  REQUIRE(passed.status == ztick::CheckStatus::passed);

  const ztick::CustomCheck rejects = [](std::string_view, const std::vector<double> &) {
    return false;
  };
  const auto failed = ztick::check_custom(input, rejects, ztick::ValidationLevel::strict);
  REQUIRE(failed.status == ztick::CheckStatus::failed);
  REQUIRE(failed.reason == "custom_check_failed");
}

/**
 * @brief Verify each level selects the required checks and case coverage.
 */
TEST_CASE("validation levels select checks and coverage", "[core][validation]") {
  const ztick::ValidationCase first = matching_case();
  ztick::ValidationCase second = matching_case();
  second.label = "extended";
  const std::vector<ztick::ValidationCase> inputs{first, second};

  ztick::ValidationOptions options;
  options.custom_check = [](std::string_view, const std::vector<double> &) { return true; };

  const auto basic = ztick::run_validation(inputs, options);
  REQUIRE(basic.has_value());
  REQUIRE(basic->level == ztick::ValidationLevel::basic);
  REQUIRE(basic->cases_checked == 1U);
  REQUIRE(check_names(*basic) == std::vector<std::string>{"finite_output", "output_range"});

  options.level = ztick::ValidationLevel::standard;
  const auto standard = ztick::run_validation(inputs, options);
  REQUIRE(standard.has_value());
  REQUIRE(standard->cases_checked == 1U);
  REQUIRE(check_names(*standard) == std::vector<std::string>{"finite_output", "output_range",
                                                             "reference_comparison",
                                                             "expected_change"});

  options.level = ztick::ValidationLevel::strict;
  const auto strict = ztick::run_validation(inputs, options);
  REQUIRE(strict.has_value());
  REQUIRE(strict->cases_checked == 2U);
  REQUIRE(strict->outcomes.size() == 10U);
  REQUIRE(strict->outcomes.back().case_label == "extended");
  REQUIRE(strict->all_passed());
  REQUIRE(strict->failed_count() == 0U);
}

/**
 * @brief Verify failed checks are aggregated without hiding other outcomes.
 */
TEST_CASE("validation report aggregates failures", "[core][validation]") {
  ztick::ValidationCase input = matching_case();
  input.actual[0] = 100.0; // out of range and far from the reference.

  ztick::ValidationOptions options;
  options.level = ztick::ValidationLevel::standard;
  const auto report = ztick::run_validation({input}, options);
  REQUIRE(report.has_value());
  REQUIRE_FALSE(report->all_passed());
  REQUIRE(report->failed_count() == 2U);

  const auto unavailable = [](const ztick::CheckOutcome &outcome) {
    return outcome.status == ztick::CheckStatus::unavailable;
  };
  REQUIRE(std::none_of(report->outcomes.begin(), report->outcomes.end(), unavailable));
}

/**
 * @brief Verify invalid validation configuration is rejected with diagnostics.
 */
TEST_CASE("validation configuration is validated", "[core][validation]") {
  ztick::ValidationOptions options;
  options.tolerance = ztick::Tolerance{-1.0, 0.0};
  std::string error;
  REQUIRE_FALSE(ztick::run_validation({matching_case()}, options, &error).has_value());
  REQUIRE(error == "tolerance must be finite and non-negative");

  options = ztick::ValidationOptions{};
  error.clear();
  REQUIRE_FALSE(ztick::run_validation({}, options, &error).has_value());
  REQUIRE(error == "at least one validation case is required");
}

/**
 * @brief Verify validation at every level leaves primary timing unchanged.
 */
TEST_CASE("all validation levels leave timing unchanged", "[core][validation]") {
  ztick::SequenceActions baseline_actions;
  baseline_actions.primary = []() {};
  int baseline_calls = 0;
  const auto baseline = ztick::run_sequence(baseline_actions, ztick::SequenceOptions{},
                                            advancing_clock(baseline_calls));
  REQUIRE(baseline.has_value());
  REQUIRE(baseline_calls == 2);

  const std::vector<ztick::ValidationLevel> levels{ztick::ValidationLevel::basic,
                                                   ztick::ValidationLevel::standard,
                                                   ztick::ValidationLevel::strict};
  for (const ztick::ValidationLevel level : levels) {
    std::vector<std::string> phases;
    std::optional<ztick::ValidationReport> captured;
    ztick::SequenceActions actions;
    actions.primary = [&phases]() { phases.push_back("primary"); };
    actions.validation = [&]() {
      ztick::ValidationOptions options;
      options.level = level;
      options.custom_check = [](std::string_view, const std::vector<double> &) { return true; };
      captured = ztick::run_validation({matching_case()}, options);
      phases.push_back("validation");
    };

    int clock_calls = 0;
    const auto traced =
        ztick::run_sequence(actions, ztick::SequenceOptions{}, advancing_clock(clock_calls));
    REQUIRE(traced.has_value());
    REQUIRE(captured.has_value());
    REQUIRE(captured->all_passed());
    REQUIRE(phases == std::vector<std::string>{"primary", "validation"});

    // Validation performs no clock reads and produces identical timing values.
    REQUIRE(clock_calls == baseline_calls);
    REQUIRE(traced->measurements.size() == baseline->measurements.size());
    REQUIRE(traced->measurements[0].duration_seconds() ==
            Catch::Approx(baseline->measurements[0].duration_seconds()));
    REQUIRE(*ztick::find_samples(*traced, ztick::TimingLevel::whole_run) ==
            *ztick::find_samples(*baseline, ztick::TimingLevel::whole_run));
  }
}
