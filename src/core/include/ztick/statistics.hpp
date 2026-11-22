// SPDX-License-Identifier: MIT
/**
 * @file statistics.hpp
 * @brief Metric formulas and aggregates over retained timing samples.
 *
 * METRICS_REFERENCE §3 fixes each formula and §4 fixes the raw-data rules:
 * aggregates are computed only from one retained population (the raw basis),
 * never from a separate in-loop accumulator. This module implements those
 * formulas as pure functions, then bundles them into records carrying a
 * @ref ztick::ValueStatus for every reported value. Overhead estimates are not
 * an input here: they remain separately observable (BR §7.3).
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_STATISTICS_HPP
#define ZTICK_STATISTICS_HPP

#include "ztick/metric_status.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztick {

/** @brief One reported metric value with its availability state (REQ-MET-01). */
struct MetricValue {
  /** @brief Availability state of @ref ztick::MetricValue::value. */
  ValueStatus status{ValueStatus::unavailable};
  /** @brief Reported value; meaningful when status is measured or estimated. */
  double value{0.0};
  /** @brief Machine-readable reason when the value is unavailable or not applicable. */
  std::string reason;
};

/**
 * @brief Population from which duration aggregates are computed (METRICS §4).
 * @note Populations are never mixed in one aggregate. The chosen basis is
 *       recorded with the statistics so comparisons use the same population.
 */
enum class RawBasis : std::uint8_t {
  /** @brief Retained per-block durations from a single run. */
  per_block = 0,
  /** @brief Retained whole-run durations across repetitions. */
  run_repetition = 1,
};

/**
 * @brief Render a @ref ztick::RawBasis as its metadata token.
 * @param basis Population to render.
 * @return Stable token used in metric metadata.
 */
std::string_view raw_basis_name(RawBasis basis) noexcept;

/** @brief Named nearest-rank percentiles required by MET-07 to MET-10. */
enum class Percentile : std::uint8_t {
  /** @brief 90th percentile, MET-07. */
  p90 = 0,
  /** @brief 95th percentile, MET-08. */
  p95 = 1,
  /** @brief 99th percentile, MET-09. */
  p99 = 2,
  /** @brief 99.9th percentile, MET-10. */
  p99_9 = 3,
};

/**
 * @brief Read the quantile of a named percentile.
 * @param percentile Percentile to convert.
 * @return Quantile in the open-closed interval (0, 1].
 */
double percentile_quantile(Percentile percentile) noexcept;

/**
 * @brief Render a @ref ztick::Percentile as its canonical name.
 * @param percentile Percentile to render.
 * @return Stable name such as `P99.9`.
 */
std::string_view percentile_name(Percentile percentile) noexcept;

/**
 * @brief Compute the arithmetic mean of a duration population (MET-02).
 * @param samples Durations in seconds; must be non-empty.
 * @return Mean in seconds, or `std::nullopt` for an empty population.
 */
std::optional<double> mean_seconds(const std::vector<double> &samples) noexcept;

/**
 * @brief Compute the median of a duration population (MET-03).
 * @param samples Durations in seconds; must be non-empty.
 * @return `d_((n+1)/2)` for odd `n`, or `(d_(n/2) + d_(n/2+1)) / 2` for even
 *         `n`, using ascending order statistics. This is distinct from
 *         nearest-rank P50, which is not reported.
 */
std::optional<double> median_seconds(const std::vector<double> &samples) noexcept;

/**
 * @brief Compute the minimum duration of a population (MET-04).
 * @param samples Durations in seconds; must be non-empty.
 * @return Smallest retained duration, or `std::nullopt` for an empty population.
 */
std::optional<double> minimum_seconds(const std::vector<double> &samples) noexcept;

/**
 * @brief Compute the maximum duration of a population (MET-05).
 * @param samples Durations in seconds; must be non-empty.
 * @return Largest retained duration, or `std::nullopt` for an empty population.
 */
std::optional<double> maximum_seconds(const std::vector<double> &samples) noexcept;

/**
 * @brief Compute the sample standard deviation of a population (MET-06).
 * @param samples Durations in seconds; must contain at least two entries.
 * @return `sqrt(sum((d_i - mean)^2) / (n - 1))` in seconds, or `std::nullopt`
 *         when fewer than two samples are available.
 */
std::optional<double> sample_stddev_seconds(const std::vector<double> &samples) noexcept;

/**
 * @brief Compute a nearest-rank quantile of a duration population (MET-07..10).
 * @param samples Durations in seconds; must be non-empty.
 * @param quantile Quantile in the open-closed interval (0, 1].
 * @return The observed sample at ascending rank `ceil(quantile * n)`, with no
 *         interpolation, or `std::nullopt` for invalid input.
 * @note For `n < 1000` the P99.9 quantile equals the maximum.
 */
std::optional<double> nearest_rank_quantile_seconds(const std::vector<double> &samples,
                                                    double quantile) noexcept;

