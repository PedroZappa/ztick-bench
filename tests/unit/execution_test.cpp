// SPDX-License-Identifier: MIT
/**
 * @file execution_test.cpp
 * @brief Catch2 tests for execution sequencing and the primary-interval boundary.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/execution.hpp>
#include <ztick/generator.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/** @brief Helpers and a deterministic fake clock for the execution tests. */
namespace {

/** @brief Mutable record of the phase actions that ran. */
struct PhaseLog {
  /** @brief Labels of phase invocations in execution order. */
  std::vector<std::string> calls;
};

/**
 * @brief Make an action that appends a label to a phase log.
 * @param log Log to append to.
 * @param label Label identifying the phase.
 * @return Callable recording one invocation of the phase.
 */
std::function<void()> record(PhaseLog &log, std::string label) {
  return [&log, label = std::move(label)]() { log.calls.push_back(label); };
}

/**
 * @brief Build a fake clock that advances by one second per call.
 * @param calls Receives the number of clock reads.
 * @return A monotonic clock suitable for deterministic interval assertions.
 */
ztick::Clock advancing_clock(int &calls) {
  return [&calls]() {
    ++calls;
    return static_cast<double>(calls);
  };
}

} // namespace

/**
 * @brief Verify interval duration is derived from the raw clock readings.
 */
TEST_CASE("PhaseMeasurement duration is end minus start", "[core][execution]") {
  const ztick::PhaseMeasurement measurement{ztick::ExecutionPhase::primary, 1.25, 3.75};
  REQUIRE(measurement.phase == ztick::ExecutionPhase::primary);
  REQUIRE(measurement.duration_seconds() == Catch::Approx(2.5));
}

/**
 * @brief Verify phase actions run in the documented lifecycle order.
 */
TEST_CASE("execution runs phases in lifecycle order", "[core][execution]") {
  PhaseLog log;
  ztick::SequenceActions actions;
  actions.generation = record(log, "generation");
  actions.setup = record(log, "setup");
  actions.warmup = record(log, "warmup");
  actions.primary = record(log, "primary");
  actions.validation = record(log, "validation");

  ztick::SequenceOptions options;
  options.warmup_iterations = 2;

  int clock_calls = 0;
  const auto trace = ztick::run_sequence(actions, options, advancing_clock(clock_calls));
  REQUIRE(trace.has_value());

  REQUIRE(log.calls == std::vector<std::string>{"generation", "setup", "warmup", "warmup",
                                                "primary", "validation"});
  REQUIRE(trace->executed_phases ==
          std::vector<ztick::ExecutionPhase>{
              ztick::ExecutionPhase::generation, ztick::ExecutionPhase::setup,
              ztick::ExecutionPhase::warmup, ztick::ExecutionPhase::warmup,
              ztick::ExecutionPhase::primary, ztick::ExecutionPhase::validation});
}

/**
 * @brief Verify only the primary interval is measured (P2.3 acceptance criterion).
 */
TEST_CASE("generation setup warmup and validation produce no measurements", "[core][execution]") {
  PhaseLog log;
  ztick::SequenceActions actions;
  actions.generation = record(log, "generation");
  actions.setup = record(log, "setup");
  actions.warmup = record(log, "warmup");
  actions.primary = record(log, "primary");
  actions.validation = record(log, "validation");

  ztick::SequenceOptions options;
  options.warmup_iterations = 3;

  int clock_calls = 0;
  const auto trace = ztick::run_sequence(actions, options, advancing_clock(clock_calls));
  REQUIRE(trace.has_value());

  // Only the primary interval reads the clock: generation, setup, warm-up and
  // validation are executed but never observed by the measurement timer.
  REQUIRE(clock_calls == 2);
  REQUIRE(trace->measurements.size() == 1);
  REQUIRE(trace->measurements[0].phase == ztick::ExecutionPhase::primary);
  REQUIRE(trace->measurements[0].start_seconds == Catch::Approx(1.0));
  REQUIRE(trace->measurements[0].end_seconds == Catch::Approx(2.0));
  REQUIRE(trace->measurements[0].duration_seconds() == Catch::Approx(1.0));

  REQUIRE(std::none_of(trace->measurements.begin(), trace->measurements.end(),
                       [](const ztick::PhaseMeasurement &measurement) {
                         return measurement.phase == ztick::ExecutionPhase::generation ||
                                measurement.phase == ztick::ExecutionPhase::setup ||
                                measurement.phase == ztick::ExecutionPhase::warmup ||
                                measurement.phase == ztick::ExecutionPhase::validation;
                       }));

  // End-to-end timing is opt-in and absent from the default selection.
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::end_to_end) == nullptr);
}

