// SPDX-License-Identifier: MIT
/**
 * @file module.cpp
 * @brief Shared module for the out-of-tree dynamic integration (P3.9).
 *
 * The module exports only the read-only descriptor named by
 * @ref ZTICK_ADAPTER_EXPORT_SYMBOL. It deliberately does not link the SDK
 * runtime: identity is data, and all lifecycle work happens in the callbacks
 * invoked by an external host (SDK_SPECIFICATION §10).
 */

#include <ztick/sdk.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

/** @brief Opaque instance state owned by one module handle. */
struct ztick_adapter {
  /** @brief Configured mix of current and previous samples. */
  double mix{0.5};
  /** @brief Previous source sample carried across block boundaries. */
  float previous{0.0F};
  /** @brief Full-run input copied during preparation. */
  std::vector<float> input;
  /** @brief Independently computed float32 reference samples. */
  std::vector<float> reference;
  /** @brief Produced samples retained for post-timing validation. */
  std::vector<float> output;
  /** @brief Number of completed processing blocks. */
  std::uint64_t blocks{0U};
  /** @brief Whether preparation has supplied valid buffers. */
  bool prepared{false};
  /** @brief Whether finish has finalized this instance. */
  bool finished{false};
};

namespace {

/** @brief Frames in one processing block. */
constexpr std::uint32_t kFrames = 64U;
/** @brief Explicit sample rate of the declared format, in Hz. */
constexpr std::uint32_t kSampleRate = 48000U;
/** @brief Name of the floating-point mix parameter. */
constexpr const char kMixParameter[] = "mix";
/** @brief Name of the finite-output validation check. */
constexpr const char kFiniteCheck[] = "engine.finite";
/** @brief Name of the reference-comparison validation check. */
constexpr const char kReferenceCheck[] = "engine.matches_reference";

/** @brief Sole audio format declared by this module. */
const ztick_audio_format kFormat{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                 ZTICK_STRUCT_LAYOUT_VERSION,
                                 ztick_sample_format_float32,
                                 ztick_channel_layout_interleaved,
                                 kSampleRate,
                                 1U,
                                 kFrames};

/** @brief Typed parameters declared by this module. */
const ztick_parameter_descriptor kParameters[]{
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     kMixParameter, ztick_parameter_type_floating_point, nullptr, 0.0, 1.0, 0.5, nullptr, 0U,
     "Mix of the current sample and the previous sample."}};

/** @brief Machine-readable configurations this module refuses. */
const char *const kUnsupported[]{
    "float64_samples: this module implements float32 output only",
    "external_reference_files: references are generated inside the module"};

/** @brief Version of this integration. */
const ztick_version kAdapterVersion{static_cast<std::uint32_t>(sizeof(ztick_version)),
                                    ZTICK_STRUCT_LAYOUT_VERSION, 1U, 0U, 0U};

/** @brief SDK ABI version this module was compiled against. */
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
 * @brief Compute one engine sample from the written smoothing formula.
 * @param input Source sample for this frame.
 * @param previous Previous source sample.
 * @param mix Configured mix of current and previous samples.
 * @return Smoothed sample before float32 storage.
 */
auto engine_sample(float input, float previous, double mix) noexcept -> float {
  return static_cast<float>(mix * static_cast<double>(input) +
                            (1.0 - mix) * static_cast<double>(previous));
}

/**
 * @brief Create one module instance.
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
 * @brief Destroy one module instance.
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
 * @brief Apply the declared floating-point mix parameter.
 * @param adapter Adapter handle.
 * @param name Parameter name from @ref describe.
 * @param value Pointer to one double value.
 * @param value_size Size of @p value in bytes.
 * @return A `ztick_status` code.
 */
auto configure(ztick_adapter *adapter, const char *name, const void *value,
               std::size_t value_size) noexcept -> std::int32_t {
  if (adapter == nullptr || adapter->prepared || name == nullptr || value == nullptr ||
      std::strcmp(name, kMixParameter) != 0 || value_size != sizeof(double)) {
    return ztick_status_invalid_argument;
  }
  double mix = 0.0;
  std::memcpy(&mix, value, sizeof(mix));
  if (!std::isfinite(mix) || mix < 0.0 || mix > 1.0) {
    return ztick_status_unsupported;
  }
  adapter->mix = mix;
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
    float previous = 0.0F;
    for (std::size_t index = 0U; index < count; ++index) {
      adapter->reference[index] = engine_sample(adapter->input[index], previous, adapter->mix);
      previous = adapter->input[index];
    }
    adapter->output.assign(count, 0.0F);
    adapter->previous = 0.0F;
    adapter->blocks = 0U;
    adapter->prepared = true;
    return ztick_status_success;
  } catch (...) {
    adapter->prepared = false;
    return ztick_status_internal_error;
  }
}

/**
 * @brief Process one measured engine block with state carried across blocks.
 * @param adapter Prepared adapter handle.
 * @param input Constant input view for this block.
 * @param output Writable output view for this block.
 * @param timing Unused optional timing sample.
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
  const auto *source = static_cast<const float *>(input->planes[0]);
  auto *target = static_cast<float *>(output->planes[0]);
  for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
    target[frame] = engine_sample(source[frame], adapter->previous, adapter->mix);
    adapter->previous = source[frame];
    adapter->output[base + frame] = target[frame];
  }
  ++adapter->blocks;
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
    results[index].reference_source = "module_generated_reference";
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
  std::int32_t status =
      emit(sink, context, "external.engine.blocks", static_cast<double>(adapter->blocks), "blocks",
           ztick_metric_status_measured, "module_counter");
  if (status != ztick_status_success) {
    return status;
  }
  status = emit(sink, context, "external.engine.worker_seconds", 0.0, "s",
                ztick_metric_status_measured, "module_clock");
  if (status != ztick_status_success) {
    return status;
  }
  status = emit(sink, context, "external.engine.overhead_seconds", 0.0, "s",
                ztick_metric_status_estimated, "host_estimate_not_subtracted");
  if (status != ztick_status_success) {
    return status;
  }
  status = emit(sink, context, "external.engine.cache_misses", 0.0, "count",
                ztick_metric_status_unavailable, "platform_counter_unavailable");
  if (status != ztick_status_success) {
    return status;
  }
  return emit(sink, context, "external.engine.live_latency_ms", 0.0, "ms",
              ztick_metric_status_not_applicable, "offline_benchmark");
}

/** @brief Lifecycle table exposed through the read-only export descriptor. */
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

} // namespace

/** @brief Read-only descriptor exported for external host inspection and use. */
extern "C" const ztick_adapter_descriptor ztick_adapter_export{
    static_cast<std::uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<std::uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     "third.party.external.dynamic", "External dynamic SDK integration", kAdapterVersion,
     kSdkVersion,
     ZTICK_CAPABILITY_ENGINE | ZTICK_CAPABILITY_REFERENCE_DATA | ZTICK_CAPABILITY_CUSTOM_VALIDATION,
     "built out of tree from the installed SDK"},
    kOps};
