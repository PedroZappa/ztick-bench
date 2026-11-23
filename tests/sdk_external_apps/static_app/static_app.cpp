// SPDX-License-Identifier: MIT
/**
 * @file static_app.cpp
 * @brief Out-of-tree static integration used by ROADMAP P3.9.
 *
 * This single file is an independent third-party application: it declares its
 * own adapter, registers it through the installed C ABI, and hosts the complete
 * lifecycle itself. It uses only <ztick/sdk.h> and the C++ standard library, so
 * it can be built against an installed ztick-sdk package without touching the
 * ztick-bench source tree.
 */

#include <ztick/sdk.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

/** @brief Opaque instance state owned by this integration. */
struct ztick_adapter {
  /** @brief Configured output gain. */
  double gain{0.5};
  /** @brief Full-run interleaved input copied during preparation. */
  std::vector<float> input;
  /** @brief Independently computed float32 reference samples. */
  std::vector<float> reference;
  /** @brief Produced samples retained for post-timing validation. */
  std::vector<float> output;
  /** @brief Number of completed processing blocks. */
  std::uint64_t blocks{0U};
  /** @brief Aggregate measured DSP time in seconds. */
  double seconds{0.0};
  /** @brief Whether preparation has supplied valid buffers. */
  bool prepared{false};
  /** @brief Whether finish has finalized this instance. */
  bool finished{false};
};

namespace {

/** @brief Number of measured blocks in one run. */
constexpr std::uint32_t kBlocks = 8U;
/** @brief Frames in one processing block. */
constexpr std::uint32_t kFrames = 64U;
/** @brief Explicit sample rate of the declared format, in Hz. */
constexpr std::uint32_t kSampleRate = 48000U;
/** @brief Absolute and relative tolerance used by both validation checks. */
constexpr double kTolerance = 1.0e-5;

/** @brief Stable identifier of this integration. */
constexpr const char kIdentifier[] = "third.party.external.static";
/** @brief Human-readable name of this integration. */
constexpr const char kName[] = "External static SDK integration";
/** @brief Name of the floating-point configuration parameter. */
constexpr const char kGainParameter[] = "gain";
/** @brief Name of the finite-output validation check. */
constexpr const char kFiniteCheck[] = "signal.finite";
/** @brief Name of the reference-comparison validation check. */
constexpr const char kReferenceCheck[] = "signal.matches_reference";

/** @brief Collected metric name and provenance status. */
struct MetricRecord {
  /** @brief Stable machine-readable metric name. */
  const char *name;
  /** @brief Provenance label reported with the value. */
  ztick_metric_status status;
};

/** @brief Sink context that owns all collected metric records. */
struct MetricContext {
  /** @brief Metrics emitted by the adapter's report callback. */
  std::vector<MetricRecord> metrics;
};

/** @brief Sole audio format declared by this integration. */
const ztick_audio_format kFormat{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                 ZTICK_STRUCT_LAYOUT_VERSION,
                                 ztick_sample_format_float32,
                                 ztick_channel_layout_interleaved,
                                 kSampleRate,
                                 1U,
                                 kFrames};

/** @brief Typed parameters declared by this integration. */
const ztick_parameter_descriptor kParameters[]{
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     kGainParameter, ztick_parameter_type_floating_point, nullptr, 0.0, 2.0, 0.5, nullptr, 0U,
     "Linear output gain applied to every sample."}};

/** @brief Machine-readable configurations this integration refuses. */
const char *const kUnsupported[]{
    "float64_samples: this integration implements float32 output only",
    "variable_block_size: only the declared 64-frame block is supported"};

/** @brief Version of this integration. */
const ztick_version kAdapterVersion{static_cast<std::uint32_t>(sizeof(ztick_version)),
                                    ZTICK_STRUCT_LAYOUT_VERSION, 1U, 0U, 0U};

/** @brief SDK ABI version this integration was compiled against. */
const ztick_version kSdkVersion{static_cast<std::uint32_t>(sizeof(ztick_version)),
                                ZTICK_STRUCT_LAYOUT_VERSION, ZTICK_SDK_VERSION_MAJOR,
                                ZTICK_SDK_VERSION_MINOR, ZTICK_SDK_VERSION_PATCH};

