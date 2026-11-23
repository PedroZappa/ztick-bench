// SPDX-License-Identifier: MIT
/**
 * @file dynamic_adapter.cpp
 * @brief Dynamic App B adapter implementing the engine SDK lifecycle.
 *
 * The shared library exports @ref ztick_adapter_export as read-only data. The host
 * may inspect the descriptor without executing plugin code; execution begins only
 * when it calls one of the lifecycle callbacks (SDK_SPECIFICATION §3, §10). This
 * adapter deliberately does not link the SDK runtime: the host owns the single
 * registry. Its engine workload divides every block across internal worker threads
 * and reports instrumented worker time without subtracting overhead from raw
 * measurements (BR §7.3).
 */

#include <app_b_contract.hpp>

#include <ztick/sdk.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

/** @brief Version of the App B integration itself. */
const ztick_version kAdapterVersion{
    static_cast<std::uint32_t>(sizeof(ztick_version)), ZTICK_STRUCT_LAYOUT_VERSION,
    ztick::example::app_b::kAdapterVersionMajor, ztick::example::app_b::kAdapterVersionMinor,
    ztick::example::app_b::kAdapterVersionPatch};

/** @brief SDK ABI version this adapter was compiled against. */
const ztick_version kSdkVersion{static_cast<std::uint32_t>(sizeof(ztick_version)),
                                ZTICK_STRUCT_LAYOUT_VERSION, ZTICK_SDK_VERSION_MAJOR,
                                ZTICK_SDK_VERSION_MINOR, ZTICK_SDK_VERSION_PATCH};

/** @brief Supported explicit audio formats; float32 is mandatory. */
const ztick_audio_format kFormats[] = {
    {static_cast<std::uint32_t>(sizeof(ztick_audio_format)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick_sample_format_float32, ztick_channel_layout_interleaved,
     ztick::example::app_b::kSampleRateHz, ztick::example::app_b::kChannels,
     ztick::example::app_b::kFramesPerBlock},
    {static_cast<std::uint32_t>(sizeof(ztick_audio_format)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick_sample_format_float32, ztick_channel_layout_planar, ztick::example::app_b::kSampleRateHz,
     ztick::example::app_b::kChannels, ztick::example::app_b::kFramesPerBlock},
};

/** @brief Number of declared audio formats. */
constexpr std::uint32_t kFormatCount =
    static_cast<std::uint32_t>(sizeof(kFormats) / sizeof(kFormats[0]));

/** @brief Allowed values of the quality enumeration parameter. */
const char *const kQualityValues[] = {ztick::example::app_b::kQualityDraft,
                                      ztick::example::app_b::kQualityFinal};

/** @brief Number of allowed quality enumeration values. */
constexpr std::uint32_t kQualityValueCount =
    static_cast<std::uint32_t>(sizeof(kQualityValues) / sizeof(kQualityValues[0]));

/** @brief Declared typed parameters with ranges, defaults and descriptions. */
const ztick_parameter_descriptor kParameters[] = {
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterVoices, ztick_parameter_type_integer, "voices", 1.0, 8.0,
     static_cast<double>(ztick::example::app_b::kVoiceCount), nullptr, 0U,
     "Number of synthesized voices in the engine."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterStages, ztick_parameter_type_integer, "stages", 1.0, 4.0,
     static_cast<double>(ztick::example::app_b::kStageCount), nullptr, 0U,
     "Number of shaping stages applied after voice mixing."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterThreads, ztick_parameter_type_integer, "threads", 1.0, 4.0,
     static_cast<double>(ztick::example::app_b::kThreadCount), nullptr, 0U,
     "Number of adapter-owned worker threads used per block."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterMix, ztick_parameter_type_floating_point, "ratio", 0.0, 1.0,
     ztick::example::app_b::kMix, nullptr, 0U,
     "Mix of synthesized signal added at each shaping stage."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterDither, ztick_parameter_type_boolean, nullptr, 0.0, 1.0, 0.0,
     nullptr, 0U, "Enable deterministic low-level dither."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterQuality, ztick_parameter_type_enumeration, nullptr, 0.0, 0.0,
     static_cast<double>(ztick::example::app_b::kQualityFinalIndex), kQualityValues,
     kQualityValueCount, "Selects the linear or saturating shaping curve."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kParameterProfile, ztick_parameter_type_string, nullptr, 0.0, 0.0, 0.0,
     nullptr, 0U, "Short engine profile label retained for diagnostics."},
};

/** @brief Number of declared typed parameters. */
constexpr std::uint32_t kParameterCount =
    static_cast<std::uint32_t>(sizeof(kParameters) / sizeof(kParameters[0]));

/** @brief Machine-readable unsupported configurations declared by the adapter. */
const char *const kUnsupported[] = {
    "float64_samples: only float32 engine buffers are implemented",
    "variable_block_size: only the declared 256-frame block is supported",
    "processor_workload: this adapter implements the integrated engine workload",
    "external_reference_files: references are generated inside the adapter",
    "more_than_four_threads: at most four internal workers are supported",
};

/** @brief Number of declared unsupported configurations. */
constexpr std::uint32_t kUnsupportedCount =
    static_cast<std::uint32_t>(sizeof(kUnsupported) / sizeof(kUnsupported[0]));

/** @brief Maximum accepted length of the profile string, including its terminator. */
constexpr std::size_t kMaxProfileLength = 64U;

/** @brief Maximum number of internal worker threads accepted by configuration. */
constexpr std::int64_t kMaxWorkerThreads = 4;

/** @brief Static detail attached to a successful finite-output check. */
constexpr const char kDetailFinite[] = "all produced samples are finite";
/** @brief Static detail attached to a successful range check. */
constexpr const char kDetailRange[] = "all produced samples are within the declared range";
/** @brief Static detail attached to a successful reference check. */
constexpr const char kDetailReference[] = "output matches adapter-generated reference data";
/** @brief Static detail attached to a successful expected-change check. */
constexpr const char kDetailChange[] = "alternate engine parameters produce different output";
/** @brief Static detail attached to a successful thread-balance check. */
constexpr const char kDetailThreads[] = "every configured worker contributed to every block";
/** @brief Static detail attached to a successful custom engine check. */
constexpr const char kDetailCustom[] = "engine peak and block layout are valid";
/** @brief Static detail attached to a failed validation check. */
constexpr const char kDetailFailure[] = "validation condition was not satisfied";

/** @brief Static source label for adapter-generated reference data. */
constexpr const char kSourceAdapterReference[] = "adapter_generated_reference";
/** @brief Static source label for per-block adapter instrumentation. */
constexpr const char kSourceInstrumentation[] = "adapter_worker_clock";
/** @brief Static source label for values this integration cannot measure. */
constexpr const char kSourceUnavailable[] = "platform_counter_unavailable";
/** @brief Static source label for values undefined for this packaging. */
constexpr const char kSourceNotApplicable[] = "adapter_generated_reference_only";
/** @brief Static source label for the deliberately separate overhead estimate. */
constexpr const char kSourceOverheadEstimate[] = "host_runner_estimate_not_subtracted";

} // namespace

