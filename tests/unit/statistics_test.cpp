// SPDX-License-Identifier: MIT
/**
 * @file statistics_test.cpp
 * @brief Catch2 tests for the metric formulas in METRICS_REFERENCE §3.
 *
 * Every expected value below is computed independently by hand from the
 * published formula; tests never derive an expectation from the implementation.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/statistics.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/**
 * @brief Verify metadata tokens for basis, percentile and work arrangement.
 */
TEST_CASE("statistics metadata tokens are stable", "[core][statistics]") {
  REQUIRE(ztick::raw_basis_name(ztick::RawBasis::per_block) == "per_block");
  REQUIRE(ztick::raw_basis_name(ztick::RawBasis::run_repetition) == "run_repetition");

  REQUIRE(ztick::percentile_name(ztick::Percentile::p90) == "P90");
  REQUIRE(ztick::percentile_name(ztick::Percentile::p95) == "P95");
  REQUIRE(ztick::percentile_name(ztick::Percentile::p99) == "P99");
  REQUIRE(ztick::percentile_name(ztick::Percentile::p99_9) == "P99.9");
  REQUIRE(ztick::percentile_quantile(ztick::Percentile::p90) == Catch::Approx(0.90));
  REQUIRE(ztick::percentile_quantile(ztick::Percentile::p95) == Catch::Approx(0.95));
  REQUIRE(ztick::percentile_quantile(ztick::Percentile::p99) == Catch::Approx(0.99));
  REQUIRE(ztick::percentile_quantile(ztick::Percentile::p99_9) == Catch::Approx(0.999));

  REQUIRE(ztick::parallel_work_name(ztick::ParallelWork::partitioned) == "partitioned");
  REQUIRE(ztick::parallel_work_name(ztick::ParallelWork::replicated) == "replicated");
}

/**
 * @brief Verify all duration aggregates against manually computed values.
 */
TEST_CASE("duration aggregates match independent reference values", "[core][statistics]") {
  const std::vector<double> samples{4.0, 1.0, 3.0, 2.0};
  const std::vector<double> original = samples;
  const auto statistics = ztick::compute_duration_statistics(samples, ztick::RawBasis::per_block);
  REQUIRE(statistics.has_value());

  REQUIRE(samples == original);
  REQUIRE(statistics->basis == ztick::RawBasis::per_block);
  REQUIRE(statistics->sample_count == 4U);
  REQUIRE(statistics->percentile_method == "nearest_rank_ceil");

  REQUIRE(statistics->mean.status == ztick::ValueStatus::measured);
  REQUIRE(statistics->mean.value == Catch::Approx(2.5));
  REQUIRE(statistics->median.status == ztick::ValueStatus::measured);
  // Sorted population is 1, 2, 3, 4; median = (2 + 3) / 2.
  REQUIRE(statistics->median.value == Catch::Approx(2.5));
  REQUIRE(statistics->minimum.value == Catch::Approx(1.0));
  REQUIRE(statistics->maximum.value == Catch::Approx(4.0));

  // Sample standard deviation = sqrt(((1-2.5)^2 + (2-2.5)^2 + (3-2.5)^2 + (4-2.5)^2) / 3)
  // = sqrt(5 / 3).
  REQUIRE(statistics->standard_deviation.status == ztick::ValueStatus::measured);
  REQUIRE(statistics->standard_deviation.value == Catch::Approx(std::sqrt(5.0 / 3.0)));

  // ceil(0.9 * 4) = ceil(0.95 * 4) = ceil(0.99 * 4) = ceil(0.999 * 4) = 4.
  REQUIRE(statistics->p90.value == Catch::Approx(4.0));
  REQUIRE(statistics->p95.value == Catch::Approx(4.0));
  REQUIRE(statistics->p99.value == Catch::Approx(4.0));
  REQUIRE(statistics->p99_9.value == Catch::Approx(4.0));
}

/**
 * @brief Verify odd-population median and small-population percentiles.
 */