/**
 * @brief Validate one public C struct header before reading its fields.
 * @param size Reported struct size.
 * @param version Reported layout version.
 * @param expected Minimum expected size in bytes.
 * @return True when the struct can be read safely.
 */
auto header_valid(std::uint32_t size, std::uint32_t version,
                  std::size_t expected) noexcept -> bool {
  return size >= expected && version == ZTICK_STRUCT_LAYOUT_VERSION;
}

/**
 * @brief Compute one output sample from the written integration formula.
 * @param input Source sample.
 * @param gain Configured linear gain.
 * @return Gain-scaled sample before float32 storage.
 */
auto output_sample(float input, double gain) noexcept -> float {
  return static_cast<float>(static_cast<double>(input) * gain);
}

/**
 * @brief Create one adapter instance.
 * @param out_adapter Receives the new opaque handle.
 * @return A `ztick_status` code.
 */
auto create(ztick_adapter **out_adapter) noexcept -> std::int32_t {
  if (out_adapter == nullptr) {
    return ztick_status_invalid_argument;
  }
  try {
    *out_adapter = new ztick_adapter{};
    return ztick_status_success;
  } catch (...) {
    *out_adapter = nullptr;
    return ztick_status_internal_error;
  }
}

/**
 * @brief Destroy one adapter instance.
 * @param adapter Handle to release; may be NULL.
 * @return A `ztick_status` code.
 */
auto destroy(ztick_adapter *adapter) noexcept -> std::int32_t {
  delete adapter;
  return ztick_status_success;
}

/**
 * @brief Declare formats, typed parameters and unsupported configurations.
 * @param adapter Constant adapter handle.
 * @param formats Receives the non-owning format array.
 * @param format_count Receives the format count.
 * @param parameters Receives the non-owning parameter array.
 * @param parameter_count Receives the parameter count.
 * @param unsupported Receives the non-owning unsupported-reason array.
 * @param unsupported_count Receives the unsupported-reason count.
 * @return A `ztick_status` code.
 */
auto describe(const ztick_adapter *adapter, const ztick_audio_format **formats,
              std::uint32_t *format_count, const ztick_parameter_descriptor **parameters,
              std::uint32_t *parameter_count, const char *const **unsupported,
              std::uint32_t *unsupported_count) noexcept -> std::int32_t {
  if (adapter == nullptr || formats == nullptr || format_count == nullptr ||
      parameters == nullptr || parameter_count == nullptr || unsupported == nullptr ||
      unsupported_count == nullptr) {
    return ztick_status_invalid_argument;
  }
  *formats = &kFormat;
  *format_count = 1U;
  *parameters = kParameters;
  *parameter_count = static_cast<std::uint32_t>(sizeof(kParameters) / sizeof(kParameters[0]));
  *unsupported = kUnsupported;
  *unsupported_count = static_cast<std::uint32_t>(sizeof(kUnsupported) / sizeof(kUnsupported[0]));
  return ztick_status_success;
}

/**
 * @brief Apply the declared floating-point gain parameter.
 * @param adapter Adapter handle.
 * @param name Parameter name from @ref describe.
 * @param value Pointer to one double value.
 * @param value_size Size of @p value in bytes.
 * @return A `ztick_status` code.
 */
auto configure(ztick_adapter *adapter, const char *name, const void *value,
               std::size_t value_size) noexcept -> std::int32_t {
  if (adapter == nullptr || adapter->prepared || name == nullptr || value == nullptr ||
      std::strcmp(name, kGainParameter) != 0 || value_size != sizeof(double)) {
    return ztick_status_invalid_argument;
  }
  double gain = 0.0;
  std::memcpy(&gain, value, sizeof(gain));
  if (!std::isfinite(gain) || gain < kParameters[0].minimum || gain > kParameters[0].maximum) {
    return ztick_status_unsupported;
  }
  adapter->gain = gain;
  return ztick_status_success;
}

