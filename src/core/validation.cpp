// SPDX-License-Identifier: MIT
/**
 * @file validation.cpp
 * @brief Implementations of the post-timing correctness validation phase.
 */

#include "ztick/validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
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
 * @brief Test whether both allowances are finite and non-negative.
 * @param tolerance Allowances to validate.
 * @return True when the tolerance can be used for comparisons.
 */
bool valid_tolerance(const Tolerance &tolerance) noexcept {
  return std::isfinite(tolerance.absolute) && tolerance.absolute >= 0.0 &&
         std::isfinite(tolerance.relative) && tolerance.relative >= 0.0;
}

/**
 * @brief Create an outcome prefilled with the case and level context.
 * @param input Case being checked.
 * @param check Identifier of the check.
 * @param level Level requiring the check.
 * @return Outcome with default not-applicable status.
 */
CheckOutcome make_outcome(const ValidationCase &input, std::string check, ValidationLevel level) {
  CheckOutcome outcome;
  outcome.check = std::move(check);
  outcome.case_label = input.label;
  outcome.level = level;
  outcome.reference_source = input.reference_source;
  return outcome;
}

/**
 * @brief Count samples that are not finite.
 * @param input Case to inspect.
 * @return Number of NaN or Inf samples.
 */
double count_non_finite(const ValidationCase &input) noexcept {
  return static_cast<double>(std::count_if(input.actual.begin(), input.actual.end(),
                                           [](double value) { return !std::isfinite(value); }));
}

/**
 * @brief Compute the largest absolute difference between two equal-length buffers.
 * @param input Case holding produced samples.
 * @return Largest `|actual - reference|`, or zero when no samples are available.
 * @note The caller checks buffer lengths first; no differences are fabricated.
 */
double max_abs_difference(const ValidationCase &input) noexcept {
  if (input.actual.empty() || input.actual.size() != input.reference.size()) {
    return 0.0;
  }
  std::vector<double> differences;
  differences.reserve(input.actual.size());
  std::transform(input.actual.begin(), input.actual.end(), input.reference.begin(),
                 std::back_inserter(differences),
                 [](double actual, double reference) { return std::fabs(actual - reference); });
  return *std::max_element(differences.begin(), differences.end());
}

/**
 * @brief Test whether a case has a usable reference buffer.
 * @param input Case to inspect.
 * @return True when a non-empty reference matching the output length is present.
 */
bool has_reference(const ValidationCase &input) noexcept {
  return !input.reference.empty() && input.reference.size() == input.actual.size();
}

} // namespace

// cppcheck-suppress unusedFunction
std::string_view reference_source_name(ReferenceSource source) noexcept {
  switch (source) {
  case ReferenceSource::none:
    return "none";
  case ReferenceSource::adapter_data:
    return "adapter_data";
  case ReferenceSource::workload_file:
    return "workload_file";
  case ReferenceSource::deterministic_generation:
    return "deterministic_generation";
  case ReferenceSource::reference_implementation:
    return "reference_implementation";
  }
  return "unknown";
}

// cppcheck-suppress unusedFunction
std::string_view check_status_name(CheckStatus status) noexcept {
  switch (status) {
  case CheckStatus::passed:
    return "passed";
  case CheckStatus::failed:
    return "failed";
  case CheckStatus::not_applicable:
    return "not_applicable";
  case CheckStatus::unavailable:
    return "unavailable";
  }
  return "unknown";
}

bool within_tolerance(double actual, double reference, const Tolerance &tolerance) noexcept {
  if (!std::isfinite(actual) || !std::isfinite(reference) || !valid_tolerance(tolerance)) {
    return false;
  }
  return std::fabs(actual - reference) <=
         tolerance.absolute + tolerance.relative * std::fabs(reference);
}

CheckOutcome check_finite(const ValidationCase &input, ValidationLevel level) {
  CheckOutcome outcome = make_outcome(input, "finite_output", level);
  outcome.checked_samples = input.actual.size();
  if (input.actual.empty()) {
    outcome.status = CheckStatus::failed;
    outcome.reason = "empty_output";
    return outcome;
  }
  const double invalid = count_non_finite(input);
  if (invalid > 0.0) {
    outcome.status = CheckStatus::failed;
    outcome.reason = "non_finite_output";
    outcome.observed_error = invalid;
  } else {
    outcome.status = CheckStatus::passed;
  }
  return outcome;
}

CheckOutcome check_range(const ValidationCase &input, ValidationLevel level) {
  CheckOutcome outcome = make_outcome(input, "output_range", level);
  outcome.checked_samples = input.actual.size();
  if (!input.min_value && !input.max_value) {
    outcome.reason = "no_range_configured";
    return outcome;
  }
  const auto outside = [&input](double value) {
    return (input.min_value && value < *input.min_value) ||
           (input.max_value && value > *input.max_value);
  };
  const double invalid =
      static_cast<double>(std::count_if(input.actual.begin(), input.actual.end(), outside));
  if (invalid > 0.0) {
    outcome.status = CheckStatus::failed;
    outcome.reason = "out_of_range";
    outcome.observed_error = invalid;
  } else {
    outcome.status = CheckStatus::passed;
  }
  return outcome;
}

