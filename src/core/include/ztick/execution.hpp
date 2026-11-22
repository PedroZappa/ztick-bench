// SPDX-License-Identifier: MIT
/**
 * @file execution.hpp
 * @brief Execution sequencing, timing levels and raw timing retention.
 *
 * BENCHMARK_SPECIFICATION §6 fixes the lifecycle of a test point: generation,
 * setup and warm-up run before the primary DSP interval, and validation runs
 * after it. REQ-BENCH-06 requires those surrounding phases to stay outside the
 * primary measurement unless end-to-end timing is explicitly selected. This
 * module sequences the phases, records a phase-attributed primary measurement
 * and retains raw durations for the selectable levels of REQ-BENCH-07.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_EXECUTION_HPP
#define ZTICK_EXECUTION_HPP

#include "ztick/metric_status.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztick {

/** @brief Ordered lifecycle phases of one benchmark test point (BENCHMARK §6). */
enum class ExecutionPhase : std::uint8_t {
  /** @brief Input signal generation; always outside the primary interval. */
  generation = 0,
  /** @brief Buffer allocation and reference attachment; outside the primary interval. */
  setup = 1,
  /** @brief Profile-dependent warm-up iterations; outside the primary interval. */
  warmup = 2,
  /** @brief Primary DSP interval; the only phase measured by default. */
  primary = 3,
  /** @brief Correctness validation; outside the primary interval. */
  validation = 4,
};

/** @brief Selectable timing levels for one test point (REQ-BENCH-07). */
enum class TimingLevel : std::uint8_t {
  /** @brief One raw duration per processed block; opt-in detailed timing. */
  per_block = 0,
  /** @brief One raw duration spanning the primary DSP interval; the default. */
  whole_run = 1,
  /** @brief One raw duration spanning generation through primary; opt-in. */
  end_to_end = 2,
};

/**
 * @brief Parse a config token into a @ref ztick::TimingLevel.
 * @param token Token from the config document, such as `whole_run`.
 * @return The matching level, or `std::nullopt` for an unknown token.
 */
std::optional<TimingLevel> parse_timing_level(std::string_view token) noexcept;

/**
 * @brief Render a @ref ztick::TimingLevel as its config token.
 * @param level Level to render.
 * @return Stable token used by the config document.
 */
std::string_view timing_level_name(TimingLevel level) noexcept;

/** @brief One recorded time interval and the phase it belongs to. */
struct PhaseMeasurement {
  /** @brief Phase that was timed. */
  ExecutionPhase phase{ExecutionPhase::primary};
  /** @brief Raw clock reading at interval start, in seconds. */
  double start_seconds{0.0};
  /** @brief Raw clock reading at interval end, in seconds. */
  double end_seconds{0.0};

  /**
   * @brief Compute the elapsed duration of this interval.
   * @return @ref ztick::PhaseMeasurement::end_seconds minus
   *         @ref ztick::PhaseMeasurement::start_seconds, in seconds.
   * @note This is a derived value; the raw clock readings are retained unchanged.
   */
  [[nodiscard]] double duration_seconds() const noexcept;
};

/** @brief Retained raw durations for one timing level (REQ-MET-03). */
struct TimingSamples {
  /** @brief Level these samples were collected at. */
  TimingLevel level{TimingLevel::whole_run};
  /** @brief Raw durations in seconds, in collection order; never aggregated in place. */
  std::vector<double> durations_seconds;
};

/**
 * @brief Separately preserved measurement-overhead estimate (MET-17).
 * @note Overhead is never subtracted from raw timing samples. The estimate is
 *       reported alongside them and remains independently observable (BR §7.3).
 */
struct OverheadEstimate {
  /** @brief Availability status; `estimated` once a calibration has run. */
  ValueStatus status{ValueStatus::unavailable};
  /** @brief Estimated overhead in seconds across @ref interval_count intervals. */
  double seconds{0.0};
  /** @brief DSP processing seconds used as the percentage denominator. */
  double dsp_time_seconds{0.0};
  /** @brief Number of bracketed intervals the estimate covers. */
  std::size_t interval_count{0};
  /** @brief Raw empty-interval calibration durations in seconds. */
  std::vector<double> calibration_samples_seconds;
  /** @brief Human-readable derivation method, or `unavailable` when not estimated. */
  std::string method{"unavailable"};
};

