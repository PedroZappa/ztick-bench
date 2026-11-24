// SPDX-License-Identifier: MIT
/**
 * @file comparison.hpp
 * @brief In-memory comparison of two stored result documents.
 *
 * ARCHITECTURE §8 assigns comparison orchestration to the core. This module
 * validates two stored result documents, reuses the metadata comparison owned by
 * `ztick_results`, and produces the comparison shape described by
 * RESULTS_FORMAT §10. P4.8 reports raw metric differences only: threshold,
 * significance and regression classification are explicitly not applied and are
 * owned by the later P4.9 task. The result is not written to disk by the core.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_COMPARISON_HPP
#define ZTICK_COMPARISON_HPP

#include "ztick/config_value.hpp"

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
 *       `classification` is always `raw_difference`; P4.9 owns thresholds and
 *       significance. The core never writes the returned record to a file.
 */
[[nodiscard]] std::optional<ConfigValue>
compare_result_documents(const ConfigValue &baseline, const ConfigValue &candidate,
                         const ComparisonReferences &references, std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_COMPARISON_HPP