/**
 * @brief Opaque instance state owned by one App B adapter handle.
 * @note Each instance owns its configuration, buffers and counters. Worker threads
 *       are created per block and joined before `process` returns, so no thread or
 *       host buffer pointer survives a callback (SDK_SPECIFICATION §3, §5).
 */
struct ztick_adapter {
  /** @brief Number of synthesized voices. */
  std::int64_t voices{static_cast<std::int64_t>(ztick::example::app_b::kVoiceCount)};
  /** @brief Number of shaping stages. */
  std::int64_t stages{static_cast<std::int64_t>(ztick::example::app_b::kStageCount)};
  /** @brief Number of adapter-owned workers per block. */
  std::int64_t threads{static_cast<std::int64_t>(ztick::example::app_b::kThreadCount)};
  /** @brief Mix of synthesized signal added at each stage. */
  double mix{ztick::example::app_b::kMix};
  /** @brief Whether deterministic dither is enabled. */
  bool dither{ztick::example::app_b::kDither};
  /** @brief Whether the saturating final shaping curve is selected. */
  bool final_quality{true};
  /** @brief Instance profile label. */
  std::string profile{ztick::example::app_b::kProfile};
  /** @brief Sample rate of the prepared workload, in Hz. */
  std::uint32_t sample_rate{0U};
  /** @brief Channel count of the prepared workload. */
  std::uint32_t channels{0U};
  /** @brief Total frame count of the prepared full-run workload. */
  std::uint32_t frames{0U};
  /** @brief Frame count of one declared processing block. */
  std::uint32_t block_frames{0U};
  /** @brief Full-run interleaved input samples copied during preparation. */
  std::vector<float> input;
  /** @brief Adapter-generated interleaved reference samples. */
  std::vector<float> reference;
  /** @brief Full-run interleaved produced samples used for validation. */
  std::vector<float> output;
  /** @brief Duration of every completed worker invocation, in seconds. */
  std::vector<double> worker_durations;
  /** @brief Number of processed blocks. */
  std::uint64_t blocks{0U};
  /** @brief Number of processed frames. */
  std::uint64_t processed_frames{0U};
  /** @brief Number of worker invocations across the whole run. */
  std::uint64_t worker_calls{0U};
  /** @brief Aggregate adapter-measured DSP time in seconds. */
  double processing_seconds{0.0};
  /** @brief Largest absolute produced sample. */
  double output_peak{0.0};
  /** @brief Whether `prepare` has supplied valid buffers. */
  bool prepared{false};
  /** @brief Whether `finish` has finalized this instance. */
  bool finished{false};
};

