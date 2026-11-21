// SPDX-License-Identifier: MIT
/**
 * @file execution.cpp
 * @brief Execution sequencing implementation and timing-boundary enforcement.
 */

#include "ztick/execution.hpp"

#include <cstdint>
#include <string>
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

} // namespace

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
double PhaseMeasurement::duration_seconds() const noexcept { return end_seconds - start_seconds; }

// cppcheck-suppress unusedFunction
std::optional<ExecutionTrace> run_sequence(const SequenceActions &actions,
                                           const SequenceOptions &options, const Clock &clock,
                                           std::string *error) {
  if (!actions.primary) {
    record_error(error, "primary action is required");
    return std::nullopt;
  }
  if (!clock) {
    record_error(error, "clock is required");
    return std::nullopt;
  }

  ExecutionTrace trace;

  if (actions.generation) {
    trace.executed_phases.push_back(ExecutionPhase::generation);
    actions.generation();
  }
  if (actions.setup) {
    trace.executed_phases.push_back(ExecutionPhase::setup);
    actions.setup();
  }
  if (actions.warmup) {
    for (std::uint32_t iteration = 0; iteration < options.warmup_iterations; ++iteration) {
      trace.executed_phases.push_back(ExecutionPhase::warmup);
      actions.warmup();
    }
  }

  trace.executed_phases.push_back(ExecutionPhase::primary);
  const double start_seconds = clock();
  actions.primary();
  const double end_seconds = clock();
  trace.measurements.push_back(
      PhaseMeasurement{ExecutionPhase::primary, start_seconds, end_seconds});

  if (actions.validation) {
    trace.executed_phases.push_back(ExecutionPhase::validation);
    actions.validation();
  }

  return trace;
}

} // namespace ztick