/**
 * @brief Copy input data and build the reference outside the timed interval.
 * @param adapter Adapter handle.
 * @param input Full-run input view supplied by the host.
 * @param reference Unused external reference view.
 * @return A `ztick_status` code.
 */
auto prepare(ztick_adapter *adapter, const ztick_buffer_view *input,
             const ztick_buffer_view *reference) noexcept -> std::int32_t {
  if (adapter == nullptr || adapter->finished || input == nullptr || reference != nullptr ||
      !header_valid(input->size, input->version, sizeof(*input)) ||
      input->format.format != ztick_sample_format_float32 || input->plane_count != 1U ||
      input->planes == nullptr || input->planes[0] == nullptr) {
    return ztick_status_invalid_argument;
  }
  try {
    const auto *samples = static_cast<const float *>(input->planes[0]);
    const std::size_t count = static_cast<std::size_t>(input->frames) * input->format.channels;
    adapter->input.assign(samples, samples + count);
    adapter->reference.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
      adapter->reference[index] = output_sample(adapter->input[index], adapter->gain);
    }
    adapter->output.assign(count, 0.0F);
    adapter->blocks = 0U;
    adapter->seconds = 0.0;
    adapter->prepared = true;
    return ztick_status_success;
  } catch (...) {
    adapter->prepared = false;
    return ztick_status_internal_error;
  }
}

/**
 * @brief Process one measured block and emit one timing sample.
 * @param adapter Prepared adapter handle.
 * @param input Constant input view for this block.
 * @param output Writable output view for this block.
 * @param timing Optional per-block timing sample.
 * @return A `ztick_status` code.
 */