TEST_CASE("median and percentiles use order statistics", "[core][statistics]") {
  const std::vector<double> samples{3.0, 1.0, 2.0};
  REQUIRE(ztick::median_seconds(samples) == Catch::Approx(2.0));
  REQUIRE(ztick::mean_seconds(samples) == Catch::Approx(2.0));
  REQUIRE(ztick::sample_stddev_seconds(samples) == Catch::Approx(1.0));
  // ceil(0.9 * 3) = 3, selecting the largest observed sample.
  REQUIRE(ztick::percentile_seconds(samples, ztick::Percentile::p90) == Catch::Approx(3.0));
  REQUIRE(ztick::percentile_seconds(samples, ztick::Percentile::p99_9) == Catch::Approx(3.0));
}

/**
 * @brief Verify a single sample yields a not-applicable standard deviation.
 */
TEST_CASE("single sample has no standard deviation", "[core][statistics]") {
  const auto statistics =
      ztick::compute_duration_statistics({7.0}, ztick::RawBasis::run_repetition);
  REQUIRE(statistics.has_value());
  REQUIRE(statistics->basis == ztick::RawBasis::run_repetition);
  REQUIRE(statistics->sample_count == 1U);
  REQUIRE(statistics->mean.value == Catch::Approx(7.0));
  REQUIRE(statistics->median.value == Catch::Approx(7.0));
  REQUIRE(statistics->standard_deviation.status == ztick::ValueStatus::not_applicable);
  REQUIRE(statistics->standard_deviation.reason == "insufficient_samples");
  REQUIRE(statistics->p99_9.value == Catch::Approx(7.0));
  REQUIRE_FALSE(ztick::sample_stddev_seconds({7.0}).has_value());
}

/**
 * @brief Verify empty and non-finite populations are rejected with diagnostics.
 */
TEST_CASE("invalid duration populations are rejected", "[core][statistics]") {
  std::string error;
  REQUIRE_FALSE(
      ztick::compute_duration_statistics({}, ztick::RawBasis::per_block, &error).has_value());
  REQUIRE(error == "empty sample population");

  error.clear();
  const std::vector<double> with_nan{1.0, std::numeric_limits<double>::quiet_NaN()};
  REQUIRE_FALSE(
      ztick::compute_duration_statistics(with_nan, ztick::RawBasis::per_block, &error).has_value());
  REQUIRE(error == "sample durations must be finite");

  error.clear();
  const std::vector<double> with_inf{1.0, std::numeric_limits<double>::infinity()};
  REQUIRE_FALSE(
      ztick::compute_duration_statistics(with_inf, ztick::RawBasis::per_block, &error).has_value());
  REQUIRE(error == "sample durations must be finite");

  REQUIRE_FALSE(ztick::mean_seconds({}).has_value());
  REQUIRE_FALSE(ztick::median_seconds({}).has_value());
  REQUIRE_FALSE(ztick::minimum_seconds({}).has_value());
  REQUIRE_FALSE(ztick::maximum_seconds({}).has_value());
  REQUIRE_FALSE(ztick::nearest_rank_quantile_seconds({}, 0.5).has_value());
}

/**
 * @brief Verify nearest-rank quantiles return observed samples without interpolation.
 */
TEST_CASE("nearest-rank quantiles select observed samples", "[core][statistics]") {
  const std::vector<double> samples{10.0, 20.0, 30.0, 40.0};
  // Median averages the middle pair, but nearest-rank P50 selects rank ceil(0.5*4)=2.
  REQUIRE(ztick::median_seconds(samples) == Catch::Approx(25.0));
  REQUIRE(ztick::nearest_rank_quantile_seconds(samples, 0.5) == Catch::Approx(20.0));
  REQUIRE(ztick::nearest_rank_quantile_seconds(samples, 0.25) == Catch::Approx(10.0));
  REQUIRE(ztick::nearest_rank_quantile_seconds(samples, 1.0) == Catch::Approx(40.0));
  REQUIRE(ztick::nearest_rank_quantile_seconds(samples, 0.01) == Catch::Approx(10.0));

  REQUIRE_FALSE(ztick::nearest_rank_quantile_seconds(samples, 0.0).has_value());
  REQUIRE_FALSE(ztick::nearest_rank_quantile_seconds(samples, -0.1).has_value());
  REQUIRE_FALSE(ztick::nearest_rank_quantile_seconds(samples, 1.1).has_value());
  REQUIRE_FALSE(
      ztick::nearest_rank_quantile_seconds(samples, std::numeric_limits<double>::quiet_NaN())
          .has_value());
}

