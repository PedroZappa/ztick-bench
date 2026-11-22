// SPDX-License-Identifier: MIT
/**
 * @file phase2_engine_test.cpp
 * @brief End-to-end verification of the Phase 2 benchmark engine.
 *
 * This is the verification pass for ROADMAP P2.8. It wires the Phase 2
 * components together in the order of BENCHMARK §6: deterministic generation,
 * untimed setup and warm-up, primary measurement with all timing levels, then
 * validation. Every expected value is computed independently from the published
 * BENCHMARK §3, METRICS §3 and REQ-BENCH-07 definitions; nothing is derived from
 * the implementations under test. The exit criterion verified here is that
 * generators and timing levels work and metrics compute per METRICS.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/execution.hpp>
#include <ztick/generator.hpp>
#include <ztick/statistics.hpp>
#include <ztick/validation.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/** @brief Deterministic helpers for the Phase 2 verification pass. */
namespace {

/**
 * @brief Compute a sine sample independently from the BENCHMARK §3 formula.
 * @param amplitude Peak amplitude.
 * @param frequency_hz Frequency in Hz.
 * @param phase_rad Initial phase in radians.
 * @param sample_rate_hz Sample rate in Hz.
 * @param frame Frame index.
 * @return `amplitude * sin(2*pi*f*n/fs + phi)`.
 */
double reference_sine(double amplitude, double frequency_hz, double phase_rad,
                      double sample_rate_hz, std::size_t frame) {
  return amplitude * std::sin(2.0 * std::numbers::pi * frequency_hz * static_cast<double>(frame) /
                                  sample_rate_hz +
                              phase_rad);
}

/**
 * @brief Build a deterministic clock from scripted inter-read increments.
 * @note Each read returns the cumulative sum after adding the next increment,
 *       so the elapsed time between reads `a` and `b` is the sum of increments
 *       `a` through `b - 1` (zero-based). Work actions consume no clock reads.
 */
class ScriptedClock {
public:
  /**
   * @brief Construct a clock over a fixed increment sequence.
   * @param increments Time added on each successive read, in seconds.
   */
  explicit ScriptedClock(std::vector<double> increments) : increments_(std::move(increments)) {}

  /**
   * @brief Read the clock, advancing by the next scripted increment.
   * @return Cumulative elapsed seconds after the increment is applied.
   */
  double operator()() {
    if (index_ < increments_.size()) {
      elapsed_ += increments_[index_++];
    }
    return elapsed_;
  }

  /**
   * @brief Count clock reads performed so far.
   * @return Number of reads, used to assert that untimed phases perform none.
   */
  [[nodiscard]] std::size_t reads() const noexcept { return index_; }

private:
  /** @brief Scripted increments, one per read. */
  std::vector<double> increments_;
  /** @brief Index of the next increment to apply. */
  std::size_t index_{0};
  /** @brief Cumulative elapsed seconds. */
  double elapsed_{0.0};
};

} // namespace

/**
 * @brief Verify each generator matches the independent BENCHMARK §3 definition.
 */