/**
 * @brief Verify generated input is produced outside the measured interval.
 */
TEST_CASE("generated input stays outside the measured interval", "[core][execution]") {
  const auto spec = ztick::GeneratorSpec::sine({1.0, 6000.0, 0.0});
  REQUIRE(spec.has_value());

  std::vector<double> generated;
  ztick::SequenceActions actions;
  actions.generation = [&generated, &spec]() {
    generated = ztick::generate_samples(*spec, 48000.0, 8);
  };
  actions.primary = []() {};

  int clock_calls = 0;
  const auto trace =
      ztick::run_sequence(actions, ztick::SequenceOptions{}, advancing_clock(clock_calls));
  REQUIRE(trace.has_value());

  REQUIRE(generated.size() == 8);
  REQUIRE(generated[0] == Catch::Approx(0.0));
  REQUIRE(generated[2] == Catch::Approx(1.0).margin(1e-9));
  REQUIRE(clock_calls == 2);
  REQUIRE(trace->executed_phases ==
          std::vector<ztick::ExecutionPhase>{ztick::ExecutionPhase::generation,
                                             ztick::ExecutionPhase::primary});
  REQUIRE(trace->measurements.size() == 1);
  REQUIRE(trace->measurements[0].phase == ztick::ExecutionPhase::primary);
}

/**
 * @brief Verify a zero warm-up count never invokes the warm-up action.
 */
TEST_CASE("zero warm-up iterations skip the warm-up action", "[core][execution]") {
  int warmup_calls = 0;
  ztick::SequenceActions actions;
  actions.warmup = [&warmup_calls]() { ++warmup_calls; };
  actions.primary = []() {};
  ztick::Clock clock = []() { return 0.0; };

  const auto trace = ztick::run_sequence(actions, ztick::SequenceOptions{}, clock);
  REQUIRE(trace.has_value());
  REQUIRE(warmup_calls == 0);
  REQUIRE(std::find(trace->executed_phases.begin(), trace->executed_phases.end(),
                    ztick::ExecutionPhase::warmup) == trace->executed_phases.end());
  REQUIRE(trace->measurements.size() == 1);
}

/**
 * @brief Verify absent optional phases are skipped without affecting measurement.
 */
TEST_CASE("optional phases may be omitted", "[core][execution]") {
  ztick::SequenceActions actions;
  actions.primary = []() {};
  ztick::Clock clock = []() { return 5.0; };

  const auto trace = ztick::run_sequence(actions, ztick::SequenceOptions{}, clock);
  REQUIRE(trace.has_value());
  REQUIRE(trace->executed_phases ==
          std::vector<ztick::ExecutionPhase>{ztick::ExecutionPhase::primary});
  REQUIRE(trace->measurements.size() == 1);
  REQUIRE(trace->measurements[0].start_seconds == Catch::Approx(5.0));
  REQUIRE(trace->measurements[0].end_seconds == Catch::Approx(5.0));
  REQUIRE(trace->measurements[0].duration_seconds() == Catch::Approx(0.0));
}

/**
 * @brief Verify missing primary action and clock are rejected with diagnostics.
 */