auto process(ztick_adapter *adapter, const ztick_buffer_view *input, ztick_buffer_view *output,
             ztick_timing_sample *timing) noexcept -> std::int32_t {
  if (adapter == nullptr || !adapter->prepared || adapter->finished || input == nullptr ||
      output == nullptr || !header_valid(input->size, input->version, sizeof(*input)) ||
      !header_valid(output->size, output->version, sizeof(*output)) || input->frames != kFrames ||
      output->frames != kFrames || input->plane_count != 1U || output->plane_count != 1U ||
      input->planes == nullptr || output->planes == nullptr) {
    return ztick_status_invalid_argument;
  }
  if (timing != nullptr && !header_valid(timing->size, timing->version, sizeof(*timing))) {
    return ztick_status_invalid_argument;
  }
  const std::size_t base = static_cast<std::size_t>(adapter->blocks) * kFrames;
  if (base + kFrames > adapter->output.size()) {
    return ztick_status_invalid_argument;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto *source = static_cast<const float *>(input->planes[0]);
  auto *target = static_cast<float *>(output->planes[0]);
  for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
    target[frame] = output_sample(source[frame], adapter->gain);
    adapter->output[base + frame] = target[frame];
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  adapter->seconds += elapsed;
  ++adapter->blocks;
  if (timing != nullptr) {
    timing->block_index = adapter->blocks - 1U;
    timing->duration_seconds = elapsed;
  }
  return ztick_status_success;
}

/**
 * @brief Finalize counters after the measured interval.
 * @param adapter Prepared adapter handle.
 * @return A `ztick_status` code.
 */
auto finish(ztick_adapter *adapter) noexcept -> std::int32_t {
  if (adapter == nullptr || !adapter->prepared || adapter->finished) {
    return ztick_status_invalid_argument;
  }
  adapter->finished = true;
  return ztick_status_success;
}

/**
 * @brief Run both post-timing checks against retained output samples.
 * @param adapter Finished adapter handle.
 * @param requested Requested checks with tolerances.
 * @param requested_count Number of requested checks.
 * @param results Output array for outcomes.
 * @param result_count In/out capacity and written count.
 * @return Success when both checks pass, otherwise an explicit failure status.
 */
auto validate(const ztick_adapter *adapter, const ztick_validation_result *requested,
              std::uint32_t requested_count, ztick_validation_result *results,
              std::uint32_t *result_count) noexcept -> std::int32_t {
  if (adapter == nullptr || !adapter->finished || requested == nullptr || results == nullptr ||
      result_count == nullptr || *result_count < requested_count) {
    return ztick_status_invalid_argument;
  }
  for (std::uint32_t index = 0U; index < requested_count; ++index) {
    if (!header_valid(requested[index].size, requested[index].version, sizeof(requested[index])) ||
        requested[index].check_name == nullptr) {
      return ztick_status_invalid_argument;
    }
    bool passed = false;
    if (std::strcmp(requested[index].check_name, kFiniteCheck) == 0) {
      passed = true;
      for (const float value : adapter->output) {
        passed = passed && std::isfinite(value);
      }
    } else if (std::strcmp(requested[index].check_name, kReferenceCheck) == 0) {
      passed = adapter->output.size() == adapter->reference.size();
      for (std::size_t sample = 0U; passed && sample < adapter->output.size(); ++sample) {
        const double difference = std::fabs(static_cast<double>(adapter->output[sample]) -
                                            static_cast<double>(adapter->reference[sample]));
        passed = difference <= requested[index].absolute_tolerance +
                                   requested[index].relative_tolerance *
                                       std::fabs(static_cast<double>(adapter->reference[sample]));
      }
    } else {
      return ztick_status_unsupported;
    }
    results[index] = ztick_validation_result{};
    results[index].size = static_cast<std::uint32_t>(sizeof(results[index]));
    results[index].version = ZTICK_STRUCT_LAYOUT_VERSION;
    results[index].check_name = requested[index].check_name;
    results[index].outcome = passed ? ztick_status_success : ztick_status_validation_failure;
    results[index].absolute_tolerance = requested[index].absolute_tolerance;
    results[index].relative_tolerance = requested[index].relative_tolerance;
    results[index].reference_source = "adapter_generated_reference";
    results[index].details = passed ? "check passed" : "check failed";
  }
  *result_count = requested_count;
  return ztick_status_success;
}

/**
 * @brief Emit one metric through the host sink.
 * @param sink Sink callback.
 * @param context Opaque host context.
 * @param name Stable metric name.
 * @param value Numeric value.
 * @param unit Unit string or NULL.
 * @param status Provenance label.
 * @param source Collection method.
 * @return The sink status, or `invalid_argument` for a bad sink.
 */
auto emit(ztick_metric_sink_fn sink, void *context, const char *name, double value,
          const char *unit, ztick_metric_status status,
          const char *source) noexcept -> std::int32_t {
  if (sink == nullptr || name == nullptr) {
    return ztick_status_invalid_argument;
  }
  return sink(context, name, value, unit, status, source);
}

/**
 * @brief Report measured, estimated, unavailable and not-applicable metrics.
 * @param adapter Finished adapter handle.
 * @param sink Host sink callback.
 * @param context Opaque host context.
 * @return A `ztick_status` code.
 */
auto report(const ztick_adapter *adapter, ztick_metric_sink_fn sink,
            void *context) noexcept -> std::int32_t {
  if (adapter == nullptr || !adapter->finished || sink == nullptr) {
    return ztick_status_invalid_argument;
  }
  std::int32_t status = emit(sink, context, "external.blocks", static_cast<double>(adapter->blocks),
                             "blocks", ztick_metric_status_measured, "adapter_counter");
  if (status != ztick_status_success) {
    return status;
  }
  status = emit(sink, context, "external.processing_seconds", adapter->seconds, "s",
                ztick_metric_status_measured, "adapter_clock");
  if (status != ztick_status_success) {
    return status;
  }
  status = emit(sink, context, "external.overhead_estimate_seconds", 0.0, "s",
                ztick_metric_status_estimated, "host_estimate_not_subtracted");
  if (status != ztick_status_success) {
    return status;
  }
  status = emit(sink, context, "external.cpu_frequency_hz", 0.0, "Hz",
                ztick_metric_status_unavailable, "platform_counter_unavailable");
  if (status != ztick_status_success) {
    return status;
  }
  return emit(sink, context, "external.live_latency_ms", 0.0, "ms",
              ztick_metric_status_not_applicable, "offline_benchmark");
}

/** @brief Lifecycle table exposed through the static descriptor. */
const ztick_adapter_ops kOps{static_cast<std::uint32_t>(sizeof(ztick_adapter_ops)),
                             ZTICK_STRUCT_LAYOUT_VERSION,
                             &create,
                             &destroy,
                             &describe,
                             &configure,
                             &prepare,
                             &process,
                             &finish,
                             &validate,
                             &report};

/** @brief Descriptor registered by this integration during host initialization. */
const ztick_adapter_descriptor kDescriptor{
    static_cast<std::uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<std::uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     kIdentifier, kName, kAdapterVersion, kSdkVersion,
     ZTICK_CAPABILITY_PROCESSOR | ZTICK_CAPABILITY_CHAIN | ZTICK_CAPABILITY_PER_BLOCK_TIMING |
         ZTICK_CAPABILITY_REFERENCE_DATA | ZTICK_CAPABILITY_CUSTOM_VALIDATION,
     "built out of tree from the installed SDK"},
    kOps};