TEST_CASE("phase2 generators match independent definitions", "[integration][phase2]") {
  constexpr std::size_t kFrames = 8;
  constexpr double kSampleRate = 48000.0;

  const auto sine = ztick::GeneratorSpec::sine({0.5, 1000.0, 0.25});
  REQUIRE(sine.has_value());
  const std::vector<double> sine_samples = ztick::generate_samples(*sine, kSampleRate, kFrames);
  REQUIRE(sine_samples.size() == kFrames);
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    REQUIRE(sine_samples[frame] ==
            Catch::Approx(reference_sine(0.5, 1000.0, 0.25, kSampleRate, frame)));
  }

  ztick::MultitoneParameters multitone_parameters;
  multitone_parameters.amplitude = 1.0;
  multitone_parameters.tones = {{1000.0, 0.0, 1.0}, {3000.0, 0.0, 1.0}};
  const auto multitone = ztick::GeneratorSpec::multitone(multitone_parameters);
  REQUIRE(multitone.has_value());
  const std::vector<double> multitone_samples =
      ztick::generate_samples(*multitone, kSampleRate, kFrames);
  REQUIRE(multitone_samples.size() == kFrames);
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    // Equal weights are normalised to 0.5 each by ztick::GeneratorSpec::multitone.
    const double reference = 0.5 * reference_sine(1.0, 1000.0, 0.0, kSampleRate, frame) +
                             0.5 * reference_sine(1.0, 3000.0, 0.0, kSampleRate, frame);
    REQUIRE(multitone_samples[frame] == Catch::Approx(reference));
  }

  const auto impulse = ztick::GeneratorSpec::impulse({0.75, 3});
  REQUIRE(impulse.has_value());
  const std::vector<double> impulse_samples =
      ztick::generate_samples(*impulse, kSampleRate, kFrames);
  REQUIRE(impulse_samples.size() == kFrames);
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    REQUIRE(impulse_samples[frame] == Catch::Approx(frame == 3 ? 0.75 : 0.0));
  }

  // BR §4.3: generation is deterministic, so the recorded definition reproduces
  // bit-identical input for a compared build.
  REQUIRE(ztick::generate_samples(*sine, kSampleRate, kFrames) == sine_samples);
  REQUIRE(ztick::generate_samples(*multitone, kSampleRate, kFrames) == multitone_samples);
}

/**
 * @brief Verify all timing levels retain raw samples and respect phase boundaries.
 */
TEST_CASE("phase2 timing levels retain raw samples outside untimed phases",
          "[integration][phase2]") {
  constexpr std::uint32_t kBlockCount = 4;
  // Increments map to clock reads: 1 sequence_start, 1 primary_start, 8 block
  // brackets, 1 primary_end, 1 sequence_end. Non-zero increments inside blocks
  // give per-block durations of 0.001..0.004 s; the leading 0.020 s covers the
  // untimed generation, setup and warm-up phases of the end-to-end interval.
  ScriptedClock clock(
      {0.000, 0.020, 0.000, 0.001, 0.000, 0.002, 0.000, 0.003, 0.000, 0.004, 0.005, 0.006});

  std::vector<ztick::ExecutionPhase> phases;
  ztick::ValidationReport validation;
  ztick::SequenceActions actions;
  actions.generation = [&phases]() { phases.push_back(ztick::ExecutionPhase::generation); };
  actions.setup = [&phases]() { phases.push_back(ztick::ExecutionPhase::setup); };
  actions.warmup = [&phases]() { phases.push_back(ztick::ExecutionPhase::warmup); };
  actions.block = [&phases](std::size_t index) {
    phases.push_back(ztick::ExecutionPhase::primary);
    REQUIRE(index < kBlockCount);
  };
  actions.validation = [&phases, &validation]() {
    phases.push_back(ztick::ExecutionPhase::validation);
    ztick::ValidationCase output;
    output.actual = {0.0, 0.5, 1.0};
    output.reference = {0.0, 0.5, 1.0};
    output.reference_source = ztick::ReferenceSource::deterministic_generation;
    const auto report = ztick::run_validation({output}, ztick::ValidationOptions{});
    REQUIRE(report.has_value());
    validation = *report;
  };

  ztick::OverheadEstimate overhead;
  overhead.status = ztick::ValueStatus::estimated;
  overhead.seconds = 0.001;
  overhead.method = "empty_interval";

  ztick::SequenceOptions options;
  options.warmup_iterations = 2;
  options.block_count = kBlockCount;
  options.timing_levels = {ztick::TimingLevel::per_block, ztick::TimingLevel::whole_run,
                           ztick::TimingLevel::end_to_end};
  options.overhead_estimate = overhead;

  std::string error;
  // Wrap by reference so the injected clock state is observable after the run;
  // passing the object directly would let std::function copy it.
  const auto trace = ztick::run_sequence(actions, options, [&clock]() { return clock(); }, &error);
  REQUIRE(trace.has_value());
  REQUIRE(error.empty());
  REQUIRE(validation.all_passed());

  const std::vector<ztick::ExecutionPhase> expected{
      ztick::ExecutionPhase::generation, ztick::ExecutionPhase::setup,
      ztick::ExecutionPhase::warmup,     ztick::ExecutionPhase::warmup,
      ztick::ExecutionPhase::primary,    ztick::ExecutionPhase::primary,
      ztick::ExecutionPhase::primary,    ztick::ExecutionPhase::primary,
      ztick::ExecutionPhase::validation};
  REQUIRE(phases == expected);

  // Untimed phases and validation never call the clock; only the 12 bracket
  // reads of the three levels are observed.
  REQUIRE(clock.reads() == 12U);

  const auto *per_block = ztick::find_samples(*trace, ztick::TimingLevel::per_block);
  const auto *whole_run = ztick::find_samples(*trace, ztick::TimingLevel::whole_run);
  const auto *end_to_end = ztick::find_samples(*trace, ztick::TimingLevel::end_to_end);
  REQUIRE(per_block != nullptr);
  REQUIRE(whole_run != nullptr);
  REQUIRE(end_to_end != nullptr);
  REQUIRE(per_block->size() == 4U);
  REQUIRE((*per_block)[0] == Catch::Approx(0.001));
  REQUIRE((*per_block)[1] == Catch::Approx(0.002));
  REQUIRE((*per_block)[2] == Catch::Approx(0.003));
  REQUIRE((*per_block)[3] == Catch::Approx(0.004));
  REQUIRE(whole_run->size() == 1U);
  REQUIRE((*whole_run)[0] == Catch::Approx(0.015));
  REQUIRE(end_to_end->size() == 1U);
  REQUIRE((*end_to_end)[0] == Catch::Approx(0.041));

  // The whole-run phase measurement uses the same primary bracket.
  REQUIRE(trace->measurements.size() == 1U);
  REQUIRE(trace->measurements[0].phase == ztick::ExecutionPhase::primary);
  REQUIRE(trace->measurements[0].duration_seconds() == Catch::Approx(0.015));

  // Overhead travels separately and is never subtracted from raw samples.
  REQUIRE(trace->overhead.status == ztick::ValueStatus::estimated);
  REQUIRE(trace->overhead.seconds == Catch::Approx(0.006)); // 1 + 4 + 1 intervals.
  REQUIRE(trace->overhead.dsp_time_seconds == Catch::Approx(0.015));
  REQUIRE((*whole_run)[0] == Catch::Approx(0.015));
}

