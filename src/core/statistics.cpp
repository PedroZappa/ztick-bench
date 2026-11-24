// SPDX-License-Identifier: MIT
/**
 * @file statistics.cpp
 * @brief Implementations of the METRICS §3 formulas and aggregate records.
 */

#include "ztick/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
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
 * @brief Test whether every duration is finite.
 * @param samples Durations in seconds.
 * @return True when all values are finite, or the population is empty.
 */
bool all_finite(const std::vector<double> &samples) noexcept {
  return std::all_of(samples.begin(), samples.end(),
                     [](double value) { return std::isfinite(value); });
}

/**
 * @brief Create an ascending copy of a duration population.
 * @param samples Durations in seconds.
 * @return Sorted copy; the caller's samples are untouched.
 */
std::vector<double> sorted_copy(const std::vector<double> &samples) {
  std::vector<double> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

/**
 * @brief Create a directly observed metric value.
 * @param value Observed value.
 * @return A measured value with no reason text.
 */
MetricValue measured_value(double value) {
  MetricValue metric;
  metric.status = ValueStatus::measured;
  metric.value = value;
  return metric;
}

/**
 * @brief Create a derived metric value.
 * @param value Derived value.
 * @return An estimated value with no reason text.
 */
MetricValue estimated_value(double value) {
  MetricValue metric;
  metric.status = ValueStatus::estimated;
  metric.value = value;
  return metric;
}

/**
 * @brief Create a reported value with no applicable definition.
 * @param reason Machine-readable reason for the absence.
 * @return A not-applicable value with zero as a placeholder.
 */
MetricValue not_applicable(const std::string &reason) {
  MetricValue metric;
  metric.status = ValueStatus::not_applicable;
  metric.reason = reason;
  return metric;
}

/**
 * @brief Test whether a value is finite and strictly positive.
 * @param value Value to test.
 * @return True when @p value is finite and greater than zero.
 */
bool finite_positive(double value) noexcept { return std::isfinite(value) && value > 0.0; }

} // namespace

std::string_view raw_basis_name(RawBasis basis) noexcept {
  switch (basis) {
  case RawBasis::per_block:
    return "per_block";
  case RawBasis::run_repetition:
    return "run_repetition";
  }
  return "unknown";
}

double percentile_quantile(Percentile percentile) noexcept {
  switch (percentile) {
  case Percentile::p90:
    return 0.90;
  case Percentile::p95:
    return 0.95;
  case Percentile::p99:
    return 0.99;
  case Percentile::p99_9:
    return 0.999;
  }
  return 0.0;
}

// cppcheck-suppress unusedFunction
std::string_view percentile_name(Percentile percentile) noexcept {
  switch (percentile) {
  case Percentile::p90:
    return "P90";
  case Percentile::p95:
    return "P95";
  case Percentile::p99:
    return "P99";
  case Percentile::p99_9:
    return "P99.9";
  }
  return "unknown";
}

std::optional<double> mean_seconds(const std::vector<double> &samples) noexcept {
  if (samples.empty()) {
    return std::nullopt;
  }
  return std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
}

std::optional<double> median_seconds(const std::vector<double> &samples) noexcept {
  if (samples.empty()) {
    return std::nullopt;
  }
  const std::vector<double> sorted = sorted_copy(samples);
  const std::size_t count = sorted.size();
  if (count % 2U == 1U) {
    return sorted[count / 2U];
  }
  return (sorted[count / 2U - 1U] + sorted[count / 2U]) / 2.0;
}

std::optional<double> minimum_seconds(const std::vector<double> &samples) noexcept {
  if (samples.empty()) {
    return std::nullopt;
  }
  return *std::min_element(samples.begin(), samples.end());
}

std::optional<double> maximum_seconds(const std::vector<double> &samples) noexcept {
  if (samples.empty()) {
    return std::nullopt;
  }
  return *std::max_element(samples.begin(), samples.end());
}

std::optional<double> sample_stddev_seconds(const std::vector<double> &samples) noexcept {
  if (samples.size() < 2U) {
    return std::nullopt;
  }
  const double center = mean_seconds(samples).value_or(0.0);
  const double sum_squared = std::transform_reduce(samples.begin(), samples.end(), 0.0,
                                                   std::plus<double>{}, [center](double value) {
                                                     const double difference = value - center;
                                                     return difference * difference;
                                                   });
  return std::sqrt(sum_squared / static_cast<double>(samples.size() - 1U));
}