/**
 * @brief Verify frame, sample and block throughput formulas.
 */
TEST_CASE("throughput rates match independent calculations", "[core][statistics]") {
  // 48000 frames in 0.5 s = 96000 frames/s; x2 channels = 192000 samples/s;
  // /128 frames per block = 750 blocks/s.
  REQUIRE(ztick::throughput_frames_per_second(48000U, 0.5) == Catch::Approx(96000.0));
  REQUIRE(ztick::throughput_samples_per_second(96000.0, 2U) == Catch::Approx(192000.0));
  REQUIRE(ztick::throughput_blocks_per_second(96000.0, 128U) == Catch::Approx(750.0));
}

/**
 * @brief Verify throughput functions reject undefined inputs.
 */
TEST_CASE("throughput rejects invalid inputs", "[core][statistics]") {
  REQUIRE_FALSE(ztick::throughput_frames_per_second(0U, 0.5).has_value());
  REQUIRE_FALSE(ztick::throughput_frames_per_second(48000U, 0.0).has_value());
  REQUIRE_FALSE(ztick::throughput_frames_per_second(48000U, -0.5).has_value());
  REQUIRE_FALSE(
      ztick::throughput_frames_per_second(48000U, std::numeric_limits<double>::quiet_NaN())
          .has_value());
  REQUIRE_FALSE(ztick::throughput_samples_per_second(96000.0, 0U).has_value());
  REQUIRE_FALSE(ztick::throughput_blocks_per_second(96000.0, 0U).has_value());
  REQUIRE_FALSE(ztick::throughput_samples_per_second(0.0, 2U).has_value());
}

/**
 * @brief Verify audio duration and real-time factor formulas and their equivalence.
 */
TEST_CASE("audio duration and real-time factor match independent values", "[core][statistics]") {
  // 48000 frames at 48000 Hz represent exactly one second of audio.
  const auto duration = ztick::audio_duration_seconds(48000U, 48000.0);
  REQUIRE(duration == Catch::Approx(1.0));
  REQUIRE(ztick::real_time_factor(0.25, *duration) == Catch::Approx(0.25));

  // Throughput is 48000 / 0.25 = 192000 frames/s; sample_rate / throughput = 0.25.
  const auto frames_per_second = ztick::throughput_frames_per_second(48000U, 0.25);
  REQUIRE(frames_per_second == Catch::Approx(192000.0));
  REQUIRE(48000.0 / *frames_per_second == Catch::Approx(0.25));
}

/**
 * @brief Verify audio duration and RTF reject undefined inputs.
 */
TEST_CASE("audio duration and real-time factor reject invalid inputs", "[core][statistics]") {
  REQUIRE_FALSE(ztick::audio_duration_seconds(0U, 48000.0).has_value());
  REQUIRE_FALSE(ztick::audio_duration_seconds(48000U, 0.0).has_value());
  REQUIRE_FALSE(ztick::audio_duration_seconds(48000U, -48000.0).has_value());
  REQUIRE_FALSE(ztick::real_time_factor(-1.0, 1.0).has_value());
  REQUIRE_FALSE(ztick::real_time_factor(1.0, 0.0).has_value());
  REQUIRE(ztick::real_time_factor(0.0, 1.0) == Catch::Approx(0.0));
}

/**
 * @brief Verify performance metrics carry per-field statuses.
 */