/**
 * @brief Verify duration aggregates come from the retained raw basis per METRICS §3.
 */
TEST_CASE("phase2 duration statistics match METRICS reference values", "[integration][phase2]") {
  constexpr std::uint32_t kBlockCount = 4;
  // Whole-run only is not selected, so there is no sequence_start read. Clock
  // reads are primary_start, then start/end per block; block ends at increments
  // 2, 4, 6 and 8 give the per-block durations.
  ScriptedClock clock({0.000, 0.000, 0.001, 0.000, 0.002, 0.000, 0.003, 0.000, 0.004, 0.000});

  ztick::SequenceActions actions;
  actions.block = [](std::size_t) {};
  ztick::SequenceOptions options;
  options.block_count = kBlockCount;
  options.timing_levels = {ztick::TimingLevel::per_block};
  const auto trace = ztick::run_sequence(actions, options, clock);
  REQUIRE(trace.has_value());
  const auto *samples = ztick::find_samples(*trace, ztick::TimingLevel::per_block);
  REQUIRE(samples != nullptr);

  const auto stats = ztick::compute_duration_statistics(*samples, ztick::RawBasis::per_block);
  REQUIRE(stats.has_value());
  REQUIRE(stats->basis == ztick::RawBasis::per_block);
  REQUIRE(stats->sample_count == 4U);
  REQUIRE(stats->percentile_method == "nearest_rank_ceil");

  // Population {0.001, 0.002, 0.003, 0.004}: mean and median are 0.0025,
  // sample stddev is sqrt(5e-6 / 3), and ceil(q * 4) = 4 for every reported
  // percentile, so each nearest-rank value is the observed maximum 0.004.
  REQUIRE(stats->mean.value == Catch::Approx(0.0025));
  REQUIRE(stats->median.value == Catch::Approx(0.0025));
  REQUIRE(stats->minimum.value == Catch::Approx(0.001));
  REQUIRE(stats->maximum.value == Catch::Approx(0.004));
  REQUIRE(stats->standard_deviation.value == Catch::Approx(std::sqrt(5.0e-6 / 3.0)));
  REQUIRE(stats->p90.value == Catch::Approx(0.004));
  REQUIRE(stats->p95.value == Catch::Approx(0.004));
  REQUIRE(stats->p99.value == Catch::Approx(0.004));
  REQUIRE(stats->p99_9.value == Catch::Approx(0.004));
  REQUIRE(stats->mean.status == ztick::ValueStatus::measured);
}