CheckOutcome check_reference(const ValidationCase &input, const Tolerance &tolerance,
                             ValidationLevel level) {
  CheckOutcome outcome = make_outcome(input, "reference_comparison", level);
  outcome.tolerance = tolerance;
  if (input.reference_source == ReferenceSource::none || input.reference.empty()) {
    outcome.reason = "no_reference";
    return outcome;
  }
  if (input.actual.size() != input.reference.size()) {
    outcome.status = CheckStatus::unavailable;
    outcome.reason = "reference_length_mismatch";
    return outcome;
  }
  outcome.checked_samples = input.actual.size();
  outcome.observed_error = max_abs_difference(input);
  const bool matches = std::equal(input.actual.begin(), input.actual.end(), input.reference.begin(),
                                  [&tolerance](double actual, double reference) {
                                    return within_tolerance(actual, reference, tolerance);
                                  });
  outcome.status = matches ? CheckStatus::passed : CheckStatus::failed;
  if (!matches) {
    outcome.reason = "reference_mismatch";
  }
  return outcome;
}

CheckOutcome check_expected_change(const ValidationCase &input, const Tolerance &tolerance,
                                   ValidationLevel level) {
  CheckOutcome outcome = make_outcome(input, "expected_change", level);
  outcome.tolerance = tolerance;
  if (!input.expected_change) {
    outcome.reason = "no_change_expected";
    return outcome;
  }
  if (!has_reference(input)) {
    outcome.status = CheckStatus::unavailable;
    outcome.reason = "no_reference";
    return outcome;
  }
  outcome.checked_samples = input.actual.size();
  outcome.observed_error = max_abs_difference(input);
  const bool unchanged =
      std::equal(input.actual.begin(), input.actual.end(), input.reference.begin(),
                 [&tolerance](double actual, double reference) {
                   return within_tolerance(actual, reference, tolerance);
                 });
  outcome.status = unchanged ? CheckStatus::failed : CheckStatus::passed;
  if (unchanged) {
    outcome.reason = "output_unchanged";
  }
  return outcome;
}

CheckOutcome check_custom(const ValidationCase &input, const CustomCheck &custom_check,
                          ValidationLevel level) {
  CheckOutcome outcome = make_outcome(input, "adapter_custom_check", level);
  outcome.checked_samples = input.actual.size();
  if (!custom_check) {
    outcome.reason = "no_custom_check";
    return outcome;
  }
  outcome.status = CheckStatus::passed;
  if (!custom_check(input.label, input.actual)) {
    outcome.status = CheckStatus::failed;
    outcome.reason = "custom_check_failed";
  }
  return outcome;
}

// cppcheck-suppress unusedFunction
bool ValidationReport::all_passed() const noexcept {
  return std::none_of(outcomes.begin(), outcomes.end(), [](const CheckOutcome &outcome) {
    return outcome.status == CheckStatus::failed || outcome.status == CheckStatus::unavailable;
  });
}

// cppcheck-suppress unusedFunction
std::size_t ValidationReport::failed_count() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(outcomes.begin(), outcomes.end(), [](const CheckOutcome &outcome) {
        return outcome.status == CheckStatus::failed;
      }));
}

// cppcheck-suppress unusedFunction
std::optional<ValidationReport> run_validation(const std::vector<ValidationCase> &inputs,
                                               const ValidationOptions &options,
                                               std::string *error) {
  if (!valid_tolerance(options.tolerance)) {
    record_error(error, "tolerance must be finite and non-negative");
    return std::nullopt;
  }
  if (inputs.empty()) {
    record_error(error, "at least one validation case is required");
    return std::nullopt;
  }

  ValidationReport report;
  report.level = options.level;
  const bool strict = options.level == ValidationLevel::strict;
  const bool standard = options.level == ValidationLevel::standard || strict;
  const std::size_t case_count = strict ? inputs.size() : 1U;
  report.cases_checked = case_count;

  for (std::size_t index = 0; index < case_count; ++index) {
    const ValidationCase &input = inputs[index];
    report.outcomes.push_back(check_finite(input, options.level));
    report.outcomes.push_back(check_range(input, options.level));
    if (standard) {
      report.outcomes.push_back(check_reference(input, options.tolerance, options.level));
      report.outcomes.push_back(check_expected_change(input, options.tolerance, options.level));
    }
    if (strict) {
      report.outcomes.push_back(check_custom(input, options.custom_check, options.level));
    }
  }
  return report;
}

} // namespace ztick