namespace {

/** @brief Monotonic clock used by adapter instrumentation. */
using Clock = std::chrono::steady_clock;

/** @brief Constant pi used by the written engine formula. */
constexpr double kPi = 3.14159265358979323846;

/**
 * @brief Validate the header of one SDK C struct.
 * @param size Reported struct size.
 * @param version Reported layout version.
 * @param expected Minimum expected size in bytes.
 * @return True when the struct can be safely read.
 */
auto header_valid(std::uint32_t size, std::uint32_t version,
                  std::size_t expected) noexcept -> bool {
  return size >= expected && version == ZTICK_STRUCT_LAYOUT_VERSION;
}

/**
 * @brief Return the byte width of one declared sample format.
 * @param format Format to inspect.
 * @return Four for float32 or zero for an undeclared format.
 */
auto sample_size(const ztick_audio_format &format) noexcept -> std::size_t {
  if (!header_valid(format.size, format.version, sizeof(format)) ||
      format.format != ztick_sample_format_float32) {
    return 0U;
  }
  return sizeof(float);
}

/**
 * @brief Compare the workload-relevant fields of two audio formats.
 * @param left First format.
 * @param right Second format.
 * @return True when sample format, layout, rate, channel and frame counts match.
 */
auto same_format(const ztick_audio_format &left, const ztick_audio_format &right) noexcept -> bool {
  return left.format == right.format && left.layout == right.layout &&
         left.sample_rate == right.sample_rate && left.channels == right.channels &&
         left.frames == right.frames;
}

/**
 * @brief Report whether a format's type, rate and dimensions are supported.
 * @param format Format to check; frame count is not considered.
 * @return True when sample format, layout, sample rate and channel count match a declaration.
 */
auto format_supported(const ztick_audio_format &format) noexcept -> bool {
  return std::any_of(std::begin(kFormats), std::end(kFormats),
                     [&format](const ztick_audio_format &candidate) noexcept {
                       return format.format == candidate.format &&
                              format.layout == candidate.layout &&
                              format.sample_rate == candidate.sample_rate &&
                              format.channels == candidate.channels;
                     });
}

/**
 * @brief Report whether a format matches one declared block format exactly.
 * @param format Format to check.
 * @return True when the format is one of @ref kFormats.
 */
auto format_declared(const ztick_audio_format &format) noexcept -> bool {
  return std::any_of(std::begin(kFormats), std::end(kFormats),
                     [&format](const ztick_audio_format &candidate) noexcept {
                       return same_format(format, candidate);
                     });
}

/**
 * @brief Validate one audio buffer view before any sample is accessed.
 * @param view View to validate.
 * @return True when planes, strides and dimensions are internally consistent.
 */
auto view_valid(const ztick_buffer_view &view) noexcept -> bool {
  if (sample_size(view.format) == 0U || !header_valid(view.size, view.version, sizeof(view)) ||
      view.planes == nullptr || view.frames == 0U || view.format.channels == 0U ||
      view.strides != nullptr) {
    return false;
  }
  if (view.format.layout == ztick_channel_layout_interleaved) {
    return view.plane_count == 1U && view.planes[0] != nullptr;
  }
  if (view.format.layout == ztick_channel_layout_planar) {
    if (view.plane_count != view.format.channels) {
      return false;
    }
    for (std::uint32_t channel = 0U; channel < view.plane_count; ++channel) {
      if (view.planes[channel] == nullptr) {
        return false;
      }
    }
    return true;
  }
  return false;
}

/**
 * @brief Copy one buffer view into interleaved owning storage.
 * @param view View to read; must already satisfy @ref view_valid.
 * @param samples Receives `view.frames * view.format.channels` samples.
 * @return True when the copy completed.
 */
auto read_view(const ztick_buffer_view &view, std::vector<float> &samples) noexcept -> bool {
  const std::uint32_t channels = view.format.channels;
  const std::size_t count = static_cast<std::size_t>(view.frames) * channels;
  if (samples.size() != count) {
    return false;
  }
  if (view.format.layout == ztick_channel_layout_interleaved) {
    const auto *source = static_cast<const float *>(view.planes[0]);
    std::copy_n(source, count, samples.begin());
    return true;
  }
  for (std::uint32_t frame = 0U; frame < view.frames; ++frame) {
    for (std::uint32_t channel = 0U; channel < channels; ++channel) {
      const auto *source = static_cast<const float *>(view.planes[channel]);
      samples[static_cast<std::size_t>(frame) * channels + channel] = source[frame];
    }
  }
  return true;
}

/**
 * @brief Copy interleaved samples into one writable buffer view.
 * @param view View to write; must already satisfy @ref view_valid.
 * @param samples Interleaved samples to copy.
 * @return True when the copy completed.
 */
auto write_view(const ztick_buffer_view &view, const std::vector<float> &samples) noexcept -> bool {
  const std::uint32_t channels = view.format.channels;
  const std::size_t count = static_cast<std::size_t>(view.frames) * channels;
  if (samples.size() != count) {
    return false;
  }
  if (view.format.layout == ztick_channel_layout_interleaved) {
    auto *target = static_cast<float *>(view.planes[0]);
    std::copy(samples.begin(), samples.end(), target);
    return true;
  }
  for (std::uint32_t frame = 0U; frame < view.frames; ++frame) {
    for (std::uint32_t channel = 0U; channel < channels; ++channel) {
      auto *target = static_cast<float *>(view.planes[channel]);
      target[frame] = samples[static_cast<std::size_t>(frame) * channels + channel];
    }
  }
  return true;
}

/**
 * @brief Compute one engine sample from the written App B contract.
 * @param input Source sample for this frame and channel.
 * @param frame Global frame index in the whole run.
 * @param channel Channel index.
 * @param sample_index Global interleaved sample index.
 * @param voices Number of synthesized voices.
 * @param stages Number of shaping stages.
 * @param mix Mix of synthesized signal added at each stage.
 * @param final_quality Whether the saturating shaping curve is selected.
 * @param dither Whether deterministic dither is enabled.
 * @return Produced double-precision sample before float32 storage.
 * @note This stateless formula is shared by the reference path and the parallel
 *       process path. The harness computes it independently for validation.
 */
auto engine_sample(double input, std::uint64_t frame, std::uint32_t channel,
                   std::uint64_t sample_index, std::int64_t voices, std::int64_t stages, double mix,
                   bool final_quality, bool dither) noexcept -> double {
  double chorus = 0.0;
  for (std::int64_t voice = 0; voice < voices; ++voice) {
    const double frequency =
        ztick::example::app_b::kBaseFrequencyHz * static_cast<double>(voice + 1);
    const double phase = kPi * 2.0 * frequency * static_cast<double>(frame) /
                             static_cast<double>(ztick::example::app_b::kSampleRateHz) +
                         static_cast<double>(voice) * ztick::example::app_b::kVoicePhaseStep +
                         static_cast<double>(channel) * ztick::example::app_b::kChannelPhaseStep;
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
      value = std::clamp(value, -ztick::example::app_b::kRangeLimit,
                         ztick::example::app_b::kRangeLimit);
    }
  }
  if (dither) {
    const double phase = kPi * 2.0 * static_cast<double>(sample_index) / 97.0;
    value += ztick::example::app_b::kDitherAmplitude * std::sin(phase);
  }
  return ztick::example::app_b::kOutputScale * value;
}