TEST_CASE("missing required inputs report errors", "[core][execution]") {
  ztick::SequenceActions actions;
  ztick::Clock clock = []() { return 0.0; };
  std::string error;

  const auto missing_primary =
      ztick::run_sequence(actions, ztick::SequenceOptions{}, clock, &error);
  REQUIRE_FALSE(missing_primary.has_value());
  REQUIRE(error == "primary action is required");

  actions.primary = []() {};
  ztick::Clock empty_clock;
  error.clear();
  const auto missing_clock =
      ztick::run_sequence(actions, ztick::SequenceOptions{}, empty_clock, &error);
  REQUIRE_FALSE(missing_clock.has_value());
  REQUIRE(error == "clock is required");

  error.clear();
  const auto ignored =
      ztick::run_sequence(ztick::SequenceActions{}, ztick::SequenceOptions{}, clock);
  REQUIRE_FALSE(ignored.has_value());
}

/**
 * @brief Verify timing level tokens parse and render symmetrically.
 */
TEST_CASE("timing level tokens round-trip", "[core][execution]") {
  REQUIRE(ztick::parse_timing_level("per_block") == ztick::TimingLevel::per_block);
  REQUIRE(ztick::parse_timing_level("whole_run") == ztick::TimingLevel::whole_run);
  REQUIRE(ztick::parse_timing_level("end_to_end") == ztick::TimingLevel::end_to_end);
  REQUIRE_FALSE(ztick::parse_timing_level("unknown").has_value());

  REQUIRE(ztick::timing_level_name(ztick::TimingLevel::per_block) == "per_block");
  REQUIRE(ztick::timing_level_name(ztick::TimingLevel::whole_run) == "whole_run");
  REQUIRE(ztick::timing_level_name(ztick::TimingLevel::end_to_end) == "end_to_end");
}

/**
 * @brief Verify whole-run is the default and retains one raw sample.
 */
TEST_CASE("whole-run timing is the default and retains one raw sample", "[core][execution]") {
  ztick::SequenceActions actions;
  actions.primary = []() {};

  int clock_calls = 0;
  const auto trace =
      ztick::run_sequence(actions, ztick::SequenceOptions{}, advancing_clock(clock_calls));
  REQUIRE(trace.has_value());

  const auto *samples = ztick::find_samples(*trace, ztick::TimingLevel::whole_run);
  REQUIRE(samples != nullptr);
  REQUIRE(samples->size() == 1);
  REQUIRE((*samples)[0] == Catch::Approx(trace->measurements[0].duration_seconds()));
  REQUIRE((*samples)[0] == Catch::Approx(1.0));
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::per_block) == nullptr);
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::end_to_end) == nullptr);
  REQUIRE(clock_calls == 2);
}

/**
 * @brief Verify per-block timing retains one raw sample per processed block.
 */
TEST_CASE("per-block timing retains one raw sample per block", "[core][execution]") {
  std::vector<std::size_t> indices;
  ztick::SequenceActions actions;
  actions.block = [&indices](std::size_t index) { indices.push_back(index); };

  ztick::SequenceOptions options;
  options.timing_levels = {ztick::TimingLevel::per_block};
  options.block_count = 3;

  int clock_calls = 0;
  const auto trace = ztick::run_sequence(actions, options, advancing_clock(clock_calls));
  REQUIRE(trace.has_value());

  REQUIRE(indices == std::vector<std::size_t>{0, 1, 2});
  const auto *samples = ztick::find_samples(*trace, ztick::TimingLevel::per_block);
  REQUIRE(samples != nullptr);
  REQUIRE(*samples == std::vector<double>{1.0, 1.0, 1.0});
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::whole_run) == nullptr);
  REQUIRE(clock_calls == 8);
  REQUIRE(trace->measurements.size() == 1);
}

/**
 * @brief Verify opt-in end-to-end timing spans generation through primary.
 */
