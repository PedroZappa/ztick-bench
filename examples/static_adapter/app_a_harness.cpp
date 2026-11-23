// SPDX-License-Identifier: MIT
/**
 * @file app_a_harness.cpp
 * @brief Host implementation of the static App A end-to-end harness.
 *
 * The harness performs each SDK lifecycle step in its specified order and keeps
 * generation, preparation, warm-up and validation outside the primary DSP
 * interval. It runs a processor workload and a chain workload on independent
 * adapter instances. Expected outputs are computed in the host from the written
 * contract so reference validation is not tautological (TESTING_STRATEGY §2).
 */

#include <app_a_harness.hpp>

#include <app_a_contract.hpp>
#include <static_adapter.hpp>

#include <ztick/sdk.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/** @brief Read-only descriptor published by the static App A adapter. */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor;

namespace ztick::example::app_a {
namespace {

/** @brief Constant pi used by harness-side reference generation. */
constexpr double kPi = 3.14159265358979323846;

/** @brief First deterministic signal component, in Hz. */
constexpr double kToneOneHz = 440.0;
/** @brief Second deterministic signal component, in Hz. */
constexpr double kToneTwoHz = 1000.0;
/** @brief Peak amplitude of the first signal component. */
constexpr double kToneOneAmplitude = 0.35;
/** @brief Peak amplitude of the second signal component. */
constexpr double kToneTwoAmplitude = 0.15;
/** @brief Channel phase offset of the second channel, in radians. */
constexpr double kChannelPhase = 0.25;

/** @brief Owning storage for the single interleaved plane used by App A. */
struct ViewStorage {
  /** @brief One plane pointer array referenced by the buffer view. */
  void *planes[1]{nullptr};
};

/** @brief Owning context for metrics received through the SDK sink. */
struct MetricCollector {
  /** @brief Metrics retained in callback order. */
  std::vector<MetricReport> metrics;
};

/**
 * @brief Build one interleaved float32 buffer view over caller-owned storage.
 * @param samples Vector containing at least `frame_offset + frames` frames.
 * @param frame_offset Offset to the first exposed frame.
 * @param frames Number of exposed frames.
 * @param storage Storage that keeps the plane pointer array alive.
 * @return Buffer view with initialized C ABI headers.
 */
// cppcheck-suppress constParameterReference
auto make_view(std::vector<float> &samples, std::size_t frame_offset, std::uint32_t frames,
               ViewStorage &storage) noexcept -> ztick_buffer_view {
  const std::size_t sample_offset = frame_offset * kChannels;
  storage.planes[0] = samples.data() + sample_offset;
  const ztick_audio_format format{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                  ZTICK_STRUCT_LAYOUT_VERSION,
                                  ztick_sample_format_float32,
                                  ztick_channel_layout_interleaved,
                                  kSampleRateHz,
                                  kChannels,
                                  frames};
  return ztick_buffer_view{static_cast<std::uint32_t>(sizeof(ztick_buffer_view)),
                           ZTICK_STRUCT_LAYOUT_VERSION,
                           storage.planes,
                           nullptr,
                           1U,
                           frames,
                           format};
}

/**
 * @brief Generate deterministic stereo input samples outside timed processing.
 * @param frames Number of frames to generate.
 * @return Interleaved double samples with independently phase-shifted channels.
 */
auto generate_input(std::uint32_t frames) -> std::vector<double> {
  const std::size_t count = static_cast<std::size_t>(frames) * kChannels;
  std::vector<double> samples(count, 0.0);
  for (std::uint32_t frame = 0U; frame < frames; ++frame) {
    const double phase = kPi * 2.0 * static_cast<double>(frame);
    const double first = phase * kToneOneHz / static_cast<double>(kSampleRateHz);
    const double second = phase * kToneTwoHz / static_cast<double>(kSampleRateHz);
    for (std::uint32_t channel = 0U; channel < kChannels; ++channel) {
      const double offset = static_cast<double>(channel) * kChannelPhase;
      samples[static_cast<std::size_t>(frame) * kChannels + channel] =
          kToneOneAmplitude * std::sin(first + offset) + kToneTwoAmplitude * std::sin(second);
    }
  }
  return samples;
}

/**
 * @brief Convert double samples to the declared float32 storage format.
 * @param samples Double samples to convert.
 * @return Narrowed samples in the same order.
 */
auto to_float(const std::vector<double> &samples) -> std::vector<float> {
  std::vector<float> converted(samples.size());
  std::transform(samples.begin(), samples.end(), converted.begin(),
                 [](double value) noexcept { return static_cast<float>(value); });
  return converted;
}

/**
 * @brief Promote narrow samples back to double for independent calculation.
 * @param samples Float samples to promote.
 * @return Double samples in the same order.
 */
auto to_double(const std::vector<float> &samples) -> std::vector<double> {
  std::vector<double> promoted(samples.size());
  std::transform(samples.begin(), samples.end(), promoted.begin(),
                 [](float value) noexcept { return static_cast<double>(value); });
  return promoted;
}

/**
 * @brief Compute the expected output independently of adapter code.
 * @param input Quantized interleaved input samples.
 * @param smoothing Whether the one-pole smoothing stage is active.
 * @param gain Multiplicative gain of each stage.
 * @param stages Number of gain stages applied in series.
 * @param cutoff_hz Smoothing cutoff frequency in Hz; ignored without smoothing.
 * @return Expected interleaved output for the supplied parameters.
 * @note The formula is written from the harness contract, not obtained from the
 *       adapter, so a reference-comparison failure cannot be self-confirming.
 */
auto compute_reference(const std::vector<double> &input, bool smoothing, double gain,
                       std::int64_t stages, double cutoff_hz) -> std::vector<double> {
  const double multiplier = std::pow(gain, static_cast<double>(stages));
  const double alpha =
      smoothing ? 1.0 - std::exp(-2.0 * kPi * cutoff_hz / static_cast<double>(kSampleRateHz)) : 0.0;
  std::vector<double> state(kChannels, 0.0);
  std::vector<double> expected(input.size(), 0.0);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    const std::size_t channel = index % kChannels;
    const double gained = input[index] * multiplier;
    if (smoothing) {
      state[channel] += alpha * (gained - state[channel]);
      expected[index] = state[channel];
    } else {
      expected[index] = gained;
    }
  }
  return expected;
}

/**
 * @brief Initialize one SDK validation request with the App A tolerances.
 * @param request Request to initialize.
 * @param check_name Stable check identifier.
 * @param reference_source Reference source recorded with the request.
 */
auto initialize_request(ztick_validation_result &request, const char *check_name,
                        const char *reference_source) noexcept -> void {
  request = ztick_validation_result{};
  request.size = static_cast<std::uint32_t>(sizeof(request));
  request.version = ZTICK_STRUCT_LAYOUT_VERSION;
  request.check_name = check_name;
  request.absolute_tolerance = kAbsoluteTolerance;
  request.relative_tolerance = kRelativeTolerance;
  request.reference_source = reference_source;
}

/**
 * @brief Collect one reported metric into the sink's owning context.
 * @param context Pointer to a @ref MetricCollector; must not be NULL.
 * @param name Stable metric name.
 * @param value Reported value.
 * @param unit Reported unit, or NULL.
 * @param status Reported provenance label.
 * @param source Reported collection method, or NULL.
 * @return `ztick_status_success` when the metric was retained.
 */
auto metric_sink(void *context, const char *name, double value, const char *unit,
                 ztick_metric_status status, const char *source) -> std::int32_t {
  if (context == nullptr || name == nullptr) {
    return ztick_status_invalid_argument;
  }
  try {
    auto &collector = *static_cast<MetricCollector *>(context);
    collector.metrics.push_back(MetricReport{name, value, unit == nullptr ? std::string{} : unit,
                                             status, source == nullptr ? std::string{} : source});
    return ztick_status_success;
  } catch (...) {
    return ztick_status_internal_error;
  }
}

/**
 * @brief Release an adapter handle on every exit path.
 */
struct AdapterGuard {
  /** @brief Lifecycle table that owns the destroy callback. */
  const ztick_adapter_ops *ops{nullptr};
  /** @brief Handle to release, or NULL. */
  ztick_adapter *adapter{nullptr};