/**
 * @brief Compute interleaved reference samples for one configured engine.
 * @param input Full-run interleaved input samples.
 * @param channels Channel count.
 * @param voices Number of synthesized voices.
 * @param stages Number of shaping stages.
 * @param mix Mix of synthesized signal.
 * @param final_quality Whether the saturating curve is selected.
 * @param dither Whether deterministic dither is enabled.
 * @return Float32 reference samples for the full run.
 */
auto compute_reference(const std::vector<float> &input, std::uint32_t channels, std::int64_t voices,
                       std::int64_t stages, double mix, bool final_quality,
                       bool dither) -> std::vector<float> {
  std::vector<float> reference(input.size(), 0.0F);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    const std::uint64_t frame = static_cast<std::uint64_t>(index / channels);
    const auto channel = static_cast<std::uint32_t>(index % channels);
    const double value = engine_sample(input[index], frame, channel, index, voices, stages, mix,
                                       final_quality, dither);
    reference[index] = static_cast<float>(value);
  }
  return reference;
}

/**
 * @brief Compare two samples with absolute and relative tolerances.
 * @param actual Produced sample.
 * @param expected Reference sample.
 * @param absolute_tolerance Absolute tolerance.
 * @param relative_tolerance Relative tolerance.
 * @return True when the samples agree within the configured tolerances.
 */
auto within_tolerance(double actual, double expected, double absolute_tolerance,
                      double relative_tolerance) noexcept -> bool {
  return std::fabs(actual - expected) <=
         absolute_tolerance + relative_tolerance * std::fabs(expected);
}

/**
 * @brief Find one declared parameter by stable name.
 * @param name Parameter name to find; may be NULL.
 * @return Pointer to the declaration, or NULL when the name is unknown.
 */
auto find_parameter(const char *name) noexcept -> const ztick_parameter_descriptor * {
  if (name == nullptr) {
    return nullptr;
  }
  const auto found =
      std::find_if(std::begin(kParameters), std::end(kParameters),
                   [name](const ztick_parameter_descriptor &parameter) noexcept {
                     return parameter.name != nullptr && std::strcmp(parameter.name, name) == 0;
                   });
  return found == std::end(kParameters) ? nullptr : &*found;
}

/**
 * @brief Find the outcome detail string for one strict check name.
 * @param name Stable check name.
 * @return Static detail string for a passing check.
 */
auto success_detail(const char *name) noexcept -> const char * {
  using namespace ztick::example::app_b;
  if (std::strcmp(name, kCheckFinite) == 0) {
    return kDetailFinite;
  }
  if (std::strcmp(name, kCheckRange) == 0) {
    return kDetailRange;
  }
  if (std::strcmp(name, kCheckReference) == 0) {
    return kDetailReference;
  }
  if (std::strcmp(name, kCheckExpectedChange) == 0) {
    return kDetailChange;
  }
  if (std::strcmp(name, kCheckThreadBalance) == 0) {
    return kDetailThreads;
  }
  if (std::strcmp(name, kCheckCustom) == 0) {
    return kDetailCustom;
  }
  return kDetailFailure;
}

