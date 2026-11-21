// SPDX-License-Identifier: MIT
/**
 * @file execution.cpp
 * @brief Execution sequencing, timing-level selection and raw sample retention.
 */

#include "ztick/execution.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
 * @brief Test whether a level appears in a selection.
 * @param levels Selected levels to search.
 * @param level Level to look for.
 * @return True when @p level is present in @p levels.
 */
bool contains_level(const std::vector<TimingLevel> &levels, TimingLevel level) noexcept {
  return std::any_of(levels.begin(), levels.end(),
                     [level](TimingLevel candidate) { return candidate == level; });
}

/**
 * @brief Append one raw duration to the samples of a timing level.
 * @param samples Retained sample sets to update.
 * @param level Level whose set receives the sample.
 * @param duration_seconds Raw duration in seconds.
 * @note The level must already have a sample set in @p samples.
 */
void append_sample(std::vector<TimingSamples> &samples, TimingLevel level,
                   double duration_seconds) {
  const auto found =
      std::find_if(samples.begin(), samples.end(),
                   [level](const TimingSamples &entry) { return entry.level == level; });
  if (found != samples.end()) {
    found->durations_seconds.push_back(duration_seconds);
  }
}

} // namespace

// cppcheck-suppress unusedFunction
std::optional<TimingLevel> parse_timing_level(std::string_view token) noexcept {
  if (token == "per_block") {
    return TimingLevel::per_block;
  }
  if (token == "whole_run") {
    return TimingLevel::whole_run;
  }
  if (token == "end_to_end") {
    return TimingLevel::end_to_end;
  }
  return std::nullopt;
}

// cppcheck-suppress unusedFunction
std::string_view timing_level_name(TimingLevel level) noexcept {
  switch (level) {
  case TimingLevel::per_block:
    return "per_block";
  case TimingLevel::whole_run:
    return "whole_run";
  case TimingLevel::end_to_end:
    return "end_to_end";
  }
  return "unknown";
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
double PhaseMeasurement::duration_seconds() const noexcept { return end_seconds - start_seconds; }

// cppcheck-suppress unusedFunction
std::optional<ExecutionTrace> run_sequence(const SequenceActions &actions,
                                           const SequenceOptions &options, const Clock &clock,
                                           std::string *error) {
  if (!clock) {
    record_error(error, "clock is required");
    return std::nullopt;
  }

  const bool use_blocks = options.block_count > 0 && static_cast<bool>(actions.block);
  if (!use_blocks && !actions.primary) {
    record_error(error, "primary action is required");
    return std::nullopt;
  }
  const bool wants_per_block = contains_level(options.timing_levels, TimingLevel::per_block);
  if (wants_per_block && !use_blocks) {
    record_error(error, "per_block timing requires a block action and a positive block_count");
    return std::nullopt;
  }
  const bool wants_whole_run = contains_level(options.timing_levels, TimingLevel::whole_run);
  const bool wants_end_to_end = contains_level(options.timing_levels, TimingLevel::end_to_end);

  ExecutionTrace trace;
  for (const TimingLevel level : options.timing_levels) {
    trace.timing.push_back(TimingSamples{level, {}});
  }

  const double sequence_start = wants_end_to_end ? clock() : 0.0;

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

  const double primary_start = clock();
  trace.executed_phases.push_back(ExecutionPhase::primary);
  if (use_blocks) {
    for (std::uint32_t index = 0; index < options.block_count; ++index) {
      const double block_start = wants_per_block ? clock() : 0.0;
      actions.block(static_cast<std::size_t>(index));
      if (wants_per_block) {
        const double block_end = clock();
        append_sample(trace.timing, TimingLevel::per_block, block_end - block_start);
      }
    }
  } else {
    actions.primary();
  }
  const double primary_end = clock();

  trace.measurements.push_back(
      PhaseMeasurement{ExecutionPhase::primary, primary_start, primary_end});
  if (wants_whole_run) {
    append_sample(trace.timing, TimingLevel::whole_run, primary_end - primary_start);
  }
  if (wants_end_to_end) {
    const double sequence_end = clock();
    append_sample(trace.timing, TimingLevel::end_to_end, sequence_end - sequence_start);
  }

  if (actions.validation) {
    trace.executed_phases.push_back(ExecutionPhase::validation);
    actions.validation();
  }

  return trace;
}

// cppcheck-suppress unusedFunction
const std::vector<double> *find_samples(const ExecutionTrace &trace, TimingLevel level) noexcept {
  const auto found =
      std::find_if(trace.timing.begin(), trace.timing.end(),
                   [level](const TimingSamples &entry) { return entry.level == level; });
  if (found != trace.timing.end()) {
    return &found->durations_seconds;
  }
  return nullptr;
}

} // namespace ztick