  /**
   * @brief Take ownership of one adapter handle.
   * @param ops_in Lifecycle table that owns the destroy callback.
   * @param adapter_in Handle to release, or NULL.
   */
  AdapterGuard(const ztick_adapter_ops *ops_in, ztick_adapter *adapter_in) noexcept
      : ops(ops_in), adapter(adapter_in) {}

  /** @brief Destroy the retained adapter when the guard goes out of scope. */
  ~AdapterGuard() {
    if (adapter != nullptr && ops != nullptr && ops->destroy != nullptr) {
      (void)ops->destroy(adapter);
    }
  }

  /** @brief Copying an owner of an adapter handle is disabled. */
  AdapterGuard(const AdapterGuard &) = delete;

  /** @brief Copy assignment of an adapter handle owner is disabled. */
  AdapterGuard &operator=(const AdapterGuard &) = delete;
};

/**
 * @brief Find one metric by name and provenance label.
 * @param metrics Metrics to search.
 * @param name Stable metric name.
 * @param status Required provenance label.
 * @return Pointer to the retained metric, or NULL when absent.
 */
auto find_metric(const std::vector<MetricReport> &metrics, const char *name,
                 ztick_metric_status status) noexcept -> const MetricReport * {
  const auto found = std::find_if(metrics.begin(), metrics.end(),
                                  [name, status](const MetricReport &metric) noexcept {
                                    return metric.name == name && metric.status == status;
                                  });
  return found == metrics.end() ? nullptr : &*found;
}

/**
 * @brief Run the complete SDK lifecycle for one App A workload.
 * @param descriptor Static descriptor whose operation table owns the lifecycle.
 * @param kind Workload variant to execute.
 * @return Observed workload result, including validation and metric evidence.
 * @note Each call creates its own adapter so the two workloads prove that
 *       instance state is independent.
 */
auto execute_workload(const ztick_adapter_descriptor &descriptor,
                      WorkloadKind kind) -> WorkloadResult {
  WorkloadResult result;
  result.kind = kind;
  auto expect = [&result](bool condition, const char *message) noexcept -> bool {
    if (!condition && result.error.empty()) {
      result.error = message;
    }
    return condition;
  };

  const ztick_adapter_ops &ops = descriptor.ops;
  ztick_adapter *raw_adapter = nullptr;
  if (!expect(ops.create(&raw_adapter) == ztick_status_success && raw_adapter != nullptr,
              "adapter creation failed")) {
    return result;
  }
  AdapterGuard guard{&ops, raw_adapter};

  const bool chain = kind == WorkloadKind::chain;
  const std::uint32_t workload_selection = chain ? 1U : 0U;
  const std::uint32_t smoothing = chain && kSmoothing ? 1U : 0U;
  const std::int64_t stages = kStages;
  const double gain = kGain;
  const double cutoff = kCutoffHz;
  const std::string label = chain ? "app_a_chain" : "app_a_processor";
  const bool configured =
      ops.configure(raw_adapter, kParameterWorkload, &workload_selection,
                    sizeof(workload_selection)) == ztick_status_success &&
      ops.configure(raw_adapter, kParameterGain, &gain, sizeof(gain)) == ztick_status_success &&
      ops.configure(raw_adapter, kParameterStages, &stages, sizeof(stages)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterSmoothing, &smoothing, sizeof(smoothing)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterCutoffHz, &cutoff, sizeof(cutoff)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterLabel, label.c_str(), label.size() + 1U) ==
          ztick_status_success;
  if (!expect(configured, "typed configuration failed")) {
    return result;
  }

  const std::uint32_t total_frames = kBlockCount * kFramesPerBlock;
  const std::vector<double> generated = generate_input(total_frames);
  std::vector<float> input = to_float(generated);
  const std::vector<double> expected =
      compute_reference(to_double(input), chain, kGain, kStages, kCutoffHz);
  std::vector<float> reference = to_float(expected);
  std::vector<float> output(static_cast<std::size_t>(total_frames) * kChannels, 0.0F);
  std::vector<float> warmup_output(static_cast<std::size_t>(kFramesPerBlock) * kChannels, 0.0F);

  ViewStorage full_input_storage{};
  ViewStorage reference_storage{};
  ViewStorage output_storage{};
  ViewStorage warmup_input_storage{};
  ViewStorage warmup_output_storage{};
  ViewStorage block_input_storage{};
  const ztick_buffer_view input_view = make_view(input, 0U, total_frames, full_input_storage);
  const ztick_buffer_view reference_view =
      make_view(reference, 0U, total_frames, reference_storage);
  if (!expect(ops.prepare(raw_adapter, &input_view, &reference_view) == ztick_status_success,
              "prepare failed")) {
    return result;
  }

  const ztick_buffer_view warmup_input =
      make_view(input, 0U, kFramesPerBlock, warmup_input_storage);
  ztick_buffer_view warmup_output_view =
      make_view(warmup_output, 0U, kFramesPerBlock, warmup_output_storage);
  if (!expect(ops.process(raw_adapter, &warmup_input, &warmup_output_view, nullptr) ==
                  ztick_status_success,
              "warm-up processing failed")) {
    return result;
  }
  if (!expect(ops.prepare(raw_adapter, &input_view, &reference_view) == ztick_status_success,
              "reset after warm-up failed")) {
    return result;
  }

  const auto start = std::chrono::steady_clock::now();
  for (std::uint32_t block = 0U; block < kBlockCount; ++block) {
    const std::size_t offset = static_cast<std::size_t>(block) * kFramesPerBlock;
    const ztick_buffer_view block_input =
        make_view(input, offset, kFramesPerBlock, block_input_storage);
    ztick_buffer_view block_output = make_view(output, offset, kFramesPerBlock, output_storage);
    ztick_timing_sample timing{static_cast<std::uint32_t>(sizeof(ztick_timing_sample)),
                               ZTICK_STRUCT_LAYOUT_VERSION, 0U, 0.0};
    if (!expect(ops.process(raw_adapter, &block_input, &block_output, &timing) ==
                    ztick_status_success,
                "primary processing failed")) {
      return result;
    }
    if (!expect(timing.block_index == block && timing.duration_seconds >= 0.0,
                "per-block timing sample is invalid")) {
      return result;
    }
    result.per_block_seconds_sum += timing.duration_seconds;
  }
  const auto stop = std::chrono::steady_clock::now();
  result.whole_run_seconds = std::chrono::duration<double>(stop - start).count();
  result.block_count = kBlockCount;
  result.processed_frames = total_frames;

  if (!expect(ops.finish(raw_adapter) == ztick_status_success, "finish failed")) {
    return result;
  }

  std::array<ztick_validation_result, 2U> basic_request{};
  initialize_request(basic_request[0U], kCheckFinite, "adapter_data");
  initialize_request(basic_request[1U], kCheckRange, "adapter_data");
  std::array<ztick_validation_result, 2U> basic_results{};
  std::uint32_t basic_capacity = static_cast<std::uint32_t>(basic_results.size());
  const std::int32_t basic_status = ops.validate(raw_adapter, basic_request.data(),
                                                 static_cast<std::uint32_t>(basic_request.size()),
                                                 basic_results.data(), &basic_capacity);
  if (!expect(basic_status == ztick_status_success && basic_capacity == basic_request.size(),
              "Basic validation failed")) {
    return result;
  }
  for (std::size_t index = 0U; index < basic_request.size(); ++index) {
    if (!expect(basic_results[index].outcome == ztick_status_success,
                "a Basic validation check failed")) {
      return result;
    }
  }

  std::array<ztick_validation_result, 5U> standard_request{};
  initialize_request(standard_request[0U], kCheckFinite, "adapter_data");
  initialize_request(standard_request[1U], kCheckRange, "adapter_data");
  initialize_request(standard_request[2U], kCheckReference, "deterministic_generation");
  initialize_request(standard_request[3U], kCheckExpectedChange, "deterministic_generation");
  initialize_request(standard_request[4U], kCheckCustom, "adapter_formula");
  std::array<ztick_validation_result, 5U> standard_results{};
  std::uint32_t standard_capacity = static_cast<std::uint32_t>(standard_results.size());
  const std::int32_t standard_status = ops.validate(
      raw_adapter, standard_request.data(), static_cast<std::uint32_t>(standard_request.size()),
      standard_results.data(), &standard_capacity);
  if (!expect(standard_status == ztick_status_success &&
                  standard_capacity == standard_request.size(),
              "Standard validation failed")) {
    return result;
  }
  for (std::size_t index = 0U; index < standard_request.size(); ++index) {
    const ztick_validation_result &outcome = standard_results[index];
    if (!expect(outcome.size >= sizeof(outcome) && outcome.version == ZTICK_STRUCT_LAYOUT_VERSION &&
                    outcome.check_name != nullptr &&
                    std::strcmp(outcome.check_name, standard_request[index].check_name) == 0 &&
                    outcome.outcome == ztick_status_success,
                "a Standard validation check failed")) {
      return result;
    }
    result.validation_results.push_back(ValidationReportEntry{
        outcome.check_name, outcome.outcome,
        outcome.details == nullptr ? std::string{} : outcome.details,
        outcome.reference_source == nullptr ? std::string{} : outcome.reference_source});
  }
  result.validation_check_count = standard_capacity;

  MetricCollector collector;
  if (!expect(ops.report(raw_adapter, metric_sink, &collector) == ztick_status_success,
              "report failed")) {
    return result;
  }
  if (!expect(collector.metrics.size() == 6U, "report emitted an unexpected metric count")) {
    return result;
  }
  if (!expect(find_metric(collector.metrics, kMetricFrames, ztick_metric_status_measured) !=
                      nullptr &&
                  find_metric(collector.metrics, kMetricBlocks, ztick_metric_status_measured) !=
                      nullptr &&
                  find_metric(collector.metrics, kMetricProcessingSeconds,
                              ztick_metric_status_measured) != nullptr &&
                  find_metric(collector.metrics, kMetricOutputPeak, ztick_metric_status_measured) !=
                      nullptr &&
                  find_metric(collector.metrics, kMetricOverheadSeconds,
                              ztick_metric_status_unavailable) != nullptr &&
                  find_metric(collector.metrics, kMetricScalingEfficiency,
                              ztick_metric_status_not_applicable) != nullptr,
              "report did not emit every value status")) {
    return result;
  }
  const MetricReport *frames =
      find_metric(collector.metrics, kMetricFrames, ztick_metric_status_measured);
  const MetricReport *blocks =
      find_metric(collector.metrics, kMetricBlocks, ztick_metric_status_measured);
  if (!expect(frames != nullptr && blocks != nullptr &&
                  frames->value == static_cast<double>(total_frames) &&
                  blocks->value == static_cast<double>(kBlockCount),
              "reported counters differ from the workload")) {
    return result;
  }
  result.metrics = std::move(collector.metrics);
  result.ok = result.error.empty();
  return result;
}

/**
 * @brief Execute the harness body, allowing exceptions to reach @ref run_harness.
 * @return The fully populated harness result.
 */
auto run_harness_impl() -> HarnessResult {
  HarnessResult result;
  result.required_capabilities = kRequiredCapabilities;
  result.execution_mode = ztick_execution_mode_in_process;

  auto expect = [&result](bool condition, const char *message) noexcept -> bool {
    if (!condition && result.error.empty()) {
      result.error = message;
    }
    return condition;
  };

  const ztick_adapter_descriptor &descriptor = ztick_test_app_static_descriptor;
  const bool descriptor_header_valid = descriptor.size >= sizeof(descriptor) &&
                                       descriptor.version == ZTICK_STRUCT_LAYOUT_VERSION &&
                                       descriptor.info.size >= sizeof(descriptor.info) &&
                                       descriptor.info.version == ZTICK_STRUCT_LAYOUT_VERSION &&
                                       descriptor.ops.size >= sizeof(descriptor.ops) &&
                                       descriptor.ops.version == ZTICK_STRUCT_LAYOUT_VERSION;
  if (!expect(descriptor_header_valid, "descriptor header is malformed")) {
    return result;
  }
  const ztick_adapter_ops &ops = descriptor.ops;
  const bool callbacks_complete =
      ops.create != nullptr && ops.destroy != nullptr && ops.describe != nullptr &&
      ops.configure != nullptr && ops.prepare != nullptr && ops.process != nullptr &&
      ops.finish != nullptr && ops.validate != nullptr && ops.report != nullptr;
  if (!expect(callbacks_complete, "descriptor has an incomplete lifecycle")) {
    return result;
  }
  if (!expect(std::strcmp(descriptor.info.identifier, kAdapterId) == 0,
              "descriptor identifier differs from the harness contract")) {
    return result;
  }

  ztick_version host_version{static_cast<std::uint32_t>(sizeof(ztick_version)),
                             ZTICK_STRUCT_LAYOUT_VERSION, 0U, 0U, 0U};
  const std::int32_t version_status = ztick_sdk_get_version(&host_version);
  result.sdk_version_status = version_status;
  if (!expect(version_status == ztick_status_success, "SDK version query failed")) {
    return result;
  }
  if (!expect(ztick_sdk_is_compatible(&descriptor.info.sdk_version) == ztick_status_success,
              "adapter SDK version is incompatible")) {
    return result;
  }

  const std::uint64_t unknown = descriptor.info.capabilities & ~ZTICK_CAPABILITY_KNOWN_MASK;
  const std::uint64_t missing = kRequiredCapabilities & ~descriptor.info.capabilities;
  if (!expect(unknown == 0U && missing == 0U, "required capabilities are not declared")) {
    return result;
  }

  const std::int32_t registration = ztick_test_app_static_register();
  result.registration_status = registration;
  if (!expect(registration == ztick_status_success || registration == ztick_status_invalid_argument,
              "static adapter registration failed")) {
    return result;
  }

  ztick_adapter *metadata_adapter = nullptr;
  if (!expect(ops.create(&metadata_adapter) == ztick_status_success && metadata_adapter != nullptr,
              "metadata instance creation failed")) {
    return result;
  }
  AdapterGuard guard{&ops, metadata_adapter};

  const ztick_audio_format *formats = nullptr;
  const ztick_parameter_descriptor *parameters = nullptr;
  const char *const *unsupported = nullptr;
  std::uint32_t format_count = 0U;
  std::uint32_t parameter_count = 0U;
  std::uint32_t unsupported_count = 0U;
  const std::int32_t describe_status =
      ops.describe(metadata_adapter, &formats, &format_count, &parameters, &parameter_count,
                   &unsupported, &unsupported_count);
  if (!expect(describe_status == ztick_status_success, "describe failed")) {
    return result;
  }
  result.format_count = format_count;
  result.parameter_count = parameter_count;
  result.unsupported_count = unsupported_count;
  if (!expect(formats != nullptr && format_count >= 4U && parameters != nullptr &&
                  parameter_count >= 6U && unsupported != nullptr && unsupported_count >= 4U,
              "describe returned incomplete declarations")) {
    return result;
  }
  bool declared_parameter_types = true;
  bool boolean_parameter = false;
  bool integer_parameter = false;
  bool floating_parameter = false;
  bool enumeration_parameter = false;
  bool string_parameter = false;
  for (std::uint32_t index = 0U; index < parameter_count; ++index) {
    declared_parameter_types = declared_parameter_types && parameters[index].name != nullptr &&
                               parameters[index].description != nullptr &&
                               parameters[index].size >= sizeof(parameters[index]) &&
                               parameters[index].version == ZTICK_STRUCT_LAYOUT_VERSION;
    switch (parameters[index].type) {
    case ztick_parameter_type_boolean:
      boolean_parameter = true;
      break;
    case ztick_parameter_type_integer:
      integer_parameter = true;
      break;
    case ztick_parameter_type_floating_point:
      floating_parameter = true;
      break;
    case ztick_parameter_type_enumeration:
      enumeration_parameter = true;
      break;
    case ztick_parameter_type_string:
      string_parameter = true;
      break;
    default:
      declared_parameter_types = false;
      break;
    }
  }
  if (!expect(declared_parameter_types && boolean_parameter && integer_parameter &&
                  floating_parameter && enumeration_parameter && string_parameter,
              "typed parameter declarations are incomplete")) {
    return result;
  }

  const std::array<WorkloadKind, 2U> kinds{WorkloadKind::processor, WorkloadKind::chain};
  for (const WorkloadKind kind : kinds) {
    WorkloadResult workload = execute_workload(descriptor, kind);
    if (!workload.ok && result.error.empty()) {
      result.error = workload.error;
    }
    result.block_count += workload.block_count;
    result.processed_frames += workload.processed_frames;
    result.validation_check_count += workload.validation_check_count;
    result.whole_run_seconds += workload.whole_run_seconds;
    result.per_block_seconds_sum += workload.per_block_seconds_sum;
    result.metrics.insert(result.metrics.end(), workload.metrics.begin(), workload.metrics.end());
    result.validation_results.insert(result.validation_results.end(),
                                     workload.validation_results.begin(),
                                     workload.validation_results.end());
    result.workloads.push_back(std::move(workload));
    if (!result.error.empty()) {
      break;
    }
  }

  if (!expect(result.workloads.size() == kinds.size(), "both workloads did not run")) {
    return result;
  }
  result.ok = result.error.empty();
  return result;
}

} // namespace

auto run_harness() noexcept -> HarnessResult {
  try {
    return run_harness_impl();
  } catch (...) {
    HarnessResult result;
    result.error = "harness threw an exception";
    return result;
  }
}

auto format_summary(const HarnessResult &result) -> std::string {
  std::ostringstream out;
  out << "app_a ok=" << (result.ok ? 1 : 0) << " execution=in_process"
      << " registration=" << result.registration_status << " workloads=" << result.workloads.size()
      << " formats=" << result.format_count << " parameters=" << result.parameter_count
      << " checks=" << result.validation_check_count << " blocks=" << result.block_count
      << " frames=" << result.processed_frames << " whole_run_s=" << std::fixed
      << std::setprecision(6) << result.whole_run_seconds << " metrics=" << result.metrics.size();
  if (!result.error.empty()) {
    out << " error=" << result.error;
  }
  return out.str();
}

} // namespace ztick::example::app_a