/**
 * @brief Compute a named percentile of a duration population.
 * @param samples Durations in seconds; must be non-empty.
 * @param percentile Named percentile to compute.
 * @return The observed nearest-rank sample, or `std::nullopt` for invalid input.
 */
std::optional<double> percentile_seconds(const std::vector<double> &samples,
                                         Percentile percentile) noexcept;

/**
 * @brief Compute canonical throughput from frames and DSP processing time (MET-11).
 * @param frames Number of processed per-channel frames; must be positive.
 * @param processing_seconds DSP processing time of the selected interval; must
 *        be finite and positive.
 * @return Frames per second, or `std::nullopt` for invalid input.
 */
std::optional<double> throughput_frames_per_second(std::uint64_t frames,
                                                   double processing_seconds) noexcept;

/**
 * @brief Derive per-channel sample throughput from frame throughput (MET-11).
 * @param frames_per_second Canonical frame throughput; must be finite and positive.
 * @param channels Channel count; must be positive.
 * @return Samples per second, or `std::nullopt` for invalid input.
 */
std::optional<double> throughput_samples_per_second(double frames_per_second,
                                                    std::uint32_t channels) noexcept;

/**
 * @brief Derive block throughput from frame throughput (MET-11).
 * @param frames_per_second Canonical frame throughput; must be finite and positive.
 * @param block_size Frames per block; must be positive.
 * @return Blocks per second, or `std::nullopt` for invalid input.
 */
std::optional<double> throughput_blocks_per_second(double frames_per_second,
                                                   std::uint32_t block_size) noexcept;

/**
 * @brief Compute the audio duration represented by a frame count (MET-12).
 * @param frames Number of per-channel frames; must be positive.
 * @param sample_rate_hz Workload sample rate in Hz; must be finite and positive.
 * @return Audio duration in seconds, or `std::nullopt` for invalid input.
 */
std::optional<double> audio_duration_seconds(std::uint64_t frames, double sample_rate_hz) noexcept;

/**
 * @brief Compute the real-time factor (MET-12).
 * @param processing_seconds DSP processing time; must be finite and non-negative.
 * @param audio_duration Audio duration represented by the workload; must be finite
 *        and positive.
 * @return Processing time divided by audio duration, or `std::nullopt` for
 *         invalid input. Values below one are faster than real time.
 */
std::optional<double> real_time_factor(double processing_seconds, double audio_duration) noexcept;

/**
 * @brief Compute scaling efficiency from fixed-work wall-clock times (MET-15).
 * @param time_one_worker Wall-clock time with one worker; must be finite and positive.
 * @param time_n_workers Wall-clock time with @p worker_count workers; must be finite
 *        and positive for the metric to be defined.
 * @param worker_count Number of workers; must be positive.
 * @return `T(1) / (n * T(n))`, or `std::nullopt` when undefined or invalid.
 */
std::optional<double> scaling_efficiency(double time_one_worker, double time_n_workers,
                                         std::uint32_t worker_count) noexcept;

/**
 * @brief Compute scaling efficiency from wall-clock throughputs (MET-15).
 * @param throughput_one Throughput with one worker; must be finite and positive.
 * @param throughput_n Throughput with @p worker_count workers; must be finite and
 *        positive for the metric to be defined.
 * @param worker_count Number of workers; must be positive.
 * @return `throughput(n) / (n * throughput(1))`, or `std::nullopt` when undefined
 *         or invalid. Both rates must use the same wall-clock denominator.
 */
std::optional<double> scaling_efficiency_from_throughput(double throughput_one, double throughput_n,
                                                         std::uint32_t worker_count) noexcept;

/** @brief Duration aggregates over one retained population (MET-01..10). */
struct DurationStatistics {
  /** @brief Population the aggregates were computed from. */
  RawBasis basis{RawBasis::per_block};
  /** @brief Number of durations in the population. */
  std::size_t sample_count{0};
  /** @brief Recorded percentile method; `nearest_rank_ceil` with no interpolation. */
  std::string percentile_method{"nearest_rank_ceil"};
  /** @brief MET-02 mean. */
  MetricValue mean;
  /** @brief MET-03 median, distinct from nearest-rank P50. */
  MetricValue median;
  /** @brief MET-04 minimum. */
  MetricValue minimum;
  /** @brief MET-05 maximum. */
  MetricValue maximum;
  /** @brief MET-06 sample standard deviation; not applicable for a single sample. */
  MetricValue standard_deviation;
  /** @brief MET-07 P90. */
  MetricValue p90;
  /** @brief MET-08 P95. */
  MetricValue p95;
  /** @brief MET-09 P99. */
  MetricValue p99;
  /** @brief MET-10 P99.9. */
  MetricValue p99_9;
};

