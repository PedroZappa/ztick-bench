// SPDX-License-Identifier: MIT
/**
 * @file validation.hpp
 * @brief Correctness validation as a distinct phase after timed measurement.
 *
 * BENCHMARK §10 and BR §10 define three selectable levels: Basic (finite and
 * in-range output), Standard (plus reference and expected-change checks) and
 * Strict (plus adapter-specific checks and broader case coverage). This module
 * operates only on already-produced output buffers: it never brackets, modifies
 * or subtracts anything from the primary timing interval. Outcomes are recorded
 * per check with the tolerance and reference source actually used (SDK §7).
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_VALIDATION_HPP
#define ZTICK_VALIDATION_HPP

#include "ztick/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztick {

/** @brief Absolute and relative allowances for reference comparisons (BR §10.2). */
struct Tolerance {
  /** @brief Absolute allowance; must be finite and non-negative. */
  double absolute{1e-6};
  /** @brief Relative allowance applied to `|reference|`; must be finite and non-negative. */
  double relative{1e-6};

  /**
   * @brief Compare two tolerance sets.
   * @param other Set to compare against.
   * @return True when both allowances are equal.
   */
  bool operator==(const Tolerance &other) const noexcept = default;
};

/**
 * @brief Source of a reference output (SDK §7).
 * @note The source is recorded with the comparison outcome so results from
 *       different reference kinds are not confused.
 */
enum class ReferenceSource : std::uint8_t {
  /** @brief No reference was supplied. */
  none = 0,
  /** @brief Reference data supplied by an adapter. */
  adapter_data = 1,
  /** @brief Reference file supplied with the workload. */
  workload_file = 2,
  /** @brief Reference generated deterministically for a built-in workload. */
  deterministic_generation = 3,
  /** @brief Output of a designated reference implementation. */
  reference_implementation = 4,
};

/**
 * @brief Render a @ref ztick::ReferenceSource as its metadata token.
 * @param source Source to render.
 * @return Stable token used in validation metadata.
 */
std::string_view reference_source_name(ReferenceSource source) noexcept;

/** @brief Status of one validation check. */
enum class CheckStatus : std::uint8_t {
  /** @brief The check ran and found no problem. */
  passed = 0,
  /** @brief The check ran and found a problem. */
  failed = 1,
  /** @brief The check is undefined for this case; still reported, never omitted. */
  not_applicable = 2,
  /** @brief The check could not be evaluated with the available data. */
  unavailable = 3,
};

/**
 * @brief Render a @ref ztick::CheckStatus as its metadata token.
 * @param status Status to render.
 * @return Stable token used in validation metadata.
 */
std::string_view check_status_name(CheckStatus status) noexcept;

/** @brief One post-run output case to validate. */
struct ValidationCase {
  /** @brief Human-readable case identifier recorded with each outcome. */
  std::string label{"output"};
  /** @brief Produced output samples; NaN and Inf are checked explicitly. */
  std::vector<double> actual;
  /** @brief Reference samples; empty when no reference is available. */
  std::vector<double> reference;
  /** @brief Where @ref ztick::ValidationCase::reference came from. */
  ReferenceSource reference_source{ReferenceSource::none};
  /** @brief Optional inclusive lower bound for in-range checking. */
  std::optional<double> min_value;
  /** @brief Optional inclusive upper bound for in-range checking. */
  std::optional<double> max_value;
  /** @brief Whether the output is expected to differ from the reference. */
  bool expected_change{false};
};

/**
 * @brief Test one sample pair against the recorded tolerance formula.
 * @param actual Produced sample.
 * @param reference Reference sample.
 * @param tolerance Allowances to apply.
 * @return True when `|actual - reference| <= atol + rtol * |reference|`.
 * @note Non-finite samples and invalid tolerances never pass; the caller records
 *       the reason separately.
 */
bool within_tolerance(double actual, double reference, const Tolerance &tolerance) noexcept;

/** @brief Adapter-specific check over one output buffer; returns true on success. */
using CustomCheck = std::function<bool(std::string_view, const std::vector<double> &)>;

/** @brief Controls applied to the validation phase. */
struct ValidationOptions {
  /** @brief Independently selected level: Basic, Standard or Strict. */
  ValidationLevel level{ValidationLevel::basic};
  /** @brief Allowances used by every reference comparison in this phase. */
  Tolerance tolerance{};
  /** @brief Adapter-specific check used only at the Strict level. */
  CustomCheck custom_check;
};