TEST_CASE("performance metrics record availability per field", "[core][statistics]") {
  const auto partial = ztick::compute_performance_metrics(48000U, 0.5, 0U, 0U, 0.0);
  REQUIRE(partial.has_value());
  REQUIRE(partial->throughput_fps.status == ztick::ValueStatus::measured);
  REQUIRE(partial->throughput_fps.value == Catch::Approx(96000.0));
  REQUIRE(partial->throughput_sps.status == ztick::ValueStatus::not_applicable);
  REQUIRE(partial->throughput_sps.reason == "invalid_channel_count");
  REQUIRE(partial->throughput_bps.status == ztick::ValueStatus::not_applicable);
  REQUIRE(partial->throughput_bps.reason == "invalid_block_size");
  REQUIRE(partial->audio_duration.status == ztick::ValueStatus::not_applicable);
  REQUIRE(partial->audio_duration.reason == "invalid_sample_rate");
  REQUIRE(partial->real_time_factor.status == ztick::ValueStatus::not_applicable);
  REQUIRE(partial->real_time_factor.reason == "invalid_audio_duration");

  const auto complete = ztick::compute_performance_metrics(48000U, 0.5, 2U, 256U, 48000.0);
  REQUIRE(complete.has_value());
  REQUIRE(complete->frames == 48000U);
  REQUIRE(complete->channels == 2U);
  REQUIRE(complete->block_size == 256U);
  REQUIRE(complete->sample_rate_hz == Catch::Approx(48000.0));
  REQUIRE(complete->processing_seconds == Catch::Approx(0.5));
  REQUIRE(complete->throughput_fps.value == Catch::Approx(96000.0));
  REQUIRE(complete->throughput_sps.value == Catch::Approx(192000.0));
  REQUIRE(complete->throughput_bps.value == Catch::Approx(375.0));
  REQUIRE(complete->audio_duration.value == Catch::Approx(1.0));
  REQUIRE(complete->real_time_factor.status == ztick::ValueStatus::measured);
  REQUIRE(complete->real_time_factor.value == Catch::Approx(0.5));
}

/**
 * @brief Verify performance metrics reject an unusable frame count or processing time.
 */
TEST_CASE("performance metrics reject invalid intervals", "[core][statistics]") {
  std::string error;
  REQUIRE_FALSE(ztick::compute_performance_metrics(0U, 0.5, 2U, 128U, 48000.0, &error).has_value());
  REQUIRE(error == "frame count must be positive");

  error.clear();
  REQUIRE_FALSE(
      ztick::compute_performance_metrics(48000U, 0.0, 2U, 128U, 48000.0, &error).has_value());
  REQUIRE(error == "processing time must be positive and finite");
}

/**
 * @brief Verify scaling efficiency formulas against independently computed values.
 */
TEST_CASE("scaling efficiency matches independent calculations", "[core][statistics]") {
  // Fixed work: 8 s with one worker, 2.5 s with four workers -> 8 / (4 * 2.5) = 0.8.
  REQUIRE(ztick::scaling_efficiency(8.0, 2.5, 4U) == Catch::Approx(0.8));
  // One worker is always ideal by definition.
  REQUIRE(ztick::scaling_efficiency(8.0, 8.0, 1U) == Catch::Approx(1.0));
  // Equivalent throughput form: 320 / (4 * 100) = 0.8.
  REQUIRE(ztick::scaling_efficiency_from_throughput(100.0, 320.0, 4U) == Catch::Approx(0.8));
}

/**
 * @brief Verify scaling efficiency is undefined or inapplicable where required.
 */
TEST_CASE("scaling efficiency rejects undefined cases", "[core][statistics]") {
  REQUIRE_FALSE(ztick::scaling_efficiency(8.0, 0.0, 4U).has_value());
  REQUIRE_FALSE(ztick::scaling_efficiency(0.0, 2.5, 4U).has_value());
  REQUIRE_FALSE(ztick::scaling_efficiency(8.0, 2.5, 0U).has_value());
  REQUIRE_FALSE(ztick::scaling_efficiency_from_throughput(0.0, 320.0, 4U).has_value());
  REQUIRE_FALSE(ztick::scaling_efficiency_from_throughput(100.0, 0.0, 4U).has_value());
  REQUIRE_FALSE(ztick::scaling_efficiency_from_throughput(100.0, 320.0, 0U).has_value());
}

/**
 * @brief Verify wall-clock scaling metrics record status, formula and context.
 */
TEST_CASE("wall-time scaling metrics carry estimated status", "[core][statistics]") {
  const auto metrics =
      ztick::compute_scaling_metrics(8.0, 2.5, 4U, ztick::ParallelWork::partitioned);
  REQUIRE(metrics.has_value());
  REQUIRE(metrics->worker_count == 4U);
  REQUIRE(metrics->work == ztick::ParallelWork::partitioned);
  REQUIRE(metrics->scope == "instances");
  REQUIRE(metrics->formula == "wall_time");
  REQUIRE(metrics->efficiency.status == ztick::ValueStatus::estimated);
  REQUIRE(metrics->efficiency.value == Catch::Approx(0.8));
}