/**
 * @brief Compute all duration aggregates from one retained population.
 * @param samples Retained durations in seconds; must be non-empty and finite.
 * @param basis Population the durations come from; never mix two populations.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Statistics with per-metric statuses, or `std::nullopt` on invalid input.
 * @note The standard deviation is `not_applicable` with reason
 *       `insufficient_samples` for a single sample. Raw samples remain owned by
 *       the caller; this function never modifies them.
 */
std::optional<DurationStatistics> compute_duration_statistics(const std::vector<double> &samples,
                                                              RawBasis basis,
                                                              std::string *error = nullptr);

/** @brief Throughput and real-time metrics for one measured interval (MET-11..12). */
struct PerformanceMetrics {
  /** @brief Processed per-channel frames used as the numerator. */
  std::uint64_t frames{0};
  /** @brief Recorded channel count; zero when unknown. */
  std::uint32_t channels{0};
  /** @brief Recorded frames per block; zero when unknown. */
  std::uint32_t block_size{0};
  /** @brief Recorded workload sample rate in Hz; zero when unknown. */
  double sample_rate_hz{0.0};
  /** @brief DSP processing time used as the denominator, in seconds. */
  double processing_seconds{0.0};
  /** @brief MET-11 canonical frame throughput. */
  MetricValue throughput_fps;
  /** @brief MET-11 derived sample throughput. */
  MetricValue throughput_sps;
  /** @brief MET-11 derived block throughput. */
  MetricValue throughput_bps;
  /** @brief Audio duration represented by the processed frames. */
  MetricValue audio_duration;
  /** @brief MET-12 real-time factor. */
  MetricValue real_time_factor;
};

/**
 * @brief Compute throughput and real-time metrics for one measured interval.
 * @param frames Processed per-channel frames; must be positive.
 * @param processing_seconds DSP processing time; must be finite and positive.
 * @param channels Channel count; derived sample rate is not applicable when zero.
 * @param block_size Frames per block; derived block rate is not applicable when zero.
 * @param sample_rate_hz Workload sample rate; audio duration and RTF are not
 *        applicable when it is not finite and positive.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Performance metrics with per-field statuses, or `std::nullopt` when
 *         the frame count or processing time is invalid.
 */
std::optional<PerformanceMetrics>
compute_performance_metrics(std::uint64_t frames, double processing_seconds, std::uint32_t channels,
                            std::uint32_t block_size, double sample_rate_hz,
                            std::string *error = nullptr);

/**
 * @brief Whether parallel workers partition one workload or replicate it (MET-15).
 * @note Replicating identical work on every instance makes efficiency
 *       identically one and must not be reported as scaling.
 */
enum class ParallelWork : std::uint8_t {
  /** @brief Total work is held fixed and partitioned across workers. */
  partitioned = 0,
  /** @brief The same work is copied to every worker; scaling is not reported. */
  replicated = 1,
};

/**
 * @brief Render a @ref ztick::ParallelWork as its metadata token.
 * @param work Work arrangement to render.
 * @return Stable token used in metric metadata.
 */
std::string_view parallel_work_name(ParallelWork work) noexcept;

/** @brief Scaling efficiency and its recorded context (MET-15). */
struct ScalingMetrics {
  /** @brief Number of workers compared against one worker. */
  std::uint32_t worker_count{0};
  /** @brief Whether work was partitioned or replicated. */
  ParallelWork work{ParallelWork::partitioned};
  /** @brief `instances` or `engine_threads`, supplied by the caller (BR §8). */
  std::string scope{"instances"};
  /** @brief Formula used: `wall_time` or `throughput`. */
  std::string formula{"wall_time"};
  /** @brief MET-15 efficiency; estimated when defined, otherwise not applicable. */
  MetricValue efficiency;
};

/**
 * @brief Compute scaling efficiency from fixed-work wall-clock times.
 * @param time_one_worker Wall-clock time with one worker.
 * @param time_n_workers Wall-clock time with @ref ztick::ScalingMetrics::worker_count workers.
 * @param worker_count Number of workers; must be positive.
 * @param work Whether the total work was partitioned or replicated.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Scaling metrics with the recorded formula, or `std::nullopt` on invalid input.
 */
std::optional<ScalingMetrics> compute_scaling_metrics(double time_one_worker, double time_n_workers,
                                                      std::uint32_t worker_count, ParallelWork work,
                                                      std::string *error = nullptr);

/**
 * @brief Compute scaling efficiency from wall-clock throughputs.
 * @param throughput_one Throughput with one worker.
 * @param throughput_n Throughput with @ref ztick::ScalingMetrics::worker_count workers.
 * @param worker_count Number of workers; must be positive.
 * @param work Whether the total work was partitioned or replicated.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Scaling metrics with the recorded formula, or `std::nullopt` on invalid input.
 */
std::optional<ScalingMetrics> compute_scaling_metrics_from_throughput(double throughput_one,
                                                                      double throughput_n,
                                                                      std::uint32_t worker_count,
                                                                      ParallelWork work,
                                                                      std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_STATISTICS_HPP
