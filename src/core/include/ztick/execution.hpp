// SPDX-License-Identifier: MIT
/**
 * @file execution.hpp
 * @brief Execution sequencing for one benchmark test point and its timing boundary.
 *
 * BENCHMARK_SPECIFICATION §6 fixes the lifecycle of a test point: generation,
 * setup and warm-up run before the primary DSP interval, and validation runs
 * after it. REQ-BENCH-06 requires those surrounding phases to stay outside the
 * primary measurement unless end-to-end timing is explicitly selected. This
 * module sequences the phases and records a measurement only for the primary
 * interval, making the boundary an observable property of a run.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_EXECUTION_HPP
#define ZTICK_EXECUTION_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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

/** @brief Phase actions for one test point; absent actions are skipped. */
struct SequenceActions {
  /** @brief Input generation action, typically a `generate_samples` call; untimed. */
  std::function<void()> generation;
  /** @brief Setup action such as buffer allocation; untimed. */
  std::function<void()> setup;
  /** @brief One warm-up iteration, repeated by @ref ztick::SequenceOptions; untimed. */
  std::function<void()> warmup;
  /** @brief Primary DSP work; the required action bracketed by the measurement timer. */
  std::function<void()> primary;
  /** @brief Correctness validation action; untimed. */
  std::function<void()> validation;
};

/** @brief Controls that affect sequencing without entering the measured interval. */
struct SequenceOptions {
  /** @brief Number of times to run the warm-up action before the primary interval. */
  std::uint32_t warmup_iterations{0};
};

/** @brief Observable result of one sequenced test point. */
struct ExecutionTrace {
  /** @brief Phases whose actions ran, in order; warm-up appears once per iteration. */
  std::vector<ExecutionPhase> executed_phases;
  /** @brief Recorded intervals; the primary interval is the only entry by default. */
  std::vector<PhaseMeasurement> measurements;
};

/** @brief Monotonic clock returning elapsed seconds; injectable for deterministic tests. */
using Clock = std::function<double()>;

/**
 * @brief Run one test point, measuring only the primary DSP interval.
 * @param actions Phase actions to invoke in lifecycle order.
 * @param options Sequencing controls such as the warm-up iteration count.
 * @param clock Monotonic clock used only to bracket the primary interval.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return The execution trace, or `std::nullopt` when a required input is missing.
 * @note generation, setup, warm-up and validation actions run but never call
 *       @p clock, so no measurement is recorded for those phases. Raw clock
 *       readings are retained in @ref ztick::PhaseMeasurement.
 */
std::optional<ExecutionTrace> run_sequence(const SequenceActions &actions,
                                           const SequenceOptions &options, const Clock &clock,
                                           std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_EXECUTION_HPP
