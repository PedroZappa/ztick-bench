// SPDX-License-Identifier: MIT
/**
 * @file session.cpp
 * @brief Core orchestration of one config-driven benchmark session.
 *
 * The portable config owns every semantic setting; this translation unit turns
 * it into a concrete plan, runs the built-in deterministic reference workload
 * through @ref ztick::run_sequence and renders the result document. Host and
 * front-end concerns remain injected through @ref ztick::SessionContext.
 */

#include "ztick/run_session.hpp"

#include "ztick/config_document.hpp"
#include "ztick/generator.hpp"
#include "ztick/metadata.hpp"
#include "ztick/profile.hpp"
#include "ztick/result_document.hpp"
#include "ztick/statistics.hpp"
#include "ztick/validation.hpp"
#include "ztick/workload.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztick {

namespace {

/** @brief Private helpers for semantic planning and result rendering. */
namespace detail {

/**
 * @brief Write an error only when the receiver is non-null.
 * @param error Optional receiver for the message.
 * @param message Human-readable failure explanation.
 */
void record_error(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

/**
 * @brief Render a metric availability status as its stable token.
 * @param status Status to render.
 * @return `measured`, `estimated`, `unavailable` or `not_applicable`.
 */
std::string_view value_status_token(ValueStatus status) noexcept {
  switch (status) {
  case ValueStatus::measured:
    return "measured";
  case ValueStatus::estimated:
    return "estimated";
  case ValueStatus::unavailable:
    return "unavailable";
  case ValueStatus::not_applicable:
    return "not_applicable";
  }
  return "unavailable";
}

/**
 * @brief Read a required integer member.
 * @param parent Object to search.
 * @param key Member name.
 * @param path Stable path used in error messages.
 * @param value Receives the parsed integer.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the member existed and had integer type.
 */
bool require_integer(const ConfigValue &parent, std::string_view key, std::string_view path,
                     std::int64_t &value, std::string *error) {
  const ConfigValue *member = parent.find(key);
  const auto parsed = member == nullptr ? std::nullopt : member->as_integer();
  if (!parsed.has_value()) {
    record_error(error, std::string(path) + " must be an integer");
    return false;
  }
  value = *parsed;
  return true;
}

/**
 * @brief Read a required finite real member.
 * @param parent Object to search.
 * @param key Member name.
 * @param path Stable path used in error messages.
 * @param value Receives the parsed real.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the member existed and was a finite real.
 */
bool require_real(const ConfigValue &parent, std::string_view key, std::string_view path,
                  double &value, std::string *error) {
  const ConfigValue *member = parent.find(key);
  const auto parsed = member == nullptr ? std::nullopt : member->as_real();
  if (!parsed.has_value() || !std::isfinite(*parsed)) {
    record_error(error, std::string(path) + " must be a finite real");
    return false;
  }
  value = *parsed;
  return true;
}

/**
 * @brief Read a required positive member into a 32-bit unsigned value.
 * @param parent Object to search.
 * @param key Member name.
 * @param path Stable path used in error messages.
 * @param value Receives the bounded value.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the member was a positive 32-bit integer.
 */
bool require_positive_u32(const ConfigValue &parent, std::string_view key, std::string_view path,
                          std::uint32_t &value, std::string *error) {
  std::int64_t parsed = 0;
  if (!require_integer(parent, key, path, parsed, error)) {
    return false;
  }
  constexpr std::int64_t kMax = std::numeric_limits<std::uint32_t>::max();
  if (parsed <= 0 || parsed > kMax) {
    record_error(error, std::string(path) + " must be a positive 32-bit integer");
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

/**
 * @brief Read a scalar token that may be written as text or an integer.
 * @param parent Object to search.
 * @param key Member name.
 * @param value Receives the token text.
 * @return True when the member was a string or an integer.
 */
bool read_token(const ConfigValue &parent, std::string_view key, std::string &value) {
  const ConfigValue *member = parent.find(key);
  if (member == nullptr) {
    return false;
  }
  if (const auto text = member->as_string(); text.has_value()) {
    value = *text;
    return true;
  }
  if (const auto integer = member->as_integer(); integer.has_value()) {
    value = std::to_string(*integer);
    return true;
  }
  return false;
}

/**
 * @brief Parse a worker-count token without silently changing its meaning.
 * @param token Token from the config document.
 * @param path Stable path used in error messages.
 * @param workers Receives the resolved worker count; `auto` resolves to one.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the token was `auto` or a positive 32-bit integer.
 */
bool parse_worker_token(std::string_view token, std::string_view path, std::uint32_t &workers,
                        std::string *error) {
  if (token == "auto") {
    workers = 1U;
    return true;
  }
  std::int64_t parsed = 0;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
  constexpr std::int64_t kMax = std::numeric_limits<std::uint32_t>::max();
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || parsed <= 0 ||
      parsed > kMax) {
    record_error(error, std::string(path) + " must be 'auto' or a positive 32-bit integer");
    return false;
  }
  workers = static_cast<std::uint32_t>(parsed);
  return true;
}

/** @brief Semantic execution plan derived from one validated config document. */
struct RunPlan {
  /** @brief Profile settings parsed from the profile-owned sections. */
  Profile profile;
  /** @brief Workload identity and built-in generator settings. */
  Workload workload;
  /** @brief Validated generator definition used for deterministic input. */
  std::optional<GeneratorSpec> generator;
  /** @brief Workload sample rate in Hz. */
  double sample_rate_hz{0.0};
  /** @brief Frames per processing block. */
  std::uint32_t block_size{0};
  /** @brief Channel count used by throughput metrics. */
  std::uint32_t channels{0};
  /** @brief Instance count recorded in the run test point. */
  std::uint32_t instances{1};
  /** @brief Oversampling factor recorded in the run test point. */
  std::uint32_t oversampling{1};
  /** @brief Worker count recorded in the run execution context. */
  std::uint32_t workers{1};
  /** @brief Number of blocks in one repetition. */
  std::uint32_t block_count{0};
  /** @brief Total per-channel frames in one repetition. */
  std::uint64_t frames{0};
  /** @brief Selected timing levels in config-declared order. */
  std::vector<TimingLevel> timing_levels;
  /** @brief Requested isolation token before auto resolution. */
  std::string isolation_request{"auto"};
  /** @brief Selected isolation token stored in run records. */
  std::string isolation{"in_process"};
  /** @brief Affinity token recorded in run records. */
  std::string affinity{"auto"};
  /** @brief Validation tolerances parsed from the config. */
  Tolerance tolerance;
};

/**
 * @brief Build a semantic execution plan from a config snapshot.
 * @param config Validated config document.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return The plan, or `std::nullopt` when a semantic setting is invalid.
 */
std::optional<RunPlan> make_run_plan(const ConfigValue &config, std::string *error) {
  RunPlan plan;
  const auto profile = profile_from_config(config, error);
  if (!profile.has_value()) {
    return std::nullopt;
  }
  plan.profile = *profile;

  const ConfigValue *workload_section = config.find("workload");
  if (workload_section == nullptr) {
    record_error(error, "workload section is missing");
    return std::nullopt;
  }
  const auto workload = workload_from_config(*workload_section, error);
  if (!workload.has_value()) {
    return std::nullopt;
  }
  plan.workload = *workload;
  const auto generator = generator_from_config(plan.workload.generator, error);
  if (!generator.has_value()) {
    return std::nullopt;
  }
  plan.generator = *generator;

  const ConfigValue *dimensions = config.find("dimensions");
  if (dimensions == nullptr || dimensions->type() != ConfigValueType::object) {
    record_error(error, "dimensions must be an object");
    return std::nullopt;
  }
  if (!require_real(*dimensions, "sample_rate_hz", "dimensions.sample_rate_hz", plan.sample_rate_hz,
                    error)) {
    return std::nullopt;
  }
  if (!(plan.sample_rate_hz > 0.0)) {
    record_error(error, "dimensions.sample_rate_hz must be positive");
    return std::nullopt;
  }
  if (!require_positive_u32(*dimensions, "block_size", "dimensions.block_size", plan.block_size,
                            error) ||
      !require_positive_u32(*dimensions, "channels", "dimensions.channels", plan.channels, error) ||
      !require_positive_u32(*dimensions, "instances", "dimensions.instances", plan.instances,
                            error) ||
      !require_positive_u32(*dimensions, "oversampling", "dimensions.oversampling",
                            plan.oversampling, error)) {
    return std::nullopt;
  }

  const ConfigValue *execution = config.find("execution");
  if (execution == nullptr || execution->type() != ConfigValueType::object) {
    record_error(error, "execution must be an object");
    return std::nullopt;
  }
  const ConfigValue *isolation = execution->find("isolation");
  if (isolation == nullptr || isolation->type() != ConfigValueType::string) {
    record_error(error, "execution.isolation must be a string");
    return std::nullopt;
  }
  plan.isolation_request = *(isolation->as_string());
  if (plan.isolation_request == "auto" || plan.isolation_request == "in_process") {
    plan.isolation = "in_process";
  } else if (plan.isolation_request == "out_of_process") {
    record_error(error, "out_of_process isolation is not available for the built-in workload");
    return std::nullopt;
  } else {
    record_error(error, "execution.isolation has an unknown value");
    return std::nullopt;
  }

  constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();
  if (plan.profile.duration_mode == DurationMode::duration) {
    if (!(plan.profile.duration_seconds > 0.0)) {
      record_error(error, "execution.duration_seconds must be positive in duration mode");
      return std::nullopt;
    }
    const double target = std::ceil(plan.profile.duration_seconds * plan.sample_rate_hz);
    if (!(target >= 1.0) || target > static_cast<double>(kU64Max)) {
      record_error(error, "execution.duration_seconds exceeds the addressable frame range");
      return std::nullopt;
    }
    const std::uint64_t frames = static_cast<std::uint64_t>(target);
    const std::uint64_t blocks = (frames + plan.block_size - 1U) / plan.block_size;
    if (blocks > std::numeric_limits<std::uint32_t>::max()) {
      record_error(error, "execution.duration_seconds requires too many blocks");
      return std::nullopt;
    }
    plan.block_count = static_cast<std::uint32_t>(blocks);
  } else {
    if (plan.profile.iterations == 0U ||
        plan.profile.iterations > std::numeric_limits<std::uint32_t>::max()) {
      record_error(error, "execution.iterations must be a positive 32-bit integer");
      return std::nullopt;
    }
    plan.block_count = static_cast<std::uint32_t>(plan.profile.iterations);
  }
  if (plan.block_count == 0U) {
    record_error(error, "execution must request at least one block");
    return std::nullopt;
  }
  if (static_cast<std::uint64_t>(plan.block_count) > kU64Max / plan.block_size) {
    record_error(error, "configured workload is too large");
    return std::nullopt;
  }
  plan.frames = static_cast<std::uint64_t>(plan.block_count) * plan.block_size;
  if (plan.frames > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    record_error(error, "configured workload exceeds addressable sample storage");
    return std::nullopt;
  }

  if (plan.profile.timing_levels.empty()) {
    plan.timing_levels.push_back(TimingLevel::whole_run);
  }
  for (const std::string &token : plan.profile.timing_levels) {
    const auto level = parse_timing_level(token);
    if (!level.has_value()) {
      record_error(error, "measurement.timing_levels contains an unknown token");
      return std::nullopt;
    }
    if (std::find(plan.timing_levels.begin(), plan.timing_levels.end(), *level) ==
        plan.timing_levels.end()) {
      plan.timing_levels.push_back(*level);
    }
  }
  const bool has_block = std::find(plan.timing_levels.begin(), plan.timing_levels.end(),
                                   TimingLevel::per_block) != plan.timing_levels.end();
  const bool has_whole = std::find(plan.timing_levels.begin(), plan.timing_levels.end(),
                                   TimingLevel::whole_run) != plan.timing_levels.end();
  if (!has_block && !has_whole) {
    record_error(error, "measurement.timing_levels must include per_block or whole_run");
    return std::nullopt;
  }
  if (plan.profile.detail == MeasurementDetail::per_block && !has_block) {
    record_error(error, "measurement.detail=per_block requires per_block timing");
    return std::nullopt;
  }

  const ConfigValue *parallelism = config.find("parallelism");
  if (parallelism != nullptr && parallelism->type() == ConfigValueType::object) {
    std::string token;
    if (read_token(*parallelism, "workers", token) &&
        !parse_worker_token(token, "parallelism.workers", plan.workers, error)) {
      return std::nullopt;
    }
    if (read_token(*parallelism, "affinity", token)) {
      plan.affinity = std::move(token);
    }
  }

  const ConfigValue *validation = config.find("validation");
  if (validation == nullptr || validation->type() != ConfigValueType::object) {
    record_error(error, "validation must be an object");
    return std::nullopt;
  }
  const ConfigValue *tolerances = validation->find("tolerances");
  if (tolerances != nullptr && tolerances->type() == ConfigValueType::object) {
    double absolute = 0.0;
    double relative = 0.0;
    if (tolerances->find("absolute") != nullptr &&
        !require_real(*tolerances, "absolute", "validation.tolerances.absolute", absolute, error)) {
      return std::nullopt;
    }
    if (tolerances->find("relative") != nullptr &&
        !require_real(*tolerances, "relative", "validation.tolerances.relative", relative, error)) {
      return std::nullopt;
    }
    if (absolute < 0.0 || relative < 0.0) {
      record_error(error, "validation tolerances must be non-negative");
      return std::nullopt;
    }
    if (tolerances->find("absolute") != nullptr) {
      plan.tolerance.absolute = absolute;
    }
    if (tolerances->find("relative") != nullptr) {
      plan.tolerance.relative = relative;
    }
  }
  return plan;
}

/**
 * @brief Accumulated sink preventing the reference work from being optimized away.
 */
struct WorkSink {
  /** @brief Running checksum of all processed samples. */
  std::atomic<double> checksum{0.0};
};

/**
 * @brief Copy one block of generated samples through the built-in reference path.
 * @param input Generated input samples.
 * @param block Zero-based block index.
 * @param output Preallocated output buffer.
 * @param block_size Frames per block.
 * @param sink Checksum accumulator observed outside the primary interval.
 */
void process_block(const std::vector<double> &input, std::size_t block, std::vector<double> &output,
                   std::uint32_t block_size, WorkSink &sink) {
  const std::size_t begin = block * block_size;
  const std::size_t end = std::min(begin + block_size, input.size());
  double sum = 0.0;
  for (std::size_t index = begin; index < end; ++index) {
    output[index] = input[index];
    sum += output[index];
  }
  sink.checksum.store(sink.checksum.load() + sum);
}

/**
 * @brief Create one canonical metric record.
 * @param id Stable metric identifier, such as `MET-02`.
 * @param name Human-readable metric name.
 * @param value Reported value and status.
 * @param unit SI or derived unit.
 * @param source Instrumentation source token.
 * @param method Derivation method token.
 * @param context Additional metadata object such as sample count or basis.
 * @return Config-shaped metric record; unavailable values carry `null` plus a reason.
 */
ConfigValue metric_record(std::string id, std::string name, const MetricValue &value,
                          std::string unit, std::string source, std::string method,
                          ConfigValue context) {
  ConfigValue metric = ConfigValue::object();
  metric.set("id", ConfigValue::string(std::move(id)));
  metric.set("name", ConfigValue::string(std::move(name)));
  const bool numeric =
      value.status == ValueStatus::measured || value.status == ValueStatus::estimated;
  metric.set("value", numeric ? ConfigValue::real(value.value) : ConfigValue::null());
  metric.set("unit", ConfigValue::string(std::move(unit)));
  metric.set("status", ConfigValue::string(std::string(value_status_token(value.status))));
  metric.set("source", ConfigValue::string(std::move(source)));
  metric.set("method", ConfigValue::string(std::move(method)));
  metric.set("reason", ConfigValue::string(value.reason));
  metric.set("context", std::move(context));
  return metric;
}

/**
 * @brief Build the context object attached to duration metrics.
 * @param basis Retained population used for aggregation.
 * @param sample_count Number of durations in the population.
 * @param percentile_method Recorded percentile method.
 * @return Config-shaped metadata object.
 */
ConfigValue duration_context(RawBasis basis, std::size_t sample_count,
                             std::string_view percentile_method) {
  ConfigValue context = ConfigValue::object();
  context.set("raw_basis", ConfigValue::string(std::string(raw_basis_name(basis))));
  context.set("sample_count", ConfigValue::integer(static_cast<std::int64_t>(sample_count)));
  context.set("percentile_method", ConfigValue::string(std::string(percentile_method)));
  return context;
}

/**
 * @brief Convert duration statistics into canonical metric records.
 * @param stats Statistics to serialize.
 * @return Metric records MET-02 through MET-10 in identifier order.
 */
std::vector<ConfigValue> duration_metrics(const DurationStatistics &stats) {
  std::vector<ConfigValue> metrics;
  metrics.reserve(9U);
  const ConfigValue context =
      duration_context(stats.basis, stats.sample_count, stats.percentile_method);
  const std::string source = "instrumented_dsp";
  const auto add = [&metrics, &context, &source, &stats](std::string id, std::string name,
                                                         const MetricValue &value) {
    metrics.push_back(metric_record(std::move(id), std::move(name), value, "s", source,
                                    stats.percentile_method, context));
  };
  add("MET-02", "Mean", stats.mean);
  add("MET-03", "Median", stats.median);
  add("MET-04", "Minimum", stats.minimum);
  add("MET-05", "Maximum", stats.maximum);
  add("MET-06", "Standard deviation", stats.standard_deviation);
  add("MET-07", "P90", stats.p90);
  add("MET-08", "P95", stats.p95);
  add("MET-09", "P99", stats.p99);
  add("MET-10", "P99.9", stats.p99_9);
  return metrics;
}

/**
 * @brief Convert timing samples and overhead into a timing object.
 * @param trace Execution trace owning the raw samples.
 * @return Config-shaped timing object with one array per selected level.
 */
ConfigValue timing_object(const ExecutionTrace &trace) {
  ConfigValue timing = ConfigValue::object();
  for (const TimingSamples &samples : trace.timing) {
    ConfigValue values = ConfigValue::array();
    std::for_each(samples.durations_seconds.begin(), samples.durations_seconds.end(),
                  [&values](double duration) { values.push_back(ConfigValue::real(duration)); });
    timing.set(std::string(timing_level_name(samples.level)), std::move(values));
  }
  const OverheadEstimate &overhead = trace.overhead;
  ConfigValue estimate = ConfigValue::object();
  estimate.set("status", ConfigValue::string(std::string(value_status_token(overhead.status))));
  estimate.set("seconds", ConfigValue::real(overhead.seconds));
  estimate.set("dsp_time_seconds", ConfigValue::real(overhead.dsp_time_seconds));
  estimate.set("interval_count",
               ConfigValue::integer(static_cast<std::int64_t>(overhead.interval_count)));
  estimate.set("method", ConfigValue::string(overhead.method));
  ConfigValue calibration = ConfigValue::array();
  std::for_each(
      overhead.calibration_samples_seconds.begin(), overhead.calibration_samples_seconds.end(),
      [&calibration](double sample) { calibration.push_back(ConfigValue::real(sample)); });
  estimate.set("calibration_samples_seconds", std::move(calibration));
  timing.set("overhead_estimate", std::move(estimate));
  return timing;
}

/**
 * @brief Convert a validation report into a run-record validation object.
 * @param report Report to serialize.
 * @param tolerance Tolerances used by the phase.
 * @return Config-shaped validation object with every skipped check retained.
 */
ConfigValue validation_object(const ValidationReport &report, const Tolerance &tolerance) {
  ConfigValue validation = ConfigValue::object();
  validation.set("level", ConfigValue::string(std::string(validation_level_name(report.level))));
  validation.set("cases_checked",
                 ConfigValue::integer(static_cast<std::int64_t>(report.cases_checked)));
  ConfigValue checks = ConfigValue::array();
  for (const CheckOutcome &outcome : report.outcomes) {
    ConfigValue check = ConfigValue::object();
    check.set("check", ConfigValue::string(outcome.check));
    check.set("case_label", ConfigValue::string(outcome.case_label));
    check.set("level", ConfigValue::string(std::string(validation_level_name(outcome.level))));
    check.set("status", ConfigValue::string(std::string(check_status_name(outcome.status))));
    check.set("reason", ConfigValue::string(outcome.reason));
    check.set("observed_error", ConfigValue::real(outcome.observed_error));
    check.set("checked_samples",
              ConfigValue::integer(static_cast<std::int64_t>(outcome.checked_samples)));
    check.set("reference_source",
              ConfigValue::string(std::string(reference_source_name(outcome.reference_source))));
    ConfigValue used = ConfigValue::object();
    used.set("absolute", ConfigValue::real(outcome.tolerance.absolute));
    used.set("relative", ConfigValue::real(outcome.tolerance.relative));
    check.set("tolerance", std::move(used));
    checks.push_back(std::move(check));
  }
  validation.set("checks", std::move(checks));
  ConfigValue configured = ConfigValue::object();
  configured.set("absolute", ConfigValue::real(tolerance.absolute));
  configured.set("relative", ConfigValue::real(tolerance.relative));
  validation.set("tolerance", std::move(configured));
  return validation;
}

/**
 * @brief Create a classified failure or warning record.
 * @param category Stable category token.
 * @param message Human-readable explanation.
 * @return Config-shaped record.
 */
ConfigValue classified_record(std::string category, std::string message) {
  ConfigValue record = ConfigValue::object();
  record.set("category", ConfigValue::string(std::move(category)));
  record.set("message", ConfigValue::string(std::move(message)));
  return record;
}

/**
 * @brief Build one run record from a completed core trace.
 * @param plan Semantic execution plan.
 * @param repetition Zero-based repetition index.
 * @param trace Observable execution trace.
 * @param report Validation report from the untimed validation phase.
 * @param statistics Duration statistics for the selected raw basis.
 * @param performance Throughput and real-time metrics.
 * @param processing_seconds Total DSP processing seconds for this repetition.
 * @return Config-shaped run record.
 */
ConfigValue run_record(const RunPlan &plan, std::uint32_t repetition, const ExecutionTrace &trace,
                       const ValidationReport &report, const DurationStatistics &statistics,
                       const PerformanceMetrics &performance, double processing_seconds) {
  ConfigValue run = ConfigValue::object();
  run.set("id", ConfigValue::string("run-" + std::to_string(repetition + 1U)));

  ConfigValue point = ConfigValue::object();
  point.set("workload_level",
            ConfigValue::string(std::string(workload_level_name(plan.workload.level))));
  ConfigValue processors = ConfigValue::array();
  std::for_each(plan.workload.processors.begin(), plan.workload.processors.end(),
                [&processors](const std::string &processor) {
                  processors.push_back(ConfigValue::string(processor));
                });
  point.set("processors", std::move(processors));
  point.set("sample_rate_hz", ConfigValue::real(plan.sample_rate_hz));
  point.set("block_size", ConfigValue::integer(static_cast<std::int64_t>(plan.block_size)));
  point.set("channels", ConfigValue::integer(static_cast<std::int64_t>(plan.channels)));
  point.set("instances", ConfigValue::integer(static_cast<std::int64_t>(plan.instances)));
  point.set("oversampling", ConfigValue::integer(static_cast<std::int64_t>(plan.oversampling)));
  run.set("test_point", std::move(point));
  run.set("config_snapshot_ref", ConfigValue::string("config_snapshot"));
  run.set("timing", timing_object(trace));

  ConfigValue metrics = ConfigValue::array();
  const ConfigValue duration_meta =
      duration_context(statistics.basis, statistics.sample_count, statistics.percentile_method);
  metrics.push_back(metric_record("MET-01", "Execution time",
                                  MetricValue{ValueStatus::measured, processing_seconds, {}}, "s",
                                  "instrumented_dsp", statistics.percentile_method, duration_meta));
  std::vector<ConfigValue> duration = duration_metrics(statistics);
  std::for_each(duration.begin(), duration.end(),
                [&metrics](ConfigValue &metric) { metrics.push_back(std::move(metric)); });
  ConfigValue performance_meta = ConfigValue::object();
  performance_meta.set("frames", ConfigValue::integer(static_cast<std::int64_t>(plan.frames)));
  performance_meta.set("block_size",
                       ConfigValue::integer(static_cast<std::int64_t>(plan.block_size)));
  performance_meta.set("raw_basis",
                       ConfigValue::string(std::string(raw_basis_name(statistics.basis))));
  metrics.push_back(metric_record("MET-11", "Throughput", performance.throughput_fps, "frames/s",
                                  "instrumented_dsp", "frames / processing_seconds",
                                  performance_meta));
  metrics.push_back(metric_record("MET-11.samples", "Sample throughput", performance.throughput_sps,
                                  "samples/s", "instrumented_dsp", "frame_throughput * channels",
                                  ConfigValue::object()));
  metrics.push_back(metric_record("MET-11.blocks", "Block throughput", performance.throughput_bps,
                                  "blocks/s", "instrumented_dsp", "frame_throughput / block_size",
                                  ConfigValue::object()));
  metrics.push_back(metric_record("MET-12.audio", "Audio duration", performance.audio_duration, "s",
                                  "instrumented_dsp", "frames / sample_rate_hz",
                                  ConfigValue::object()));
  metrics.push_back(metric_record("MET-12", "Real-time factor", performance.real_time_factor, "",
                                  "instrumented_dsp", "processing_seconds / audio_duration",
                                  ConfigValue::object()));

  ConfigValue overhead_meta = ConfigValue::object();
  overhead_meta.set("interval_count",
                    ConfigValue::integer(static_cast<std::int64_t>(trace.overhead.interval_count)));
  overhead_meta.set("dsp_time_seconds", ConfigValue::real(trace.overhead.dsp_time_seconds));
  const auto percent = overhead_percent(trace.overhead);
  overhead_meta.set("percent",
                    percent.has_value() ? ConfigValue::real(*percent) : ConfigValue::null());
  metrics.push_back(metric_record("MET-17", "Benchmark overhead estimate",
                                  MetricValue{ValueStatus::estimated, trace.overhead.seconds, {}},
                                  "s", "empty_interval_calibration", trace.overhead.method,
                                  std::move(overhead_meta)));
  run.set("metrics", std::move(metrics));
  run.set("validation", validation_object(report, plan.tolerance));

  ConfigValue execution = ConfigValue::object();
  execution.set("isolation_request", ConfigValue::string(plan.isolation_request));
  execution.set("mode", ConfigValue::string(plan.isolation));
  execution.set("workers", ConfigValue::integer(static_cast<std::int64_t>(plan.workers)));
  execution.set("affinity", ConfigValue::string(plan.affinity));
  run.set("execution", std::move(execution));

  ConfigValue failures = ConfigValue::array();
  if (!report.all_passed()) {
    failures.push_back(classified_record("workload_failure", "validation checks failed"));
  }
  run.set("warnings", ConfigValue::array());
  run.set("failures", std::move(failures));
  run.set("status", ConfigValue::string(std::string(run_status_token(
                        report.all_passed() ? RunStatus::complete : RunStatus::failed))));
  return run;
}

/**
 * @brief Emit one copied progress observation when an observer is registered.
 * @param context Session context that may carry a progress observer.
 * @param phase Lifecycle phase to report.
 * @param test_point One-based test point, or zero when none is active.
 * @param repetition One-based repetition, or zero when none is active.
 * @param repetition_count Total configured repetitions, or zero.
 * @param plan Resolved run plan used for timing and isolation tokens, or null.
 * @param message Human-readable context copied into the observation.
 */
void emit_progress(const SessionContext &context, SessionPhase phase, std::uint32_t test_point,
                   std::uint32_t repetition, std::uint32_t repetition_count, const RunPlan *plan,
                   std::string message) {
  if (!context.progress) {
    return;
  }
  SessionProgress progress;
  progress.phase = phase;
  progress.test_point = test_point;
  progress.repetition = repetition;
  progress.repetition_count = repetition_count;
  if (plan != nullptr) {
    progress.timing_level = plan->timing_levels.empty()
                                ? std::string("unknown")
                                : std::string(timing_level_name(plan->timing_levels.front()));
    progress.isolation = plan->isolation;
  }
  progress.message = std::move(message);
  context.progress(progress);
}

/**
 * @brief Record a terminal failure and emit its progress observation.
 * @param context Session context that may carry a progress observer.
 * @param error Optional receiver for the failure explanation.
 * @param message Human-readable failure explanation.
 * @return Always `std::nullopt`, so callers can return this value directly.
 */
std::optional<ConfigValue> fail_run(const SessionContext &context, std::string *error,
                                    std::string message) {
  emit_progress(context, SessionPhase::failed, 0U, 0U, 0U, nullptr, message);
  record_error(error, std::move(message));
  return std::nullopt;
}

} // namespace detail
} // namespace

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
bool validate_run_config(const ConfigValue &config, std::string *error) {
  if (!validate_config_document(config, error)) {
    return false;
  }
  return detail::make_run_plan(config, error).has_value();
}

std::optional<ConfigValue> run_configured_session(const ConfigValue &config,
                                                  const SessionContext &context,
                                                  std::string *error) {
  if (!context.clock || !context.machine_metadata || !context.session_id || !context.timestamp) {
    return detail::fail_run(context, error, "session context is incomplete");
  }
  if (!validate_run_config(config, error)) {
    const std::string message =
        error != nullptr && !error->empty() ? *error : "config is not runnable";
    return detail::fail_run(context, error, message);
  }
  const auto plan = detail::make_run_plan(config, error);
  if (!plan.has_value()) {
    const std::string message =
        error != nullptr && !error->empty() ? *error : "execution plan could not be built";
    return detail::fail_run(context, error, message);
  }
  detail::emit_progress(context, SessionPhase::validating, 1U, 0U, plan->profile.repetitions,
                        &*plan, "validating portable config");

  const std::string session = context.session_id();
  const std::string started = context.timestamp();
  const std::string ended = context.timestamp();
  // The two timestamp callbacks are independent injected services even when a
  // test supplies the same constant for both; cppcheck cannot see that boundary.
  // cppcheck-suppress knownConditionTrueFalse
  if (session.empty() || started.empty() || ended.empty()) {
    return detail::fail_run(context, error,
                            "session context produced an empty identifier or timestamp");
  }

  detail::WorkSink sink;
  const OverheadEstimate overhead = estimate_overhead(context.clock, 8U, error);
  if (overhead.status != ValueStatus::estimated) {
    return detail::fail_run(context, error, "clock overhead calibration failed");
  }
  detail::emit_progress(context, SessionPhase::running, 1U, 0U, plan->profile.repetitions, &*plan,
                        "starting measured repetitions");

  bool all_validations_passed = true;
  ConfigValue runs = ConfigValue::array();
  for (std::uint32_t repetition = 0U; repetition < plan->profile.repetitions; ++repetition) {
    std::vector<double> input;
    std::vector<double> output;
    ValidationReport report;
    std::string phase_error;

    SequenceActions actions;
    actions.generation = [&plan, &input]() {
      input = generate_samples(*(plan->generator), plan->sample_rate_hz,
                               static_cast<std::size_t>(plan->frames));
    };
    actions.setup = [&input, &output]() { output.assign(input.size(), 0.0); };
    actions.warmup = [&plan, &input, &output, &sink]() {
      detail::process_block(input, 0U, output, plan->block_size, sink);
    };
    actions.block = [&plan, &input, &output, &sink](std::size_t index) {
      detail::process_block(input, index, output, plan->block_size, sink);
    };
    actions.validation = [&input, &output, &plan, &report, &phase_error]() {
      ValidationCase output_case;
      output_case.label = "builtin-output";
      output_case.actual = output;
      output_case.reference = input;
      output_case.reference_source = ReferenceSource::deterministic_generation;
      const auto validated = run_validation(
          {output_case}, ValidationOptions{plan->profile.validation_level, plan->tolerance, {}},
          &phase_error);
      if (validated.has_value()) {
        report = *validated;
      }
    };

    detail::emit_progress(context, SessionPhase::running, 1U, repetition + 1U,
                          plan->profile.repetitions, &*plan,
                          "repetition " + std::to_string(repetition + 1U) + "/" +
                              std::to_string(plan->profile.repetitions));

    SequenceOptions options;
    options.warmup_iterations = plan->profile.warmup_iterations;
    options.timing_levels = plan->timing_levels;
    options.block_count = plan->block_count;
    options.overhead_estimate = overhead;
    const auto trace = run_sequence(actions, options, context.clock, &phase_error);
    if (!trace.has_value()) {
      return detail::fail_run(context, error, "execution failed: " + phase_error);
    }
    if (!phase_error.empty()) {
      return detail::fail_run(context, error,
                              "validation phase failed to configure: " + phase_error);
    }

    const std::vector<double> *basis = find_samples(*trace, TimingLevel::per_block);
    RawBasis raw_basis = RawBasis::per_block;
    if (basis == nullptr) {
      basis = find_samples(*trace, TimingLevel::whole_run);
      raw_basis = RawBasis::run_repetition;
    }
    if (basis == nullptr || basis->empty()) {
      return detail::fail_run(context, error, "execution produced no aggregate timing samples");
    }
    const auto mean = mean_seconds(*basis);
    if (!mean.has_value()) {
      return detail::fail_run(context, error, "duration samples are not finite");
    }
    const double processing_seconds = raw_basis == RawBasis::per_block
                                          ? std::accumulate(basis->begin(), basis->end(), 0.0)
                                          : *mean;
    const auto statistics = compute_duration_statistics(*basis, raw_basis, &phase_error);
    if (!statistics.has_value()) {
      return detail::fail_run(context, error, "duration aggregation failed: " + phase_error);
    }
    const auto performance =
        compute_performance_metrics(plan->frames, processing_seconds, plan->channels,
                                    plan->block_size, plan->sample_rate_hz, &phase_error);
    if (!performance.has_value()) {
      return detail::fail_run(context, error, "performance metrics failed: " + phase_error);
    }
    if (!report.all_passed()) {
      all_validations_passed = false;
    }
    runs.push_back(detail::run_record(*plan, repetition, *trace, report, *statistics, *performance,
                                      processing_seconds));
  }
  static_cast<void>(sink.checksum.load());

  ConfigValue document = ConfigValue::object();
  document.set("schema_version", ConfigValue::string(std::string(kResultSchemaVersion)));
  document.set("sdk_version", *config.find("sdk_version"));
  ConfigValue session_object = ConfigValue::object();
  session_object.set("id", ConfigValue::string(session));
  session_object.set("started_at", ConfigValue::string(started));
  session_object.set("ended_at", ConfigValue::string(ended));
  document.set("session", std::move(session_object));
  document.set("config_snapshot", config);
  document.set("build", ConfigValue::object());
  document.set("machine", ConfigValue::object());
  document.set("environment", ConfigValue::object());
  document.set("runs", std::move(runs));

  const std::string warning_text =
      "built-in reference workload used; no integration plugin was loaded";
  detail::emit_progress(context, SessionPhase::warning, 1U, 0U, plan->profile.repetitions, &*plan,
                        warning_text);
  ConfigValue warnings = ConfigValue::array();
  warnings.push_back(detail::classified_record("execution", warning_text));
  if (!all_validations_passed) {
    const std::string validation_warning = "validation checks failed; run is incomplete";
    detail::emit_progress(context, SessionPhase::warning, 1U, 0U, plan->profile.repetitions, &*plan,
                          validation_warning);
    warnings.push_back(detail::classified_record("validation", validation_warning));
  }
  document.set("warnings", std::move(warnings));
  document.set("failures", ConfigValue::array());
  document.set("comparisons", ConfigValue::array());
  document.set("status",
               ConfigValue::string(std::string(session_status_token(
                   all_validations_passed ? SessionStatus::complete : SessionStatus::incomplete))));

  const ConfigValue environment = environment_metadata(config);
  if (!attach_metadata(document, build_metadata(), context.machine_metadata(), environment,
                       error)) {
    const std::string message = error != nullptr && !error->empty() ? *error : "metadata failed";
    return detail::fail_run(context, error, message);
  }
  if (!finalize_result_document(document, error)) {
    const std::string message =
        error != nullptr && !error->empty() ? *error : "result document failed validation";
    return detail::fail_run(context, error, message);
  }
  detail::emit_progress(context,
                        all_validations_passed ? SessionPhase::complete : SessionPhase::incomplete,
                        1U, 0U, plan->profile.repetitions, &*plan,
                        all_validations_passed ? "run completed" : "run completed with failures");
  return document;
}

} // namespace ztick
