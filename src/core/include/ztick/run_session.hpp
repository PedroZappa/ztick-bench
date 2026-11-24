// SPDX-License-Identifier: MIT
/**
 * @file run_session.hpp
 * @brief Core orchestration of one config-driven benchmark session.
 *
 * ARCHITECTURE §2 assigns run orchestration to the core engine. This module
 * consumes an already-validated portable config, sequences every configured
 * repetition through the Phase 2 execution engine, computes the canonical
 * metrics and returns a complete result document. It deliberately does not read
 * files, collect platform inventory or generate timestamps: those front-end and
 * platform concerns are injected through @ref ztick::SessionContext so this
 * module stays independent of the CLI, TUI and OS-specific code.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_RUN_SESSION_HPP
#define ZTICK_RUN_SESSION_HPP

#include "ztick/config_value.hpp"
#include "ztick/execution.hpp"

#include <functional>
#include <optional>
#include <string>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/**
 * @brief Injectable host services needed to materialize a result document.
 * @note All callbacks must be non-null. The front end owns their implementation
 *       so the core depends on neither a clock source nor a platform inventory.
 */
struct SessionContext {
  /** @brief Monotonic clock returning elapsed seconds; used for every bracket. */
  Clock clock;
  /** @brief Provider of the availability-aware `machine` metadata object. */
  std::function<ConfigValue()> machine_metadata;
  /** @brief Generator of a non-empty session identifier. */
  std::function<std::string()> session_id;
  /** @brief Generator of a non-empty ISO-8601 UTC timestamp string. */
  std::function<std::string()> timestamp;
};

/**
 * @brief Validate one config document against the core execution planner.
 * @param config Portable config document that has passed schema validation.
 * @param error Optional receiver for a human-readable semantic failure explanation.
 * @return True when the core can build an execution plan from @p config.
 * @note This performs the same semantic checks as @ref run_configured_session
 *       before any timed work starts; it does not execute DSP code or require a
 *       @ref SessionContext. Front ends use it to reject edits the core cannot run.
 */
bool validate_run_config(const ConfigValue &config, std::string *error = nullptr);

/**
 * @brief Execute one validated config document and build its result.
 * @param config Portable config document that has passed schema validation.
 * @param context Injectable clock, metadata and identity services.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Complete result document on success, or `std::nullopt` when the config
 *         is semantically invalid or a core execution phase fails.
 * @note The returned document's `config_snapshot` is exactly @p config, so any
 *       override applied by the caller is visible in the frozen result.
 *       Validation runs after all timed intervals; a failed validation phase is
 *       recorded in the run and makes the session incomplete rather than
 *       discarding the measurements.
 */
std::optional<ConfigValue> run_configured_session(const ConfigValue &config,
                                                  const SessionContext &context,
                                                  std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_RUN_SESSION_HPP