/**
 * @brief Verify replicated work and invalid inputs are reported, not silently scaled.
 */
TEST_CASE("scaling metrics handle replicated work and invalid input", "[core][statistics]") {
  const auto replicated =
      ztick::compute_scaling_metrics(8.0, 8.0, 4U, ztick::ParallelWork::replicated);
  REQUIRE(replicated.has_value());
  REQUIRE(replicated->efficiency.status == ztick::ValueStatus::not_applicable);
  REQUIRE(replicated->efficiency.reason == "replicated_work");

  const auto zero_parallel =
      ztick::compute_scaling_metrics(8.0, 0.0, 4U, ztick::ParallelWork::partitioned);
  REQUIRE(zero_parallel.has_value());
  REQUIRE(zero_parallel->efficiency.status == ztick::ValueStatus::not_applicable);
  REQUIRE(zero_parallel->efficiency.reason == "non_positive_parallel_time");

  std::string error;
  REQUIRE_FALSE(
      ztick::compute_scaling_metrics(8.0, 2.5, 0U, ztick::ParallelWork::partitioned, &error)
          .has_value());
  REQUIRE(error == "worker count must be positive");

  error.clear();
  REQUIRE_FALSE(
      ztick::compute_scaling_metrics(0.0, 2.5, 4U, ztick::ParallelWork::partitioned, &error)
          .has_value());
  REQUIRE(error == "single-worker time must be positive");

  error.clear();
  REQUIRE_FALSE(ztick::compute_scaling_metrics(8.0, std::numeric_limits<double>::quiet_NaN(), 4U,
                                               ztick::ParallelWork::partitioned, &error)
                    .has_value());
  REQUIRE(error == "scaling times must be finite");
}

/**
 * @brief Verify throughput-based scaling metrics record their own formula and errors.
 */
TEST_CASE("throughput scaling metrics record formula and availability", "[core][statistics]") {
  const auto metrics = ztick::compute_scaling_metrics_from_throughput(
      100.0, 320.0, 4U, ztick::ParallelWork::partitioned);
  REQUIRE(metrics.has_value());
  REQUIRE(metrics->formula == "throughput");
  REQUIRE(metrics->worker_count == 4U);
  REQUIRE(metrics->efficiency.status == ztick::ValueStatus::estimated);
  REQUIRE(metrics->efficiency.value == Catch::Approx(0.8));

  const auto replicated = ztick::compute_scaling_metrics_from_throughput(
      100.0, 400.0, 4U, ztick::ParallelWork::replicated);
  REQUIRE(replicated.has_value());
  REQUIRE(replicated->efficiency.status == ztick::ValueStatus::not_applicable);
  REQUIRE(replicated->efficiency.reason == "replicated_work");

  const auto zero_parallel = ztick::compute_scaling_metrics_from_throughput(
      100.0, 0.0, 4U, ztick::ParallelWork::partitioned);
  REQUIRE(zero_parallel.has_value());
  REQUIRE(zero_parallel->efficiency.status == ztick::ValueStatus::not_applicable);
  REQUIRE(zero_parallel->efficiency.reason == "non_positive_parallel_throughput");

  std::string error;
  REQUIRE_FALSE(ztick::compute_scaling_metrics_from_throughput(
                    0.0, 320.0, 4U, ztick::ParallelWork::partitioned, &error)
                    .has_value());
  REQUIRE(error == "single-worker throughput must be positive");

  error.clear();
  REQUIRE_FALSE(
      ztick::compute_scaling_metrics_from_throughput(100.0, std::numeric_limits<double>::infinity(),
                                                     4U, ztick::ParallelWork::partitioned, &error)
          .has_value());
  REQUIRE(error == "throughput values must be finite");

  error.clear();
  REQUIRE_FALSE(ztick::compute_scaling_metrics_from_throughput(
                    100.0, 320.0, 0U, ztick::ParallelWork::partitioned, &error)
                    .has_value());
  REQUIRE(error == "worker count must be positive");
}