/**
 * @brief Evaluate one strict validation check against the finished instance.
 * @param adapter Finished adapter instance.
 * @param check_name Stable check identifier.
 * @return Success when the check passed, otherwise `validation_failure` or `unsupported`.
 */
auto evaluate_check(const ztick_adapter &adapter, const char *check_name) noexcept -> std::int32_t {
  using namespace ztick::example::app_b;
  if (check_name == nullptr) {
    return ztick_status_invalid_argument;
  }
  if (std::strcmp(check_name, kCheckFinite) == 0) {
    const bool finite = std::all_of(adapter.output.begin(), adapter.output.end(),
                                    [](float value) noexcept { return std::isfinite(value); });
    return finite ? ztick_status_success : ztick_status_validation_failure;
  }
  if (std::strcmp(check_name, kCheckRange) == 0) {
    const bool in_range =
        std::all_of(adapter.output.begin(), adapter.output.end(),
                    [](float value) noexcept { return std::fabs(value) <= kRangeLimit; });
    return in_range ? ztick_status_success : ztick_status_validation_failure;
  }
  if (std::strcmp(check_name, kCheckReference) == 0) {
    if (adapter.output.size() != adapter.reference.size()) {
      return ztick_status_validation_failure;
    }
    for (std::size_t index = 0U; index < adapter.output.size(); ++index) {
      if (!within_tolerance(adapter.output[index], adapter.reference[index], kAbsoluteTolerance,
                            kRelativeTolerance)) {
        return ztick_status_validation_failure;
      }
    }
    return ztick_status_success;
  }
  if (std::strcmp(check_name, kCheckExpectedChange) == 0) {
    const double alternate_mix = adapter.mix > 0.5 ? adapter.mix - 0.25 : adapter.mix + 0.25;
    const std::vector<float> alternate =
        compute_reference(adapter.input, adapter.channels, adapter.voices, adapter.stages,
                          alternate_mix, adapter.final_quality, adapter.dither);
    if (alternate.size() != adapter.output.size()) {
      return ztick_status_validation_failure;
    }
    for (std::size_t index = 0U; index < alternate.size(); ++index) {
      if (!within_tolerance(adapter.output[index], alternate[index], kAbsoluteTolerance,
                            kRelativeTolerance)) {
        return ztick_status_success;
      }
    }
    return ztick_status_validation_failure;
  }
  if (std::strcmp(check_name, kCheckThreadBalance) == 0) {
    const auto expected_calls = static_cast<std::uint64_t>(adapter.threads) * adapter.blocks;
    const bool balanced =
        adapter.threads >= 2 && adapter.worker_calls == expected_calls &&
        adapter.worker_durations.size() == expected_calls &&
        std::all_of(adapter.worker_durations.begin(), adapter.worker_durations.end(),
                    [](double value) noexcept { return std::isfinite(value) && value >= 0.0; });
    return balanced ? ztick_status_success : ztick_status_validation_failure;
  }
  if (std::strcmp(check_name, kCheckCustom) == 0) {
    const std::size_t expected_samples =
        static_cast<std::size_t>(adapter.blocks) * adapter.block_frames * adapter.channels;
    const bool valid = adapter.output.size() == expected_samples && adapter.output_peak > 0.0 &&
                       adapter.output_peak <= kRangeLimit;
    return valid ? ztick_status_success : ztick_status_validation_failure;
  }
  return ztick_status_unsupported;
}

/**
 * @brief Emit one metric through a host sink.
 * @param sink Sink callback; must not be NULL.
 * @param context Opaque host context.
 * @param name Stable metric name.
 * @param value Numeric value.
 * @param unit Unit string, or NULL.
 * @param status Provenance label.
 * @param source Collection method.
 * @return The sink status, or `invalid_argument` for a bad sink.
 */
auto emit_metric(ztick_metric_sink_fn sink, void *context, const char *name, double value,
                 const char *unit, ztick_metric_status status,
                 const char *source) noexcept -> std::int32_t {
  if (sink == nullptr || name == nullptr) {
    return ztick_status_invalid_argument;
  }
  return sink(context, name, value, unit, status, source);
}

/**
 * @brief Create one App B adapter instance.
 * @param out_adapter Receives the new opaque handle; must not be NULL.
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
 * @brief Destroy one App B adapter instance.
 * @param adapter Handle to release; may be NULL.
 * @return A `ztick_status` code.
 */
auto destroy(ztick_adapter *adapter) noexcept -> std::int32_t {
  delete adapter;
  return ztick_status_success;
}

/**
 * @brief Describe all formats, typed parameters and unsupported configurations.
 * @param adapter Constant adapter handle; must not be NULL.
 * @param formats Receives the non-owning format array.
 * @param format_count Receives the format count.
 * @param parameters Receives the non-owning typed parameter array.
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
  *formats = kFormats;
  *format_count = kFormatCount;
  *parameters = kParameters;
  *parameter_count = kParameterCount;
  *unsupported = kUnsupported;
  *unsupported_count = kUnsupportedCount;
  return ztick_status_success;
}

/**
 * @brief Apply one validated typed configuration parameter.
 * @param adapter Adapter handle; must not be NULL.
 * @param name Parameter name from @ref describe.
 * @param value Pointer to the value in the descriptor's representation.
 * @param value_size Size of @p value in bytes, including a string terminator.
 * @return A `ztick_status` code.
 */