/** @brief Outcome of one validation check, including its method metadata. */
struct CheckOutcome {
  /** @brief Check identifier, such as `reference_comparison`. */
  std::string check;
  /** @brief Label of the @ref ztick::ValidationCase that produced the outcome. */
  std::string case_label;
  /** @brief Level that required this check. */
  ValidationLevel level{ValidationLevel::basic};
  /** @brief Outcome of the check. */
  CheckStatus status{CheckStatus::not_applicable};
  /** @brief Machine-readable reason, empty when the check passed. */
  std::string reason;
  /** @brief Observed error: largest absolute difference, or offending sample count. */
  double observed_error{0.0};
  /** @brief Allowances actually used, recorded even when the check is skipped. */
  Tolerance tolerance{};
  /** @brief Reference source actually used. */
  ReferenceSource reference_source{ReferenceSource::none};
  /** @brief Number of samples examined by the check. */
  std::size_t checked_samples{0};
};

/** @brief Per-check outcomes produced by one validation phase. */
struct ValidationReport {
  /** @brief Level that selected the executed checks. */
  ValidationLevel level{ValidationLevel::basic};
  /** @brief One outcome per executed check; skipped checks are still reported. */
  std::vector<CheckOutcome> outcomes;
  /** @brief Number of input cases examined at this level. */
  std::size_t cases_checked{0};

  /**
   * @brief Report whether every executed check passed.
   * @return True when no outcome is failed or unavailable; not-applicable checks
   *         are excluded because they were not required at this level.
   */
  [[nodiscard]] bool all_passed() const noexcept;

  /**
   * @brief Count failed checks.
   * @return Number of outcomes with status `failed`.
   */
  [[nodiscard]] std::size_t failed_count() const noexcept;
};

/**
 * @brief Check that every produced sample is finite (Basic).
 * @param input Case to check.
 * @param level Level requiring the check.
 * @return One outcome; an empty buffer produces `failed` with reason
 *         `empty_output`, and NaN or Inf samples produce `failed` with reason
 *         `non_finite_output`.
 */
CheckOutcome check_finite(const ValidationCase &input, ValidationLevel level);

/**
 * @brief Check that produced samples lie within configured bounds (Basic).
 * @param input Case to check.
 * @param level Level requiring the check.
 * @return One outcome; the check is `not_applicable` when no bounds are set.
 */
CheckOutcome check_range(const ValidationCase &input, ValidationLevel level);

/**
 * @brief Compare produced samples with a recorded reference (Standard).
 * @param input Case to check.
 * @param tolerance Allowances to apply and record.
 * @param level Level requiring the check.
 * @return One outcome recording the largest absolute difference and reference source;
 *         the check is `not_applicable` when no reference is available.
 */
CheckOutcome check_reference(const ValidationCase &input, const Tolerance &tolerance,
                             ValidationLevel level);

/**
 * @brief Check that an expected output change actually occurred (Standard).
 * @param input Case to check.
 * @param tolerance Allowances used to decide whether samples differ.
 * @param level Level requiring the check.
 * @return One outcome: `not_applicable` when no change is expected; otherwise
 *         `unavailable` with reason `no_reference` when no usable reference is
 *         supplied, `failed` when all samples remain within tolerance, and
 *         `passed` when a change is observed.
 */
CheckOutcome check_expected_change(const ValidationCase &input, const Tolerance &tolerance,
                                   ValidationLevel level);

/**
 * @brief Run an adapter-specific check over produced samples (Strict).
 * @param input Case to check.
 * @param custom_check Check to invoke; an empty check yields `not_applicable`.
 * @param level Level requiring the check.
 * @return One outcome with reason `custom_check_failed` when the check returns false.
 */
CheckOutcome check_custom(const ValidationCase &input, const CustomCheck &custom_check,
                          ValidationLevel level);

/**
 * @brief Execute the validation phase for one or more output cases.
 * @param inputs Cases to examine; Strict examines all, lower levels the first case.
 * @param options Level, tolerances and optional adapter check.
 * @param error Optional receiver for a human-readable configuration failure.
 * @return Per-check outcomes with recorded metadata, or `std::nullopt` when the
 *         tolerances are invalid or no cases are supplied.
 * @note This phase runs after the primary interval and performs no timing calls.
 */
std::optional<ValidationReport> run_validation(const std::vector<ValidationCase> &inputs,
                                               const ValidationOptions &options,
                                               std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_VALIDATION_HPP