/**
 * @brief Verify throughput, real-time factor and scaling match METRICS §3.
 */
TEST_CASE("phase2 performance metrics match METRICS reference values", "[integration][phase2]") {
  constexpr std::uint32_t kBlockCount = 4;
  constexpr std::uint32_t kFramesPerBlock = 128;
  constexpr std::uint32_t kChannels = 2;
  constexpr double kSampleRate = 48000.0;
  // Whole-run timing brackets one primary interval: primary_start at 0.000 s
  // and primary_end at 0.015 s.
  ScriptedClock clock({0.000, 0.015});

  ztick::SequenceActions actions;
  actions.primary = []() {};
  ztick::SequenceOptions options;
  options.timing_levels = {ztick::TimingLevel::whole_run};
  const auto trace = ztick::run_sequence(actions, options, clock);
  REQUIRE(trace.has_value());
  const auto *whole_run = ztick::find_samples(*trace, ztick::TimingLevel::whole_run);
  REQUIRE(whole_run != nullptr);
  REQUIRE(whole_run->size() == 1U);

  const std::uint64_t frames = static_cast<std::uint64_t>(kBlockCount) * kFramesPerBlock; // 512.
  const double processing_seconds = (*whole_run)[0]; // 0.015 s.
  const auto metrics = ztick::compute_performance_metrics(frames, processing_seconds, kChannels,
                                                          kFramesPerBlock, kSampleRate);
  REQUIRE(metrics.has_value());

  // 512 frames / 0.015 s = 34133.33 fps; samples/s = fps * 2; blocks/s = fps / 128.
  REQUIRE(metrics->throughput_fps.value == Catch::Approx(512.0 / 0.015));
  REQUIRE(metrics->throughput_sps.value == Catch::Approx(2.0 * 512.0 / 0.015));
  REQUIRE(metrics->throughput_bps.value == Catch::Approx(512.0 / 0.015 / 128.0));
  // Audio duration = 512 / 48000 s; RTF = 0.015 / audio_duration = 1.40625.
  REQUIRE(metrics->audio_duration.value == Catch::Approx(512.0 / 48000.0));
  REQUIRE(metrics->real_time_factor.value == Catch::Approx(1.40625));
  REQUIRE(metrics->throughput_fps.status == ztick::ValueStatus::measured);

  // Partitioned work: E(2) = T(1) / (2 * T(2)) = 0.020 / 0.030 = 0.6667.
  const auto scaling = ztick::compute_scaling_metrics(0.020, processing_seconds, 2,
                                                      ztick::ParallelWork::partitioned);
  REQUIRE(scaling.has_value());
  REQUIRE(scaling->efficiency.value == Catch::Approx(0.020 / (2.0 * 0.015)));
  REQUIRE(scaling->efficiency.status == ztick::ValueStatus::estimated);
  REQUIRE(scaling->formula == "wall_time");

  // Replicated work must not be reported as scaling.
  const auto replicated =
      ztick::compute_scaling_metrics(0.020, processing_seconds, 2, ztick::ParallelWork::replicated);
  REQUIRE(replicated.has_value());
  REQUIRE(replicated->efficiency.status == ztick::ValueStatus::not_applicable);
}
