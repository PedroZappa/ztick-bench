// SPDX-License-Identifier: MIT
/**
 * @file comparison.hpp
 * @brief In-memory comparison of two stored result documents.
 *
 * ARCHITECTURE §8 assigns comparison orchestration to the core. This module
 * validates two stored result documents, reuses the metadata comparison owned by
 * `ztick_results`, and produces the comparison shape described by
 * RESULTS_FORMAT §10. A raw comparison reports metric differences only; the
 * P4.9 entry point additionally applies configured thresholds and a deterministic
 * bootstrap significance check over run-level samples. The result is not written
 * to disk by the core. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_COMPARISON_HPP
#define ZTICK_COMPARISON_HPP

#include "ztick/config_value.hpp"

#include <cstdint>
#include <optional>
#include <string>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/**
 * @brief Human-readable references recorded in a comparison.
 * @note The core does not interpret these strings; the caller owns path and
 *       label syntax so this module stays independent of the CLI and file system.
 */
struct ComparisonReferences {
  /** @brief Reference text for the baseline document. */
  std::string baseline;
  /** @brief Reference text for the candidate document. */
  std::string candidate;
};

/** @brief Tail selection used by a comparison significance check. */
enum class SignificanceSidedness {
  /** @brief Use both tails of the bootstrap interval. */
  two_sided,
  /** @brief Use the tail matching the observed direction of change. */
  one_sided,
};

/**
 * @brief Policy for thresholded significance classification of a comparison.
 * @note All fields are validated by @ref compare_result_documents_with_policy.
 *       Bootstrap sampling uses a fixed in-process generator so results are
 *       reproducible for the same documents and policy.
 */
struct ComparisonPolicy {
  /** @brief Absolute relative-change percentage required before classification. */
  double threshold_percent{1.0};
  /** @brief Significance level in the open interval `(0, 1)`. */
  double alpha{0.05};
  /** @brief Tail selection for the bootstrap interval. */
  SignificanceSidedness sidedness{SignificanceSidedness::two_sided};
  /** @brief Number of deterministic bootstrap resamples; must be non-zero. */
  std::uint32_t bootstrap_iterations{1024U};
  /** @brief Non-zero xorshift seed; zero is replaced by the built-in default. */
  std::uint32_t seed{0x5EED1234U};
};

/**
 * @brief Compare two validated result documents without judging regressions.
 * @param baseline Baseline result document.
 * @param candidate Candidate result document.
 * @param references Caller-owned labels for the two documents.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Comparison record on success, or `std::nullopt` when either document
 *         is invalid or a computed value cannot be represented.
 * @note The record contains comparability warnings, raw metric deltas, explicit
 *       unmatched metrics, and copies of both documents' metadata context.
 *       `classification` is always `raw_difference`, and threshold, statistics
 *       and significance are explicitly not applicable. Use
 *       @ref compare_result_documents_with_policy for P4.9 classification. The
 *       core never writes the returned record to a file.
 */
[[nodiscard]] std::optional<ConfigValue>
compare_result_documents(const ConfigValue &baseline, const ConfigValue &candidate,
                         const ComparisonReferences &references, std::string *error = nullptr);

/**
 * @brief Compare two validated documents and classify thresholded regressions.
 * @param baseline Baseline result document.
 * @param candidate Candidate result document.
 * @param references Caller-owned labels for the two documents.
 * @param policy Validated regression threshold and significance policy.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Comparison record on success, or `std::nullopt` when a document or the
 *         policy is invalid, or when a computed value cannot be represented.
 * @note Run-level metric values from all repetitions form each sample set. The
 *       record retains those samples, the threshold decision, the bootstrap
 *       interval, per-metric polarity and the overall classification. Metrics
 *       without enough samples or without a known polarity stay explicitly
 *       not-applicable and are never reported as regressions. The core never
 *       writes the returned record to a file.
 */
[[nodiscard]] std::optional<ConfigValue>
compare_result_documents_with_policy(const ConfigValue &baseline, const ConfigValue &candidate,
                                     const ComparisonReferences &references,
                                     const ComparisonPolicy &policy, std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_COMPARISON_HPP
