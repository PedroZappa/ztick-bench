// SPDX-License-Identifier: MIT
/**
 * @file app_b_harness.cpp
 * @brief Host implementation of the dynamic App B end-to-end harness.
 *
 * Preflight is metadata-only: dynamic inspection, negotiation and trust all refuse
 * integration before a lifecycle callback can run. Only after those decisions does
 * the harness keep the module mapped, check its complete op table and invoke the
 * engine workload through a selected runner. The host computes its own reference
 * output so Strict validation cannot confirm adapter results against adapter data.
 */

#include <app_b_harness.hpp>

#include <app_b_contract.hpp>

#include <ztick/plugin/compatibility.hpp>
#include <ztick/plugin/dynamic_loader.hpp>
#include <ztick/plugin/dynamic_runtime.hpp>
#include <ztick/plugin/runner.hpp>
#include <ztick/plugin/trust.hpp>

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

namespace ztick::example::app_b {
namespace {

/** @brief Constant pi used by the host's independent reference generation. */
constexpr double kPi = 3.14159265358979323846;

/** @brief First deterministic source tone, in Hz. */
constexpr double kToneOneHz = 320.0;
/** @brief Second deterministic source tone, in Hz. */
constexpr double kToneTwoHz = 1280.0;
/** @brief Peak amplitude of the first source tone. */
constexpr double kToneOneAmplitude = 0.35;
/** @brief Peak amplitude of the second source tone. */
constexpr double kToneTwoAmplitude = 0.12;
/** @brief Per-channel phase offset of the first source tone, in radians. */
constexpr double kChannelPhase = 0.375;

/** @brief Reference source recorded for host-generated validation data. */
constexpr const char kHostReferenceSource[] = "host_generated_contract";

/** @brief Child argument selecting the App B engine mode. */
constexpr const char kModuleArgument[] = "--module";

/** @brief Owning storage for the single interleaved plane used by App B. */
struct ViewStorage {
  /** @brief One plane pointer array referenced by the buffer view. */
  void *planes[1]{nullptr};
};

/** @brief Owning context for metrics received through the SDK sink. */
struct MetricCollector {
  /** @brief Metrics retained in callback order. */
  std::vector<MetricReport> metrics;
};

/** @brief Detailed evidence produced by one in-process engine lifecycle. */
struct WorkloadOutcome {
  /** @brief First failed expectation, or empty on success. */
  std::string error;
  /** @brief Number of primary processing blocks. */
  std::uint32_t block_count{0U};
  /** @brief Number of processed frames across all channels. */
  std::uint64_t processed_frames{0U};
  /** @brief Number of Strict checks reported by the adapter. */
  std::uint32_t validation_check_count{0U};
  /** @brief Host-measured whole-workload DSP duration in seconds. */
  double whole_run_seconds{0.0};
  /** @brief Sum of adapter-emitted per-block durations in seconds. */
  double per_block_seconds_sum{0.0};
  /** @brief Metrics received through the report sink. */
  std::vector<MetricReport> metrics;
  /** @brief Strict validation outcomes received from the adapter. */
  std::vector<ValidationReportEntry> validation_results;
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
 * @brief Generate deterministic interleaved input outside timed processing.
 * @param frames Number of frames to generate.
 * @return Float32 samples with independently phase-shifted channels.
 */
auto generate_input(std::uint32_t frames) -> std::vector<float> {
  const std::size_t count = static_cast<std::size_t>(frames) * kChannels;
  std::vector<float> samples(count, 0.0F);
  for (std::uint32_t frame = 0U; frame < frames; ++frame) {
    const double phase = kPi * 2.0 * static_cast<double>(frame);
    const double first = phase * kToneOneHz / static_cast<double>(kSampleRateHz);
    const double second = phase * kToneTwoHz / static_cast<double>(kSampleRateHz);
    for (std::uint32_t channel = 0U; channel < kChannels; ++channel) {
      const double offset = static_cast<double>(channel) * kChannelPhase;
      const double value =
          kToneOneAmplitude * std::sin(first + offset) + kToneTwoAmplitude * std::sin(second);
      samples[static_cast<std::size_t>(frame) * kChannels + channel] = static_cast<float>(value);
    }
  }
  return samples;
}

/**
 * @brief Compute one expected engine sample from the written App B contract.
 * @param input Source sample for this frame and channel.
 * @param frame Global frame index in the whole run.
 * @param channel Channel index.
 * @param sample_index Global interleaved sample index.
 * @param voices Number of synthesized voices.
 * @param stages Number of shaping stages.
 * @param mix Mix of synthesized signal added at each stage.
 * @param final_quality Whether the saturating shaping curve is selected.
 * @param dither Whether deterministic dither is enabled.
 * @return Expected double-precision sample before float32 storage.
 * @note This is an independent host implementation of the published formula, never a
 *       call into the adapter under test.
 */
auto expected_sample(double input, std::uint64_t frame, std::uint32_t channel,
                     std::uint64_t sample_index, std::int64_t voices, std::int64_t stages,
                     double mix, bool final_quality, bool dither) noexcept -> double {
  double chorus = 0.0;
  for (std::int64_t voice = 0; voice < voices; ++voice) {
    const double frequency = kBaseFrequencyHz * static_cast<double>(voice + 1);
    const double phase =
        kPi * 2.0 * frequency * static_cast<double>(frame) / static_cast<double>(kSampleRateHz) +
        static_cast<double>(voice) * kVoicePhaseStep +
        static_cast<double>(channel) * kChannelPhaseStep;
    chorus += std::sin(phase) / static_cast<double>(voice + 1);
  }
  chorus /= static_cast<double>(voices);

  double value = input;
  const double stage_mix = mix / static_cast<double>(stages);
  for (std::int64_t stage = 0; stage < stages; ++stage) {
    value += stage_mix * chorus;
    if (final_quality) {
      value = std::tanh(value);
    } else {
      value = std::clamp(value, -kRangeLimit, kRangeLimit);
    }
  }
  if (dither) {
    value += kDitherAmplitude * std::sin(kPi * 2.0 * static_cast<double>(sample_index) / 97.0);
  }
  return kOutputScale * value;
}

/**
 * @brief Compute host-owned reference samples for the configured engine.
 * @param input Quantized interleaved input samples.
 * @return Float32 reference samples for the full run.
 */
auto compute_expected_output(const std::vector<float> &input) -> std::vector<float> {
  std::vector<float> expected(input.size(), 0.0F);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    const auto frame = static_cast<std::uint64_t>(index / kChannels);
    const auto channel = static_cast<std::uint32_t>(index % kChannels);
    const double value = expected_sample(input[index], frame, channel, index, kVoiceCount,
                                         kStageCount, kMix, true, kDither);
    expected[index] = static_cast<float>(value);
  }
  return expected;
}

/**
 * @brief Initialize one Strict validation request with App B tolerances.
 * @param request Request to initialize.
 * @param check_name Stable check identifier.
 */
auto initialize_request(ztick_validation_result &request, const char *check_name) noexcept -> void {
  request = ztick_validation_result{};
  request.size = static_cast<std::uint32_t>(sizeof(request));
  request.version = ZTICK_STRUCT_LAYOUT_VERSION;
  request.check_name = check_name;
  request.absolute_tolerance = kAbsoluteTolerance;
  request.relative_tolerance = kRelativeTolerance;
  request.reference_source = kHostReferenceSource;
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
 * @brief Report whether a lifecycle table implements every required callback.
 * @param ops Lifecycle table to inspect.
 * @return True when all nine callback pointers are non-NULL.
 */
auto has_all_callbacks(const ztick_adapter_ops &ops) noexcept -> bool {
  return ops.create != nullptr && ops.destroy != nullptr && ops.describe != nullptr &&
         ops.configure != nullptr && ops.prepare != nullptr && ops.process != nullptr &&
         ops.finish != nullptr && ops.validate != nullptr && ops.report != nullptr;
}

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
 * @brief Run the complete engine lifecycle for the dynamic adapter.
 * @param descriptor Loaded descriptor whose op table owns the lifecycle.
 * @param outcome Receives all detailed evidence from the run.
 * @return A `ztick_status` code describing the lifecycle outcome.
 * @note Input generation, warm-up, preparation, finish, validation and reporting are
 *       all outside the host's primary timed DSP interval.
 */
auto execute_engine_workload(const ztick_adapter_descriptor &descriptor,
                             WorkloadOutcome &outcome) -> std::int32_t {
  auto expect = [&outcome](bool condition, const char *message) noexcept -> bool {
    if (!condition && outcome.error.empty()) {
      outcome.error = message;
    }
    return condition;
  };

  const ztick_adapter_ops &ops = descriptor.ops;
  ztick_adapter *raw_adapter = nullptr;
  if (!expect(ops.create(&raw_adapter) == ztick_status_success && raw_adapter != nullptr,
              "adapter creation failed")) {
    return ztick_status_internal_error;
  }
  AdapterGuard guard{&ops, raw_adapter};

  const std::int64_t voices = kVoiceCount;
  const std::int64_t stages = kStageCount;
  const std::int64_t threads = kThreadCount;
  const double mix = kMix;
  const std::uint32_t dither = kDither ? 1U : 0U;
  const std::uint32_t quality = kQualityFinalIndex;
  const std::string profile = kProfile;
  const bool configured =
      ops.configure(raw_adapter, kParameterVoices, &voices, sizeof(voices)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterStages, &stages, sizeof(stages)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterThreads, &threads, sizeof(threads)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterMix, &mix, sizeof(mix)) == ztick_status_success &&
      ops.configure(raw_adapter, kParameterDither, &dither, sizeof(dither)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterQuality, &quality, sizeof(quality)) ==
          ztick_status_success &&
      ops.configure(raw_adapter, kParameterProfile, profile.c_str(), profile.size() + 1U) ==
          ztick_status_success;
  if (!expect(configured, "typed engine configuration failed")) {
    return ztick_status_invalid_argument;
  }

  const std::uint32_t total_frames = kBlockCount * kFramesPerBlock;
  std::vector<float> input = generate_input(total_frames);
  std::vector<float> expected = compute_expected_output(input);
  std::vector<float> output(static_cast<std::size_t>(total_frames) * kChannels, 0.0F);
  std::vector<float> warmup_output(static_cast<std::size_t>(kFramesPerBlock) * kChannels, 0.0F);

  ViewStorage input_storage{};
  ViewStorage expected_storage{};
  ViewStorage output_storage{};
  ViewStorage warmup_input_storage{};
  ViewStorage warmup_output_storage{};
  ViewStorage block_input_storage{};
  const ztick_buffer_view input_view = make_view(input, 0U, total_frames, input_storage);
  const ztick_buffer_view expected_view = make_view(expected, 0U, total_frames, expected_storage);
  if (!expect(ops.prepare(raw_adapter, &input_view, &expected_view) == ztick_status_success,
              "prepare failed")) {
    return ztick_status_invalid_argument;
  }

  const ztick_buffer_view warmup_input =
      make_view(input, 0U, kFramesPerBlock, warmup_input_storage);
  ztick_buffer_view warmup_output_view =
      make_view(warmup_output, 0U, kFramesPerBlock, warmup_output_storage);
  if (!expect(ops.process(raw_adapter, &warmup_input, &warmup_output_view, nullptr) ==
                  ztick_status_success,
              "warm-up processing failed")) {
    return ztick_status_processing_failure;
  }
  if (!expect(ops.prepare(raw_adapter, &input_view, &expected_view) == ztick_status_success,
              "reset after warm-up failed")) {
    return ztick_status_invalid_argument;
  }

  const auto started = std::chrono::steady_clock::now();
  for (std::uint32_t block = 0U; block < kBlockCount; ++block) {
    const std::size_t offset = static_cast<std::size_t>(block) * kFramesPerBlock;
    const ztick_buffer_view block_input =
        make_view(input, offset, kFramesPerBlock, block_input_storage);
    ztick_buffer_view block_output = make_view(output, offset, kFramesPerBlock, output_storage);
    ztick_timing_sample timing{static_cast<std::uint32_t>(sizeof(ztick_timing_sample)),
                               ZTICK_STRUCT_LAYOUT_VERSION, 0U, 0.0};
    if (!expect(ops.process(raw_adapter, &block_input, &block_output, &timing) ==
                    ztick_status_success,
                "primary engine processing failed")) {
      return ztick_status_processing_failure;
    }
    if (!expect(timing.block_index == block && timing.duration_seconds >= 0.0,
                "per-block timing sample is invalid")) {
      return ztick_status_invalid_argument;
    }
    outcome.per_block_seconds_sum += timing.duration_seconds;
  }
  const auto stopped = std::chrono::steady_clock::now();
  outcome.whole_run_seconds = std::chrono::duration<double>(stopped - started).count();
  outcome.block_count = kBlockCount;
  outcome.processed_frames = total_frames;

  if (!expect(ops.finish(raw_adapter) == ztick_status_success, "finish failed")) {
    return ztick_status_internal_error;
  }

  constexpr std::size_t kCheckCount = 6U;
  const std::array<const char *, kCheckCount> check_names{kCheckFinite,        kCheckRange,
                                                          kCheckReference,     kCheckExpectedChange,
                                                          kCheckThreadBalance, kCheckCustom};
  std::array<ztick_validation_result, kCheckCount> request{};
  std::array<ztick_validation_result, kCheckCount> results{};
  for (std::size_t index = 0U; index < kCheckCount; ++index) {
    initialize_request(request[index], check_names[index]);
  }
  std::uint32_t result_count = static_cast<std::uint32_t>(results.size());
  const std::int32_t validation_status =
      ops.validate(raw_adapter, request.data(), static_cast<std::uint32_t>(request.size()),
                   results.data(), &result_count);
  if (!expect(validation_status == ztick_status_success && result_count == request.size(),
              "Strict validation failed")) {
    return validation_status == ztick_status_success ? ztick_status_validation_failure
                                                     : validation_status;
  }
  for (std::size_t index = 0U; index < request.size(); ++index) {
    const ztick_validation_result &entry = results[index];
    if (!expect(entry.size >= sizeof(entry) && entry.version == ZTICK_STRUCT_LAYOUT_VERSION &&
                    entry.check_name != nullptr &&
                    std::strcmp(entry.check_name, request[index].check_name) == 0 &&
                    entry.outcome == ztick_status_success,
                "a Strict validation check failed")) {
      return ztick_status_validation_failure;
    }
    outcome.validation_results.push_back(ValidationReportEntry{
        entry.check_name, entry.outcome, entry.details == nullptr ? std::string{} : entry.details,
        entry.reference_source == nullptr ? std::string{} : entry.reference_source});
  }
  outcome.validation_check_count = result_count;

  MetricCollector collector;
  if (!expect(ops.report(raw_adapter, metric_sink, &collector) == ztick_status_success,
              "report failed")) {
    return ztick_status_internal_error;
  }
  constexpr std::size_t kMetricCount = 9U;
  if (!expect(collector.metrics.size() == kMetricCount,
              "report emitted an unexpected metric count")) {
    return ztick_status_validation_failure;
  }
  const bool statuses_present =
      find_metric(collector.metrics, kMetricFrames, ztick_metric_status_measured) != nullptr &&
      find_metric(collector.metrics, kMetricBlocks, ztick_metric_status_measured) != nullptr &&
      find_metric(collector.metrics, kMetricProcessingSeconds, ztick_metric_status_measured) !=
          nullptr &&
      find_metric(collector.metrics, kMetricWorkerSeconds, ztick_metric_status_measured) !=
          nullptr &&
      find_metric(collector.metrics, kMetricWorkerCalls, ztick_metric_status_measured) != nullptr &&
      find_metric(collector.metrics, kMetricOutputPeak, ztick_metric_status_measured) != nullptr &&
      find_metric(collector.metrics, kMetricScalingEfficiency, ztick_metric_status_unavailable) !=
          nullptr &&
      find_metric(collector.metrics, kMetricReferenceFiles, ztick_metric_status_not_applicable) !=
          nullptr &&
      find_metric(collector.metrics, "app_b.overhead_estimate_seconds",
                  ztick_metric_status_estimated) != nullptr;
  if (!expect(statuses_present, "report did not emit every value status")) {
    return ztick_status_validation_failure;
  }
  const MetricReport *frames =
      find_metric(collector.metrics, kMetricFrames, ztick_metric_status_measured);
  const MetricReport *blocks =
      find_metric(collector.metrics, kMetricBlocks, ztick_metric_status_measured);
  const MetricReport *worker_calls =
      find_metric(collector.metrics, kMetricWorkerCalls, ztick_metric_status_measured);
  const MetricReport *reference =
      find_metric(collector.metrics, kMetricReferenceFiles, ztick_metric_status_not_applicable);
  if (!expect(frames != nullptr && blocks != nullptr && worker_calls != nullptr &&
                  reference != nullptr && frames->value == static_cast<double>(total_frames) &&
                  blocks->value == static_cast<double>(kBlockCount) &&
                  worker_calls->value == static_cast<double>(kThreadCount * kBlockCount) &&
                  reference->value == 0.0,
              "reported App B counters differ from the workload")) {
    return ztick_status_validation_failure;
  }
  outcome.metrics = std::move(collector.metrics);
  return ztick_status_success;
}

/**
 * @brief Convert a trust decision into a stable numeric category.
 * @param status Trust decision to convert.
 * @return Integer matching the `TrustStatus` enumerator.
 */
auto trust_status_code(ztick::plugin::TrustStatus status) noexcept -> std::int32_t {
  return static_cast<std::int32_t>(status);
}

/**
 * @brief Apply the harness's explicit first-run consent policy.
 * @note The harness is non-interactive: first use of a test integration is explicitly
 *       approved, and the approval is persisted in an in-memory store for the run.
 */
class ApprovingPrompter final : public ztick::plugin::ConsentPrompter {
public:
  /**
   * @brief Approve first-run use after observing the displayed identity.
   * @param identity Metadata supplied for display; retained for observability.
   * @return An explicit approval decision.
   */
  auto confirm_first_run(const ztick::plugin::PluginIdentity &identity) const noexcept
      -> ztick::plugin::ConsentDecision override {
    ++prompts;
    last_identifier = identity.identifier;
    return ztick::plugin::ConsentDecision::approve;
  }

  /** @brief Number of prompts observed; mutable for the const interface. */
  mutable std::uint32_t prompts{0U};
  /** @brief Identifier from the most recent prompt; mutable for the const API. */
  mutable std::string last_identifier;
};

/**
 * @brief Copy detailed in-process workload evidence into the harness result.
 * @param outcome Workload evidence to copy.
 * @param result Harness result that receives the evidence.
 */
auto absorb_outcome(const WorkloadOutcome &outcome, HarnessResult &result) -> void {
  result.block_count = outcome.block_count;
  result.processed_frames = outcome.processed_frames;
  result.validation_check_count = outcome.validation_check_count;
  result.whole_run_seconds = outcome.whole_run_seconds;
  result.per_block_seconds_sum = outcome.per_block_seconds_sum;
  result.metrics = outcome.metrics;
  result.validation_results = outcome.validation_results;
}

/**
 * @brief Run the harness body, allowing exceptions to reach @ref run_harness.
 * @param module_path Path to the dynamic App B shared library.
 * @param isolation Requested isolation mode.
 * @param child_executable Path to the harness executable for child processes.
 * @return Fully populated harness result.
 */
auto run_harness_impl(const char *module_path, HarnessIsolation isolation,
                      const char *child_executable) -> HarnessResult {
  HarnessResult result;
  result.required_capabilities = kRequiredCapabilities;
  if (module_path == nullptr || module_path[0] == '\0') {
    result.error = "module path is required";
    return result;
  }

  auto expect = [&result](bool condition, const char *message) noexcept -> bool {
    if (!condition && result.error.empty()) {
      result.error = message;
    }
    return condition;
  };

  const ztick::plugin::InspectionResult inspection =
      ztick::plugin::inspect_dynamic_adapter(module_path);
  result.inspection_status = inspection.status;
  if (!expect(inspection.has_metadata(), "metadata-only inspection failed")) {
    if (inspection.reason != nullptr) {
      result.error = inspection.reason;
    }
    return result;
  }

  ztick_version host_version{static_cast<std::uint32_t>(sizeof(ztick_version)),
                             ZTICK_STRUCT_LAYOUT_VERSION, 0U, 0U, 0U};
  result.sdk_version_status = ztick_sdk_get_version(&host_version);
  if (!expect(result.sdk_version_status == ztick_status_success, "SDK version query failed")) {
    return result;
  }

  const ztick::plugin::NegotiationResult negotiation =
      ztick::plugin::negotiate_adapter(inspection.metadata, kRequiredCapabilities);
  result.negotiation_status = negotiation.status;
  if (!expect(negotiation.accepted(), "SDK version or capability negotiation failed")) {
    if (negotiation.reason != nullptr) {
      result.error = negotiation.reason;
    }
    return result;
  }

  ztick::plugin::MemoryTrustStore store;
  ApprovingPrompter prompter;
  ztick::plugin::TrustManager manager(store, &prompter);
  const ztick::plugin::TrustAuthorization authorization =
      manager.authorize(ztick::plugin::identity_from_metadata(inspection.metadata));
  result.trust_status = trust_status_code(authorization.status);
  result.trust_authorized = authorization.permits_execution();
  result.trust_reason =
      authorization.reason == nullptr ? std::string{} : std::string{authorization.reason};
  if (!expect(result.trust_authorized, "integration was not authorized")) {
    return result;
  }

  const ztick::plugin::RuntimeLoadResult loaded =
      ztick::plugin::load_dynamic_adapter_runtime(module_path);
  result.runtime_load_status = loaded.status;
  if (!expect(loaded.loaded(), "runtime module load failed")) {
    if (loaded.reason != nullptr) {
      result.error = loaded.reason;
    }
    return result;
  }
  const ztick_adapter_descriptor *descriptor = loaded.adapter.get();
  if (!expect(descriptor != nullptr && descriptor->info.identifier != nullptr &&
                  std::strcmp(descriptor->info.identifier, kAdapterId) == 0 &&
                  has_all_callbacks(descriptor->ops),
              "loaded descriptor identifier or lifecycle is invalid")) {
    return result;
  }
  const ztick::plugin::NegotiationResult loaded_negotiation =
      ztick::plugin::negotiate_adapter(*descriptor, kRequiredCapabilities);
  if (!expect(loaded_negotiation.accepted(), "loaded descriptor failed renegotiation")) {
    if (loaded_negotiation.reason != nullptr) {
      result.error = loaded_negotiation.reason;
    }
    return result;
  }

  ztick_adapter *metadata_adapter = nullptr;
  if (!expect(descriptor->ops.create(&metadata_adapter) == ztick_status_success &&
                  metadata_adapter != nullptr,
              "metadata instance creation failed")) {
    return result;
  }
  AdapterGuard metadata_guard{&descriptor->ops, metadata_adapter};
  const ztick_audio_format *formats = nullptr;
  const ztick_parameter_descriptor *parameters = nullptr;
  const char *const *unsupported = nullptr;
  std::uint32_t format_count = 0U;
  std::uint32_t parameter_count = 0U;
  std::uint32_t unsupported_count = 0U;
  if (!expect(descriptor->ops.describe(metadata_adapter, &formats, &format_count, &parameters,
                                       &parameter_count, &unsupported,
                                       &unsupported_count) == ztick_status_success,
              "describe failed")) {
    return result;
  }
  result.format_count = format_count;
  result.parameter_count = parameter_count;
  result.unsupported_count = unsupported_count;
  bool typed_parameters_complete = formats != nullptr && format_count >= 2U &&
                                   parameters != nullptr && parameter_count >= 7U &&
                                   unsupported != nullptr && unsupported_count >= 5U;
  bool boolean_parameter = false;
  bool integer_parameter = false;
  bool floating_parameter = false;
  bool enumeration_parameter = false;
  bool string_parameter = false;
  for (std::uint32_t index = 0U; typed_parameters_complete && index < parameter_count; ++index) {
    const ztick_parameter_descriptor &parameter = parameters[index];
    typed_parameters_complete = parameter.name != nullptr && parameter.description != nullptr &&
                                parameter.size >= sizeof(parameter) &&
                                parameter.version == ZTICK_STRUCT_LAYOUT_VERSION;
    switch (parameter.type) {
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
      typed_parameters_complete = false;
      break;
    }
  }
  if (!expect(typed_parameters_complete && boolean_parameter && integer_parameter &&
                  floating_parameter && enumeration_parameter && string_parameter,
              "typed engine parameter declarations are incomplete")) {
    return result;
  }

  ztick::plugin::IsolationRequest request = ztick::plugin::IsolationRequest::in_process;
  if (isolation == HarnessIsolation::out_of_process) {
    request = ztick::plugin::IsolationRequest::out_of_process;
  }
  const ztick::plugin::RunnerSelection selection = ztick::plugin::select_runner(
      {request, result.trust_authorized, ztick::plugin::child_process_backend_available()});
  result.runner_status = selection.status;
  if (!expect(selection.selectable(), "runner selection was refused")) {
    if (selection.reason != nullptr) {
      result.error = selection.reason;
    }
    return result;
  }
  if (selection.mode.has_value()) {
    result.execution_mode = *selection.mode;
  }

  ztick::plugin::RunRequest run_request;
  run_request.selection = selection;
  ztick::plugin::RunExecutionRecord record;
  if (result.execution_mode == ztick_execution_mode_in_process) {
    WorkloadOutcome outcome;
    run_request.workload = [descriptor, &outcome]() {
      return execute_engine_workload(*descriptor, outcome);
    };
    record = ztick::plugin::run_in_process(run_request);
    absorb_outcome(outcome, result);
    if (!record.succeeded() && !outcome.error.empty()) {
      result.error = outcome.error;
    }
  } else {
    if (!expect(child_executable != nullptr && child_executable[0] != '\0',
                "child executable is required for out-of-process mode")) {
      return result;
    }
    run_request.child_process = ztick::plugin::ChildProcessSpec{
        child_executable, {kChildModeArgument, kModuleArgument, module_path}};
    record = ztick::plugin::run_out_of_process(run_request);
  }

  result.runner_status = record.status;
  if (record.mode.has_value()) {
    result.execution_mode = *record.mode;
  }
  if (!expect(record.succeeded(), "engine workload execution failed")) {
    if (result.error.empty() && record.reason != nullptr) {
      result.error = record.reason;
    }
    return result;
  }
  result.ok = result.error.empty();
  return result;
}

} // namespace

auto run_harness(const char *module_path, HarnessIsolation isolation,
                 const char *child_executable) noexcept -> HarnessResult {
  try {
    return run_harness_impl(module_path, isolation, child_executable);
  } catch (...) {
    HarnessResult result;
    result.error = "harness threw an exception";
    return result;
  }
}

auto run_child_engine(const char *module_path) noexcept -> std::int32_t {
  try {
    if (module_path == nullptr || module_path[0] == '\0') {
      return ztick_status_invalid_argument;
    }
    const ztick::plugin::RuntimeLoadResult loaded =
        ztick::plugin::load_dynamic_adapter_runtime(module_path);
    if (!loaded.loaded()) {
      return ztick_status_internal_error;
    }
    const ztick_adapter_descriptor *descriptor = loaded.adapter.get();
    if (descriptor == nullptr || descriptor->info.identifier == nullptr ||
        std::strcmp(descriptor->info.identifier, kAdapterId) != 0 ||
        !has_all_callbacks(descriptor->ops)) {
      return ztick_status_invalid_argument;
    }
    const ztick::plugin::NegotiationResult negotiation =
        ztick::plugin::negotiate_adapter(*descriptor, kRequiredCapabilities);
    if (!negotiation.accepted()) {
      return negotiation.status;
    }
    WorkloadOutcome outcome;
    return execute_engine_workload(*descriptor, outcome);
  } catch (...) {
    return ztick_status_internal_error;
  }
}

auto format_summary(const HarnessResult &result) -> std::string {
  std::ostringstream out;
  out << "app_b ok=" << (result.ok ? 1 : 0) << " execution="
      << ztick::plugin::execution_mode_name(static_cast<std::int32_t>(result.execution_mode))
      << " inspection=" << result.inspection_status << " negotiation=" << result.negotiation_status
      << " trust=" << result.trust_status << " runner=" << result.runner_status
      << " checks=" << result.validation_check_count << " blocks=" << result.block_count
      << " frames=" << result.processed_frames << " whole_run_s=" << std::fixed
      << std::setprecision(6) << result.whole_run_seconds << " metrics=" << result.metrics.size();
  if (!result.error.empty()) {
    out << " error=" << result.error;
  }
  return out.str();
}

} // namespace ztick::example::app_b