TEST_CASE("end-to-end timing spans generation through primary", "[core][execution]") {
  PhaseLog log;
  ztick::SequenceActions actions;
  actions.generation = record(log, "generation");
  actions.setup = record(log, "setup");
  actions.warmup = record(log, "warmup");
  actions.primary = record(log, "primary");
  actions.validation = record(log, "validation");

  ztick::SequenceOptions options;
  options.warmup_iterations = 2;
  options.timing_levels = {ztick::TimingLevel::whole_run, ztick::TimingLevel::end_to_end};

  int clock_calls = 0;
  const auto trace = ztick::run_sequence(actions, options, advancing_clock(clock_calls));
  REQUIRE(trace.has_value());

  const auto *end_to_end = ztick::find_samples(*trace, ztick::TimingLevel::end_to_end);
  REQUIRE(end_to_end != nullptr);
  REQUIRE(end_to_end->size() == 1);
  // Clock reads: sequence start 1, primary start 2, primary end 3, sequence end 4.
  REQUIRE((*end_to_end)[0] == Catch::Approx(3.0));
  REQUIRE(clock_calls == 4);

  const auto *whole = ztick::find_samples(*trace, ztick::TimingLevel::whole_run);
  REQUIRE(whole != nullptr);
  REQUIRE(whole->size() == 1);
  REQUIRE((*whole)[0] == Catch::Approx(1.0));

  // Validation runs after the end-to-end bracket closes.
  REQUIRE(log.calls.back() == "validation");
}

/**
 * @brief Verify all timing levels can be selected together without duplication.
 */
TEST_CASE("all timing levels can be selected together", "[core][execution]") {
  ztick::SequenceActions actions;
  actions.block = [](std::size_t) {};

  ztick::SequenceOptions options;
  options.timing_levels = {ztick::TimingLevel::per_block, ztick::TimingLevel::whole_run,
                           ztick::TimingLevel::end_to_end};
  options.block_count = 2;

  const auto trace = ztick::run_sequence(actions, options, []() { return 0.0; });
  REQUIRE(trace.has_value());
  REQUIRE(trace->timing.size() == 3);
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::per_block)->size() == 2);
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::whole_run)->size() == 1);
  REQUIRE(ztick::find_samples(*trace, ztick::TimingLevel::end_to_end)->size() == 1);
}

/**
 * @brief Verify per-block timing is rejected without a usable block loop.
 */
TEST_CASE("per-block timing requires a block action and count", "[core][execution]") {
  ztick::SequenceActions actions;
  actions.primary = []() {};
  ztick::SequenceOptions options;
  options.timing_levels = {ztick::TimingLevel::per_block};
  ztick::Clock clock = []() { return 0.0; };
  std::string error;

  const auto no_block = ztick::run_sequence(actions, options, clock, &error);
  REQUIRE_FALSE(no_block.has_value());
  REQUIRE(error == "per_block timing requires a block action and a positive block_count");

  actions.block = [](std::size_t) {};
  options.block_count = 0;
  error.clear();
  const auto zero_count = ztick::run_sequence(actions, options, clock, &error);
  REQUIRE_FALSE(zero_count.has_value());
  REQUIRE(error == "per_block timing requires a block action and a positive block_count");
}

/**
 * @brief Verify a supplied block loop drives the primary interval.
 */
TEST_CASE("block action drives primary work when supplied", "[core][execution]") {
  int primary_calls = 0;
  int block_calls = 0;
  ztick::SequenceActions actions;
  actions.primary = [&primary_calls]() { ++primary_calls; };
  actions.block = [&block_calls](std::size_t) { ++block_calls; };

  ztick::SequenceOptions options;
  options.block_count = 4;

  const auto trace = ztick::run_sequence(actions, options, []() { return 0.0; });
  REQUIRE(trace.has_value());
  REQUIRE(primary_calls == 0);
  REQUIRE(block_calls == 4);
  REQUIRE(trace->executed_phases ==
          std::vector<ztick::ExecutionPhase>{ztick::ExecutionPhase::primary});
}