auto configure(ztick_adapter *adapter, const char *name, const void *value,
               std::size_t value_size) noexcept -> std::int32_t {
  if (adapter == nullptr || adapter->prepared || value == nullptr) {
    return ztick_status_invalid_argument;
  }
  const ztick_parameter_descriptor *parameter = find_parameter(name);
  if (parameter == nullptr) {
    return ztick_status_unsupported;
  }

  switch (parameter->type) {
  case ztick_parameter_type_integer: {
    if (value_size != sizeof(std::int64_t)) {
      return ztick_status_invalid_argument;
    }
    std::int64_t number = 0;
    std::memcpy(&number, value, sizeof(number));
    if (static_cast<double>(number) < parameter->minimum ||
        static_cast<double>(number) > parameter->maximum) {
      return ztick_status_unsupported;
    }
    if (std::strcmp(parameter->name, ztick::example::app_b::kParameterVoices) == 0) {
      adapter->voices = number;
    } else if (std::strcmp(parameter->name, ztick::example::app_b::kParameterStages) == 0) {
      adapter->stages = number;
    } else if (number > kMaxWorkerThreads) {
      return ztick_status_unsupported;
    } else {
      adapter->threads = number;
    }
    return ztick_status_success;
  }
  case ztick_parameter_type_floating_point: {
    if (value_size != sizeof(double)) {
      return ztick_status_invalid_argument;
    }
    double number = 0.0;
    std::memcpy(&number, value, sizeof(number));
    if (number < parameter->minimum || number > parameter->maximum || !std::isfinite(number)) {
      return ztick_status_unsupported;
    }
    adapter->mix = number;
    return ztick_status_success;
  }
  case ztick_parameter_type_boolean: {
    if (value_size != sizeof(std::uint32_t)) {
      return ztick_status_invalid_argument;
    }
    std::uint32_t flag = 0U;
    std::memcpy(&flag, value, sizeof(flag));
    if (flag > 1U) {
      return ztick_status_unsupported;
    }
    adapter->dither = flag == 1U;
    return ztick_status_success;
  }
  case ztick_parameter_type_enumeration: {
    if (value_size != sizeof(std::uint32_t)) {
      return ztick_status_invalid_argument;
    }
    std::uint32_t selection = 0U;
    std::memcpy(&selection, value, sizeof(selection));
    if (selection >= parameter->allowed_value_count) {
      return ztick_status_unsupported;
    }
    adapter->final_quality = std::strcmp(parameter->allowed_values[selection],
                                         ztick::example::app_b::kQualityFinal) == 0;
    return ztick_status_success;
  }
  case ztick_parameter_type_string: {
    if (value_size == 0U || value_size > kMaxProfileLength) {
      return ztick_status_invalid_argument;
    }
    const auto *text = static_cast<const char *>(value);
    if (text[value_size - 1U] != '\0' || std::memchr(text, '\0', value_size - 1U) != nullptr) {
      return ztick_status_invalid_argument;
    }
    try {
      adapter->profile.assign(text);
      return ztick_status_success;
    } catch (...) {
      return ztick_status_internal_error;
    }
  }
  default:
    return ztick_status_unsupported;
  }
}

/**
 * @brief Accept generated input, create references and reset counters outside timing.
 * @param adapter Adapter handle; must not be NULL.
 * @param input Input buffer view supplied by the host.
 * @param reference Optional external reference view; NULL selects adapter generation.
 * @return A `ztick_status` code.
 */
auto prepare(ztick_adapter *adapter, const ztick_buffer_view *input,
             const ztick_buffer_view *reference) noexcept -> std::int32_t {
  if (adapter == nullptr || input == nullptr || !view_valid(*input) ||
      !format_supported(input->format) || adapter->finished) {
    return ztick_status_invalid_argument;
  }
  try {
    const std::size_t count = static_cast<std::size_t>(input->frames) * input->format.channels;
    std::vector<float> samples(count, 0.0F);
    if (!read_view(*input, samples)) {
      return ztick_status_invalid_argument;
    }

    adapter->sample_rate = input->format.sample_rate;
    adapter->channels = input->format.channels;
    adapter->frames = input->frames;
    adapter->block_frames = ztick::example::app_b::kFramesPerBlock;
    adapter->input = std::move(samples);
    if (reference != nullptr) {
      if (!view_valid(*reference) || !format_supported(reference->format) ||
          reference->frames != input->frames ||
          reference->format.channels != input->format.channels) {
        return ztick_status_invalid_argument;
      }
      adapter->reference.assign(count, 0.0F);
      if (!read_view(*reference, adapter->reference)) {
        return ztick_status_invalid_argument;
      }
    } else {
      adapter->reference =
          compute_reference(adapter->input, adapter->channels, adapter->voices, adapter->stages,
                            adapter->mix, adapter->final_quality, adapter->dither);
    }
    adapter->output.assign(count, 0.0F);
    adapter->worker_durations.clear();
    adapter->blocks = 0U;
    adapter->processed_frames = 0U;
    adapter->worker_calls = 0U;
    adapter->processing_seconds = 0.0;
    adapter->output_peak = 0.0;
    adapter->prepared = true;
    return ztick_status_success;
  } catch (...) {
    adapter->prepared = false;
    return ztick_status_internal_error;
  }
}