/** @brief Phase actions for one test point; absent actions are skipped. */
struct SequenceActions {
  /** @brief Input generation action, typically a `generate_samples` call; untimed. */
  std::function<void()> generation;
  /** @brief Setup action such as buffer allocation; untimed. */
  std::function<void()> setup;
  /** @brief One warm-up iteration, repeated by @ref ztick::SequenceOptions; untimed. */
  std::function<void()> warmup;
  /** @brief Primary DSP work used when no block action is supplied. */
  std::function<void()> primary;
  /** @brief Process one block, receiving its zero-based index; used for per-block timing. */
  std::function<void(std::size_t)> block;
  /** @brief Correctness validation action; untimed. */
  std::function<void()> validation;
};

/** @brief Controls that affect sequencing and timing selection. */
struct SequenceOptions {
  /** @brief Number of times to run the warm-up action before the primary interval. */
  std::uint32_t warmup_iterations{0};
  /** @brief Levels to collect; whole-run aggregate only by default (REQ-BENCH-08). */
  std::vector<TimingLevel> timing_levels{TimingLevel::whole_run};
  /** @brief Number of block invocations when a @ref ztick::SequenceActions::block is used. */
  std::uint32_t block_count{0};
  /** @brief Calibrated overhead estimate carried into the trace; unavailable by default. */
  OverheadEstimate overhead_estimate{};
};

/** @brief Observable result of one sequenced test point. */
struct ExecutionTrace {
  /** @brief Phases whose actions ran, in order; warm-up appears once per iteration. */
  std::vector<ExecutionPhase> executed_phases;
  /** @brief Phase-attributed primary interval; empty when no primary work ran. */
  std::vector<PhaseMeasurement> measurements;
  /** @brief Raw duration samples keyed by level; only selected levels are present. */
  std::vector<TimingSamples> timing;
  /** @brief Overhead estimate reported separately; never applied to @ref timing. */
  OverheadEstimate overhead;
};

/** @brief Monotonic clock returning elapsed seconds; injectable for deterministic tests. */
using Clock = std::function<double()>;

/**
 * @brief Run one test point with the selected timing levels.
 * @param actions Phase actions to invoke in lifecycle order.
 * @param options Sequencing controls such as warm-up count and timing levels.
 * @param clock Monotonic clock used to bracket the primary and selected intervals.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return The execution trace with retained raw samples, or `std::nullopt` on error.
 * @note Generation, setup and warm-up are bracketed only by the opt-in
 *       end-to-end level in @ref ztick::SequenceOptions::timing_levels.
 *       Validation is never bracketed by any timing level, including
 *       end-to-end: it runs after all timed intervals are closed (BR §7.3).
 *       Whole-run uses the same bracket as
 *       @ref ztick::ExecutionTrace::measurements. Per-block requires a block
 *       action and a positive @ref ztick::SequenceOptions::block_count.
 */
std::optional<ExecutionTrace> run_sequence(const SequenceActions &actions,
                                           const SequenceOptions &options, const Clock &clock,
                                           std::string *error = nullptr);

/**
 * @brief Find the retained raw durations for a timing level.
 * @param trace Trace to search.
 * @param level Level whose samples are requested.
 * @return Pointer to the retained durations, or `nullptr` when the level was not collected.
 * @note The returned pointer refers to storage owned by @p trace and is valid while
 *       @p trace lives.
 */
const std::vector<double> *find_samples(const ExecutionTrace &trace, TimingLevel level) noexcept;

/**
 * @brief Estimate clock overhead from repeated empty intervals.
 * @param clock Monotonic clock to calibrate.
 * @param sample_count Number of empty intervals to measure.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return An estimate with status `estimated` on success, or `unavailable` otherwise.
 * @note Each sample measures two consecutive clock reads with no work between,
 *       isolating the clock's intrinsic cost. Raw calibration durations are retained.
 */
OverheadEstimate estimate_overhead(const Clock &clock, std::size_t sample_count,
                                   std::string *error = nullptr);

/**
 * @brief Compute estimated overhead as a percentage of DSP processing time.
 * @param estimate Estimate to convert.
 * @return The percentage, or `std::nullopt` when the estimate is unavailable or
 *         the DSP-time denominator is not positive.
 * @note This is a derived reporting value; it never alters raw measurements.
 */
std::optional<double> overhead_percent(const OverheadEstimate &estimate) noexcept;

} // namespace ztick

#endif // ZTICK_EXECUTION_HPP