std::optional<double> nearest_rank_quantile_seconds(const std::vector<double> &samples,
                                                    double quantile) noexcept {
  if (samples.empty() || !std::isfinite(quantile) || !(quantile > 0.0) || quantile > 1.0) {
    return std::nullopt;
  }
  const std::vector<double> sorted = sorted_copy(samples);
  const auto rank =
      static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(sorted.size())));
  const std::size_t bounded_rank = std::min(rank, sorted.size());
  return sorted[bounded_rank - 1U];
}

std::optional<double> percentile_seconds(const std::vector<double> &samples,
                                         Percentile percentile) noexcept {
  return nearest_rank_quantile_seconds(samples, percentile_quantile(percentile));
}

std::optional<double> throughput_frames_per_second(std::uint64_t frames,
                                                   double processing_seconds) noexcept {
  if (frames == 0U || !finite_positive(processing_seconds)) {
    return std::nullopt;
  }
  return static_cast<double>(frames) / processing_seconds;
}

std::optional<double> throughput_samples_per_second(double frames_per_second,
                                                    std::uint32_t channels) noexcept {
  if (!finite_positive(frames_per_second) || channels == 0U) {
    return std::nullopt;
  }
  return frames_per_second * static_cast<double>(channels);
}

std::optional<double> throughput_blocks_per_second(double frames_per_second,
                                                   std::uint32_t block_size) noexcept {
  if (!finite_positive(frames_per_second) || block_size == 0U) {
    return std::nullopt;
  }
  return frames_per_second / static_cast<double>(block_size);
}

std::optional<double> audio_duration_seconds(std::uint64_t frames, double sample_rate_hz) noexcept {
  if (frames == 0U || !finite_positive(sample_rate_hz)) {
    return std::nullopt;
  }
  return static_cast<double>(frames) / sample_rate_hz;
}

std::optional<double> real_time_factor(double processing_seconds, double audio_duration) noexcept {
  if (!std::isfinite(processing_seconds) || processing_seconds < 0.0 ||
      !finite_positive(audio_duration)) {
    return std::nullopt;
  }
  return processing_seconds / audio_duration;
}

std::optional<double> scaling_efficiency(double time_one_worker, double time_n_workers,
                                         std::uint32_t worker_count) noexcept {
  if (worker_count == 0U || !std::isfinite(time_one_worker) || !std::isfinite(time_n_workers) ||
      !(time_one_worker > 0.0) || !(time_n_workers > 0.0)) {
    return std::nullopt;
  }
  return time_one_worker / (static_cast<double>(worker_count) * time_n_workers);
}

std::optional<double> scaling_efficiency_from_throughput(double throughput_one, double throughput_n,
                                                         std::uint32_t worker_count) noexcept {
  if (worker_count == 0U || !finite_positive(throughput_one) || !finite_positive(throughput_n)) {
    return std::nullopt;
  }
  return throughput_n / (static_cast<double>(worker_count) * throughput_one);
}

std::optional<DurationStatistics> compute_duration_statistics(const std::vector<double> &samples,
                                                              RawBasis basis, std::string *error) {
  if (samples.empty()) {
    record_error(error, "empty sample population");
    return std::nullopt;
  }
  if (!all_finite(samples)) {
    record_error(error, "sample durations must be finite");
    return std::nullopt;
  }

  DurationStatistics statistics;
  statistics.basis = basis;
  statistics.sample_count = samples.size();
  statistics.mean = measured_value(mean_seconds(samples).value_or(0.0));
  statistics.median = measured_value(median_seconds(samples).value_or(0.0));
  statistics.minimum = measured_value(minimum_seconds(samples).value_or(0.0));
  statistics.maximum = measured_value(maximum_seconds(samples).value_or(0.0));
  const auto deviation = sample_stddev_seconds(samples);
  statistics.standard_deviation =
      deviation ? measured_value(*deviation) : not_applicable("insufficient_samples");
  statistics.p90 = measured_value(percentile_seconds(samples, Percentile::p90).value_or(0.0));
  statistics.p95 = measured_value(percentile_seconds(samples, Percentile::p95).value_or(0.0));
  statistics.p99 = measured_value(percentile_seconds(samples, Percentile::p99).value_or(0.0));
  statistics.p99_9 = measured_value(percentile_seconds(samples, Percentile::p99_9).value_or(0.0));
  return statistics;
}