/**
 * @brief Build one buffer view from a caller-owned plane array.
 * @param planes Caller-owned array of plane pointers.
 * @param frames Frame count in the view.
 * @param format Explicit format of the sample data.
 * @return A fully initialized buffer view.
 */
auto make_view(void *const *planes, std::uint32_t frames,
               const ztick_audio_format &format) -> ztick_buffer_view {
  ztick_buffer_view view{};
  view.size = static_cast<std::uint32_t>(sizeof(view));
  view.version = ZTICK_STRUCT_LAYOUT_VERSION;
  view.planes = planes;
  view.strides = nullptr;
  view.plane_count = 1U;
  view.frames = frames;
  view.format = format;
  return view;
}

/**
 * @brief Report one failed expectation and return its result.
 * @param condition Condition that must hold.
 * @param message Explanation printed when the condition fails.
 * @return True when the condition held.
 */
auto expect(bool condition, const char *message) noexcept -> bool {
  if (!condition) {
    std::cerr << "external static app: " << message << '\n';
  }
  return condition;
}

/**
 * @brief Collect one reported metric in the host sink.
 * @param context Opaque @ref MetricContext pointer.
 * @param name Stable metric name.
 * @param value Numeric metric value.
 * @param unit Unit string, ignored here.
 * @param status Provenance label.
 * @param source Collection method, ignored here.
 * @return `ztick_status_success`.
 */
auto collect_metric(void *context, const char *name, double, const char *,
                    ztick_metric_status status, const char *) noexcept -> std::int32_t {
  auto *metrics = static_cast<MetricContext *>(context);
  metrics->metrics.push_back(MetricRecord{name, status});
  return ztick_status_success;
}

/**
 * @brief Drive the complete lifecycle through the registered descriptor.
 * @return True when every lifecycle step and host assertion passed.
 */