/**
 * @brief Process one engine block using adapter-owned worker threads.
 * @param adapter Prepared adapter handle.
 * @param input Constant input view for this block.
 * @param output Writable output view for this block.
 * @param timing Optional per-block timing sample; NULL selects aggregate timing.
 * @return A `ztick_status` code.
 */
auto process(ztick_adapter *adapter, const ztick_buffer_view *input, ztick_buffer_view *output,
             ztick_timing_sample *timing) noexcept -> std::int32_t {
  if (adapter == nullptr || input == nullptr || output == nullptr || !adapter->prepared ||
      adapter->finished || !view_valid(*input) || !view_valid(*output) ||
      !format_declared(input->format) || !format_declared(output->format) ||
      input->frames != adapter->block_frames || output->frames != adapter->block_frames ||
      input->format.channels != adapter->channels || output->format.channels != adapter->channels) {
    return ztick_status_invalid_argument;
  }
  if (timing != nullptr && !header_valid(timing->size, timing->version, sizeof(*timing))) {
    return ztick_status_invalid_argument;
  }

  try {
    const std::uint32_t channels = adapter->channels;
    const std::uint32_t frames = input->frames;
    const std::size_t sample_count = static_cast<std::size_t>(frames) * channels;
    const std::size_t base_sample = static_cast<std::size_t>(adapter->blocks) * sample_count;
    if (base_sample + sample_count > adapter->input.size() ||
        adapter->output.size() != adapter->input.size()) {
      return ztick_status_invalid_argument;
    }

    std::vector<float> block_input(sample_count, 0.0F);
    if (!read_view(*input, block_input)) {
      return ztick_status_invalid_argument;
    }
    std::vector<float> block_output(sample_count, 0.0F);
    std::vector<double> durations(static_cast<std::size_t>(adapter->threads), 0.0);
    std::atomic<bool> worker_failed{false};
    std::vector<std::thread> workers;
    workers.reserve(durations.size());

    const auto started = Clock::now();
    for (std::size_t worker = 0U; worker < durations.size(); ++worker) {
      const std::size_t begin = sample_count * worker / durations.size();
      const std::size_t end = sample_count * (worker + 1U) / durations.size();
      workers.emplace_back([&, begin, end, worker]() noexcept {
        const auto worker_started = Clock::now();
        try {
          for (std::size_t index = begin; index < end; ++index) {
            const std::size_t global_index = base_sample + index;
            const auto frame = static_cast<std::uint64_t>(global_index / channels);
            const auto channel = static_cast<std::uint32_t>(global_index % channels);
            const double value = engine_sample(block_input[index], frame, channel, global_index,
                                               adapter->voices, adapter->stages, adapter->mix,
                                               adapter->final_quality, adapter->dither);
            block_output[index] = static_cast<float>(value);
            adapter->output[global_index] = block_output[index];
          }
        } catch (...) {
          worker_failed.store(true, std::memory_order_relaxed);
        }
        durations[worker] = std::chrono::duration<double>(Clock::now() - worker_started).count();
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
    if (worker_failed.load(std::memory_order_relaxed)) {
      return ztick_status_internal_error;
    }
    if (!write_view(*output, block_output)) {
      return ztick_status_invalid_argument;
    }

    const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
    for (const float value : block_output) {
      const double magnitude = std::fabs(static_cast<double>(value));
      if (magnitude > adapter->output_peak) {
        adapter->output_peak = magnitude;
      }
    }
    adapter->processing_seconds += elapsed;
    adapter->worker_durations.insert(adapter->worker_durations.end(), durations.begin(),
                                     durations.end());
    adapter->worker_calls += durations.size();
    adapter->blocks += 1U;
    adapter->processed_frames += frames;
    if (timing != nullptr) {
      timing->block_index = adapter->blocks - 1U;
      timing->duration_seconds = elapsed;
    }
    return ztick_status_success;
  } catch (...) {
    return ztick_status_internal_error;
  }
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
 * @brief Run per-check Strict validation after timing has stopped.
 * @param adapter Constant finished adapter handle.
 * @param requested Requested checks with tolerances and reference sources.
 * @param requested_count Number of requested checks.
 * @param results Output array for outcomes.
 * @param result_count In/out capacity on entry, number written on return.
 * @return Success when every requested check passed, otherwise an explicit failure status.
 */
auto validate(const ztick_adapter *adapter, const ztick_validation_result *requested,
              std::uint32_t requested_count, ztick_validation_result *results,
              std::uint32_t *result_count) noexcept -> std::int32_t {
  if (adapter == nullptr || adapter->finished == false || requested == nullptr ||
      results == nullptr || result_count == nullptr || *result_count < requested_count) {
    return ztick_status_invalid_argument;
  }

  std::int32_t overall = ztick_status_success;
  for (std::uint32_t index = 0U; index < requested_count; ++index) {
    const ztick_validation_result &request = requested[index];
    if (!header_valid(request.size, request.version, sizeof(request)) ||
        request.check_name == nullptr) {
      return ztick_status_invalid_argument;
    }
    const std::int32_t outcome = evaluate_check(*adapter, request.check_name);
    if (outcome == ztick_status_unsupported) {
      return ztick_status_unsupported;
    }
    if (outcome != ztick_status_success) {
      overall = ztick_status_validation_failure;
    }
    results[index] = ztick_validation_result{};
    results[index].size = static_cast<std::uint32_t>(sizeof(results[index]));
    results[index].version = ZTICK_STRUCT_LAYOUT_VERSION;
    results[index].check_name = request.check_name;
    results[index].outcome = static_cast<ztick_status>(outcome);
    results[index].absolute_tolerance = request.absolute_tolerance;
    results[index].relative_tolerance = request.relative_tolerance;
    results[index].reference_source =
        request.reference_source != nullptr ? request.reference_source : kSourceAdapterReference;
    results[index].details =
        outcome == ztick_status_success ? success_detail(request.check_name) : kDetailFailure;
  }
  *result_count = requested_count;
  return overall;
}

/**
 * @brief Emit App B measurements through the host sink.
 * @param adapter Constant finished adapter handle.
 * @param sink Sink callback supplied by the host.
 * @param context Opaque host context passed to @p sink.
 * @return A `ztick_status` code.
 */
auto report(const ztick_adapter *adapter, ztick_metric_sink_fn sink,
            void *context) noexcept -> std::int32_t {
  using namespace ztick::example::app_b;
  if (adapter == nullptr || !adapter->finished || sink == nullptr) {
    return ztick_status_invalid_argument;
  }

  double worker_seconds = 0.0;
  for (const double duration : adapter->worker_durations) {
    worker_seconds += duration;
  }
  const ztick_metric_sink_fn emit = sink;
  std::int32_t status =
      emit_metric(emit, context, kMetricFrames, static_cast<double>(adapter->processed_frames),
                  kUnitFrames, ztick_metric_status_measured, kSourceInstrumentation);
  if (status != ztick_status_success) {
    return status;
  }
  status = emit_metric(emit, context, kMetricBlocks, static_cast<double>(adapter->blocks),
                       kUnitBlocks, ztick_metric_status_measured, kSourceInstrumentation);
  if (status != ztick_status_success) {
    return status;
  }
  status = emit_metric(emit, context, kMetricProcessingSeconds, adapter->processing_seconds,
                       kUnitSeconds, ztick_metric_status_measured, kSourceInstrumentation);
  if (status != ztick_status_success) {
    return status;
  }
  status = emit_metric(emit, context, kMetricWorkerSeconds, worker_seconds, kUnitSeconds,
                       ztick_metric_status_measured, kSourceInstrumentation);
  if (status != ztick_status_success) {
    return status;
  }
  status =
      emit_metric(emit, context, kMetricWorkerCalls, static_cast<double>(adapter->worker_calls),
                  kUnitCalls, ztick_metric_status_measured, kSourceInstrumentation);
  if (status != ztick_status_success) {
    return status;
  }
  status = emit_metric(emit, context, kMetricOutputPeak, adapter->output_peak, nullptr,
                       ztick_metric_status_measured, kSourceInstrumentation);
  if (status != ztick_status_success) {
    return status;
  }
  status = emit_metric(emit, context, kMetricScalingEfficiency, 0.0, nullptr,
                       ztick_metric_status_unavailable, kSourceUnavailable);
  if (status != ztick_status_success) {
    return status;
  }
  status = emit_metric(emit, context, kMetricReferenceFiles, 0.0, nullptr,
                       ztick_metric_status_not_applicable, kSourceNotApplicable);
  if (status != ztick_status_success) {
    return status;
  }
  return emit_metric(emit, context, "app_b.overhead_estimate_seconds", 0.0, kUnitSeconds,
                     ztick_metric_status_estimated, kSourceOverheadEstimate);
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

/**
 * @brief Read-only descriptor exported for host inspection and lifecycle use.
 * @note The symbol name matches @ref ZTICK_ADAPTER_EXPORT_SYMBOL. Reading its data
 *       never calls adapter code; the host does so only after negotiation and trust.
 */
extern "C" const ztick_adapter_descriptor ztick_adapter_export = {
    static_cast<std::uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<std::uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_b::kAdapterId, ztick::example::app_b::kAdapterName, kAdapterVersion,
     kSdkVersion, ztick::example::app_b::kRequiredCapabilities,
     ztick::example::app_b::kAdapterMetadata},
    kOps};