std::optional<PerformanceMetrics>
// Frames, channels, block size and sample rate are distinct, named units at this API boundary.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
compute_performance_metrics(std::uint64_t frames, double processing_seconds, std::uint32_t channels,
                            std::uint32_t block_size, double sample_rate_hz, std::string *error) {
  if (frames == 0U) {
    record_error(error, "frame count must be positive");
    return std::nullopt;
  }
  if (!finite_positive(processing_seconds)) {
    record_error(error, "processing time must be positive and finite");
    return std::nullopt;
  }

  PerformanceMetrics metrics;
  metrics.frames = frames;
  metrics.processing_seconds = processing_seconds;
  metrics.channels = channels;
  metrics.block_size = block_size;
  metrics.sample_rate_hz = sample_rate_hz;
  metrics.throughput_fps =
      measured_value(throughput_frames_per_second(frames, processing_seconds).value_or(0.0));

  const auto samples_per_second =
      throughput_samples_per_second(metrics.throughput_fps.value, channels);
  metrics.throughput_sps = samples_per_second ? measured_value(*samples_per_second)
                                              : not_applicable("invalid_channel_count");

  const auto blocks_per_second =
      throughput_blocks_per_second(metrics.throughput_fps.value, block_size);
  metrics.throughput_bps =
      blocks_per_second ? measured_value(*blocks_per_second) : not_applicable("invalid_block_size");

  const auto duration = audio_duration_seconds(frames, sample_rate_hz);
  metrics.audio_duration =
      duration ? measured_value(*duration) : not_applicable("invalid_sample_rate");

  if (!duration || !(*duration > 0.0)) {
    metrics.real_time_factor = not_applicable("invalid_audio_duration");
  } else {
    metrics.real_time_factor =
        measured_value(real_time_factor(processing_seconds, duration.value_or(0.0)).value_or(0.0));
  }
  return metrics;
}

// cppcheck-suppress unusedFunction
std::string_view parallel_work_name(ParallelWork work) noexcept {
  switch (work) {
  case ParallelWork::partitioned:
    return "partitioned";
  case ParallelWork::replicated:
    return "replicated";
  }
  return "unknown";
}

// cppcheck-suppress unusedFunction
std::optional<ScalingMetrics> compute_scaling_metrics(double time_one_worker, double time_n_workers,
                                                      std::uint32_t worker_count, ParallelWork work,
                                                      std::string *error) {
  if (worker_count == 0U) {
    record_error(error, "worker count must be positive");
    return std::nullopt;
  }
  if (!std::isfinite(time_one_worker) || !std::isfinite(time_n_workers)) {
    record_error(error, "scaling times must be finite");
    return std::nullopt;
  }
  if (!(time_one_worker > 0.0)) {
    record_error(error, "single-worker time must be positive");
    return std::nullopt;
  }

  ScalingMetrics metrics;
  metrics.worker_count = worker_count;
  metrics.work = work;
  metrics.formula = "wall_time";
  if (work == ParallelWork::replicated) {
    metrics.efficiency = not_applicable("replicated_work");
    return metrics;
  }
  const auto efficiency = scaling_efficiency(time_one_worker, time_n_workers, worker_count);
  if (!efficiency) {
    metrics.efficiency = not_applicable("non_positive_parallel_time");
    return metrics;
  }
  metrics.efficiency = estimated_value(*efficiency);
  return metrics;
}

// cppcheck-suppress unusedFunction
std::optional<ScalingMetrics> compute_scaling_metrics_from_throughput(double throughput_one,
                                                                      double throughput_n,
                                                                      std::uint32_t worker_count,
                                                                      ParallelWork work,
                                                                      std::string *error) {
  if (worker_count == 0U) {
    record_error(error, "worker count must be positive");
    return std::nullopt;
  }
  if (!std::isfinite(throughput_one) || !std::isfinite(throughput_n)) {
    record_error(error, "throughput values must be finite");
    return std::nullopt;
  }
  if (!(throughput_one > 0.0)) {
    record_error(error, "single-worker throughput must be positive");
    return std::nullopt;
  }

  ScalingMetrics metrics;
  metrics.worker_count = worker_count;
  metrics.work = work;
  metrics.formula = "throughput";
  if (work == ParallelWork::replicated) {
    metrics.efficiency = not_applicable("replicated_work");
    return metrics;
  }
  const auto efficiency =
      scaling_efficiency_from_throughput(throughput_one, throughput_n, worker_count);
  if (!efficiency) {
    metrics.efficiency = not_applicable("non_positive_parallel_throughput");
    return metrics;
  }
  metrics.efficiency = estimated_value(*efficiency);
  return metrics;
}

} // namespace ztick