auto run_lifecycle() noexcept -> bool {
  const auto *ops = &kDescriptor.ops;
  ztick_adapter *adapter = nullptr;
  if (!expect(ops->create(&adapter) == ztick_status_success && adapter != nullptr,
              "create failed")) {
    return false;
  }

  const ztick_audio_format *formats = nullptr;
  std::uint32_t format_count = 0U;
  const ztick_parameter_descriptor *parameters = nullptr;
  std::uint32_t parameter_count = 0U;
  const char *const *unsupported = nullptr;
  std::uint32_t unsupported_count = 0U;
  bool ok = expect(ops->describe(adapter, &formats, &format_count, &parameters, &parameter_count,
                                 &unsupported, &unsupported_count) == ztick_status_success,
                   "describe failed") &&
            expect(format_count == 1U && formats != nullptr &&
                       formats[0].format == ztick_sample_format_float32,
                   "float32 format was not declared") &&
            expect(parameter_count == 1U && unsupported_count > 0U,
                   "parameter or unsupported declarations are missing");

  const double gain = 0.75;
  if (ok) {
    ok =
        expect(ops->configure(adapter, kGainParameter, &gain, sizeof(gain)) == ztick_status_success,
               "configure failed");
  }

  std::vector<float> input(static_cast<std::size_t>(kBlocks) * kFrames);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    input[index] = static_cast<float>(
        0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * static_cast<double>(index) /
                       static_cast<double>(kSampleRate)));
  }
  void *full_planes[1]{input.data()};
  const auto full_view = make_view(full_planes, static_cast<std::uint32_t>(input.size()), kFormat);
  if (ok) {
    ok = expect(ops->prepare(adapter, &full_view, nullptr) == ztick_status_success,
                "prepare failed");
  }

  for (std::uint32_t block = 0U; ok && block < kBlocks; ++block) {
    float block_input[kFrames]{};
    float block_output[kFrames]{};
    std::memcpy(block_input, input.data() + block * kFrames, sizeof(block_input));
    void *input_planes[1]{block_input};
    void *output_planes[1]{block_output};
    const auto input_view = make_view(input_planes, kFrames, kFormat);
    auto output_view = make_view(output_planes, kFrames, kFormat);
    ztick_timing_sample timing{};
    timing.size = static_cast<std::uint32_t>(sizeof(timing));
    timing.version = ZTICK_STRUCT_LAYOUT_VERSION;
    ok = expect(ops->process(adapter, &input_view, &output_view, &timing) == ztick_status_success &&
                    timing.block_index == block && timing.duration_seconds >= 0.0,
                "process failed or returned an invalid timing sample");
  }

  if (ok) {
    ok = expect(ops->finish(adapter) == ztick_status_success, "finish failed");
  }

  ztick_validation_result requested[2]{};
  for (auto &check : requested) {
    check.size = static_cast<std::uint32_t>(sizeof(check));
    check.version = ZTICK_STRUCT_LAYOUT_VERSION;
    check.absolute_tolerance = kTolerance;
    check.relative_tolerance = kTolerance;
    check.reference_source = "adapter_generated_reference";
  }
  requested[0].check_name = kFiniteCheck;
  requested[1].check_name = kReferenceCheck;
  ztick_validation_result results[2]{};
  std::uint32_t result_count = 2U;
  if (ok) {
    ok = expect(ops->validate(adapter, requested, 2U, results, &result_count) ==
                        ztick_status_success &&
                    result_count == 2U && results[0].outcome == ztick_status_success &&
                    results[1].outcome == ztick_status_success,
                "validation failed");
  }

  MetricContext context;
  if (ok) {
    ok = expect(ops->report(adapter, &collect_metric, &context) == ztick_status_success,
                "report failed");
  }
  if (ok) {
    bool measured = false;
    bool estimated = false;
    bool unavailable = false;
    bool not_applicable = false;
    for (const auto &metric : context.metrics) {
      measured = measured || metric.status == ztick_metric_status_measured;
      estimated = estimated || metric.status == ztick_metric_status_estimated;
      unavailable = unavailable || metric.status == ztick_metric_status_unavailable;
      not_applicable = not_applicable || metric.status == ztick_metric_status_not_applicable;
    }
    ok = expect(measured && estimated && unavailable && not_applicable,
                "report did not cover all four metric statuses");
  }

  const std::int32_t destroy_status = ops->destroy(adapter);
  return expect(destroy_status == ztick_status_success, "destroy failed") && ok;
}

} // namespace

/**
 * @brief Entry point for the out-of-tree static integration.
 * @return Zero when the public SDK registration path and full lifecycle pass.
 */
auto main() -> int {
  ztick_version host_version{};
  host_version.size = static_cast<std::uint32_t>(sizeof(host_version));
  host_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  if (!expect(ztick_sdk_get_version(&host_version) == ztick_status_success,
              "version query failed")) {
    return 1;
  }
  if (!expect(ztick_sdk_is_compatible(&kDescriptor.info.sdk_version) == ztick_status_success,
              "SDK version check failed")) {
    return 1;
  }
  if (!expect(ztick_register_adapter(&kDescriptor) == ztick_status_success,
              "static registration failed")) {
    return 1;
  }
  if (!run_lifecycle()) {
    return 1;
  }
  std::cout << "external static app: " << kIdentifier << " completed " << kBlocks << " blocks\n";
  return 0;
}
