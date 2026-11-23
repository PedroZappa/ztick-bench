// SPDX-License-Identifier: MIT
/**
 * @file static_adapter.cpp
 * @brief Static App A adapter implementing the complete SDK processor/chain lifecycle.
 *
 * The adapter publishes a read-only descriptor that can be inspected without
 * executing code (SDK_SPECIFICATION §2, §10). Its lifecycle callbacks create
 * independent instances, declare typed parameters, accept generated input and
 * reference data outside timing, process blocks with optional per-block samples,
 * finish counters, run validation after timing and report measurements through a
 * host sink. No C++ exception crosses the C ABI boundary (SDK_SPECIFICATION §8).
 */

#include <static_adapter.hpp>

#include <app_a_contract.hpp>

#include <ztick/sdk.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

/** @brief Version of the App A integration itself. */
const ztick_version kAdapterVersion{
    static_cast<std::uint32_t>(sizeof(ztick_version)), ZTICK_STRUCT_LAYOUT_VERSION,
    ztick::example::app_a::kAdapterVersionMajor, ztick::example::app_a::kAdapterVersionMinor,
    ztick::example::app_a::kAdapterVersionPatch};

/** @brief SDK ABI version this adapter was compiled against. */
const ztick_version kSdkVersion{static_cast<std::uint32_t>(sizeof(ztick_version)),
                                ZTICK_STRUCT_LAYOUT_VERSION, ZTICK_SDK_VERSION_MAJOR,
                                ZTICK_SDK_VERSION_MINOR, ZTICK_SDK_VERSION_PATCH};

/** @brief Supported explicit audio formats; float32 is mandatory. */
const ztick_audio_format kFormats[] = {
    {static_cast<std::uint32_t>(sizeof(ztick_audio_format)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick_sample_format_float32, ztick_channel_layout_interleaved,
     ztick::example::app_a::kSampleRateHz, ztick::example::app_a::kChannels,
     ztick::example::app_a::kFramesPerBlock},
    {static_cast<std::uint32_t>(sizeof(ztick_audio_format)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick_sample_format_float32, ztick_channel_layout_planar, ztick::example::app_a::kSampleRateHz,
     ztick::example::app_a::kChannels, ztick::example::app_a::kFramesPerBlock},
    {static_cast<std::uint32_t>(sizeof(ztick_audio_format)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick_sample_format_float64, ztick_channel_layout_interleaved,
     ztick::example::app_a::kSampleRateHz, ztick::example::app_a::kChannels,
     ztick::example::app_a::kFramesPerBlock},
    {static_cast<std::uint32_t>(sizeof(ztick_audio_format)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick_sample_format_float64, ztick_channel_layout_planar, ztick::example::app_a::kSampleRateHz,
     ztick::example::app_a::kChannels, ztick::example::app_a::kFramesPerBlock},
};

/** @brief Number of declared audio formats. */
constexpr std::uint32_t kFormatCount =
    static_cast<std::uint32_t>(sizeof(kFormats) / sizeof(kFormats[0]));

/** @brief Allowed values of the workload enumeration parameter. */
const char *const kWorkloadValues[] = {ztick::example::app_a::kWorkloadProcessor,
                                       ztick::example::app_a::kWorkloadChain};

/** @brief Declared typed parameters with ranges, defaults and descriptions. */
const ztick_parameter_descriptor kParameters[] = {
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kParameterWorkload, ztick_parameter_type_enumeration, nullptr, 0.0, 0.0,
     0.0, kWorkloadValues, 2U, "Processor uses one gain stage; chain adds smoothing."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kParameterGain, ztick_parameter_type_floating_point, "ratio", 0.0, 4.0,
     1.0, nullptr, 0U, "Multiplicative gain applied by each stage."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kParameterStages, ztick_parameter_type_integer, "stages", 1.0, 2.0, 1.0,
     nullptr, 0U, "Number of gain stages applied in series."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kParameterSmoothing, ztick_parameter_type_boolean, nullptr, 0.0, 1.0,
     0.0, nullptr, 0U, "Enable the one-pole smoothing stage."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kParameterCutoffHz, ztick_parameter_type_floating_point,
     ztick::example::app_a::kUnitHertz, 100.0, 12000.0, 3000.0, nullptr, 0U,
     "Cutoff frequency of the optional smoothing stage."},
    {static_cast<std::uint32_t>(sizeof(ztick_parameter_descriptor)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kParameterLabel, ztick_parameter_type_string, nullptr, 0.0, 0.0, 0.0,
     nullptr, 0U, "Short instance label retained for diagnostics."},
};

/** @brief Number of declared typed parameters. */
constexpr std::uint32_t kParameterCount =
    static_cast<std::uint32_t>(sizeof(kParameters) / sizeof(kParameters[0]));

/** @brief Machine-readable unsupported configurations declared by the adapter. */
const char *const kUnsupported[] = {
    "engine_workload: only processor and chain workloads are implemented",
    "internal_threading: this adapter processes on the caller's thread",
    "variable_block_size: only the declared block size is supported",
    "provided_reference_data: validation references are supplied by the host",
};

/** @brief Number of declared unsupported configurations. */
constexpr std::uint32_t kUnsupportedCount =
    static_cast<std::uint32_t>(sizeof(kUnsupported) / sizeof(kUnsupported[0]));

/** @brief Maximum accepted length of the label string, including its terminator. */
constexpr std::size_t kMaxLabelLength = 64U;

/** @brief Upper bound used by the declared range check. */
constexpr double kRangeLimit = 4.0;

} // namespace

/**
 * @brief Opaque instance state owned by one App A adapter handle.
 * @note Every instance has independent configuration, filter history and
 *       counters, satisfying the multi-instance requirement of SDK_SPECIFICATION §3.
 */
struct ztick_adapter {
  /** @brief Multiplicative gain of each configured stage. */
  double gain{1.0};
  /** @brief Number of gain stages applied in series. */
  std::int64_t stages{1};
  /** @brief Whether the smoothing stage is active. */
  bool smoothing{false};
  /** @brief Whether the chain workload was selected. */
  bool chain_workload{false};
  /** @brief Cutoff frequency of the smoothing stage, in Hz. */
  double cutoff_hz{3000.0};
  /** @brief Host-provided instance label. */
  std::string label{"app_a"};
  /** @brief Sample rate of the prepared workload, in Hz. */
  std::uint32_t sample_rate{0U};
  /** @brief Channel count of the prepared workload. */
  std::uint32_t channels{0U};
  /** @brief Frame count of the prepared workload. */
  std::uint32_t frames{0U};
  /** @brief Copied interleaved input samples, one per frame and channel. */
  std::vector<double> input;
  /** @brief Copied interleaved reference samples, or empty when absent. */
  std::vector<double> reference;
  /** @brief Produced interleaved output samples in processing order. */
  std::vector<double> output;
  /** @brief Per-channel smoothing state. */
  std::vector<double> filter_state;
  /** @brief Number of processed blocks. */
  std::uint64_t blocks{0U};
  /** @brief Number of processed frames. */
  std::uint64_t processed_frames{0U};
  /** @brief Adapter-measured DSP time in seconds. */
  double processing_seconds{0.0};
  /** @brief Largest absolute produced sample. */
  double output_peak{0.0};
  /** @brief Whether `callbacks::prepare` has supplied valid buffers. */
  bool prepared{false};
  /** @brief Whether `callbacks::finish` has finalized this instance. */
  bool finished{false};
};

namespace {

/** @brief Internal helpers for App A lifecycle callbacks. */
namespace detail {

/** @brief Monotonic clock used for adapter-internal DSP timing. */
using Clock = std::chrono::steady_clock;

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
 * @return Four for float32, eight for float64, or zero when unknown.
 */
auto sample_size(const ztick_audio_format &format) noexcept -> std::size_t {
  if (!header_valid(format.size, format.version, sizeof(format))) {
    return 0U;
  }
  if (format.format == ztick_sample_format_float32) {
    return sizeof(float);
  }
  if (format.format == ztick_sample_format_float64) {
    return sizeof(double);
  }
  return 0U;
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
  const std::size_t width = sample_size(view.format);
  if (width == 0U || !header_valid(view.size, view.version, sizeof(view)) ||
      view.planes == nullptr || view.frames == 0U || view.format.channels == 0U) {
    return false;
  }
  if (view.format.layout == ztick_channel_layout_interleaved) {
    if (view.plane_count != 1U || view.planes[0] == nullptr) {
      return false;
    }
  } else if (view.format.layout == ztick_channel_layout_planar) {
    if (view.plane_count != view.format.channels) {
      return false;
    }
    for (std::uint32_t channel = 0U; channel < view.plane_count; ++channel) {
      if (view.planes[channel] == nullptr) {
        return false;
      }
    }
  } else {
    return false;
  }

  if (view.strides != nullptr) {
    for (std::uint32_t plane = 0U; plane < view.plane_count; ++plane) {
      if (view.strides[plane] < width) {
        return false;
      }
    }
  }
  const std::size_t channels = view.format.layout == ztick_channel_layout_interleaved
                                   ? static_cast<std::size_t>(view.format.channels)
                                   : 1U;
  return static_cast<std::size_t>(view.frames) <=
         std::numeric_limits<std::size_t>::max() / (width * channels);
}

/**
 * @brief Resolve the byte stride for one sample or frame plane.
 * @param view View owning the plane.
 * @param plane_index Plane to inspect.
 * @param width Native sample width in bytes.
 * @return Declared stride, or @p width for packed data.
 */
auto plane_stride(const ztick_buffer_view &view, std::uint32_t plane_index,
                  std::size_t width) noexcept -> std::size_t {
  return view.strides == nullptr ? width : view.strides[plane_index];
}

/**
 * @brief Read one sample from a validated buffer view.
 * @param view View to read.
 * @param frame Zero-based frame index.
 * @param channel Zero-based channel index.
 * @return Sample value converted to double.
 * @note Uses `memcpy` so custom byte strides need not be naturally aligned.
 */
auto read_sample(const ztick_buffer_view &view, std::uint32_t frame,
                 std::uint32_t channel) noexcept -> double {
  const std::size_t width = sample_size(view.format);
  const bool interleaved = view.format.layout == ztick_channel_layout_interleaved;
  const std::uint32_t plane_index = interleaved ? 0U : channel;
  const std::size_t stride = plane_stride(view, plane_index, width);
  const auto *base = static_cast<const unsigned char *>(view.planes[plane_index]);
  const std::size_t element = interleaved
                                  ? static_cast<std::size_t>(frame) * view.format.channels + channel
                                  : static_cast<std::size_t>(frame);
  const unsigned char *address = base + element * stride;

  if (view.format.format == ztick_sample_format_float32) {
    float value = 0.0F;
    std::memcpy(&value, address, sizeof(value));
    return static_cast<double>(value);
  }
  double value = 0.0;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

/**
 * @brief Write one double sample into a validated writable buffer view.
 * @param view View to write.
 * @param frame Zero-based frame index.
 * @param channel Zero-based channel index.
 * @param value Sample value to convert and store.
 */
auto write_sample(const ztick_buffer_view &view, std::uint32_t frame, std::uint32_t channel,
                  double value) noexcept -> void {
  const std::size_t width = sample_size(view.format);
  const bool interleaved = view.format.layout == ztick_channel_layout_interleaved;
  const std::uint32_t plane_index = interleaved ? 0U : channel;
  const std::size_t stride = plane_stride(view, plane_index, width);
  auto *base = static_cast<unsigned char *>(view.planes[plane_index]);
  const std::size_t element = interleaved
                                  ? static_cast<std::size_t>(frame) * view.format.channels + channel
                                  : static_cast<std::size_t>(frame);
  unsigned char *address = base + element * stride;

  if (view.format.format == ztick_sample_format_float32) {
    const float converted = static_cast<float>(value);
    std::memcpy(address, &converted, sizeof(converted));
    return;
  }
  std::memcpy(address, &value, sizeof(value));
}

/**
 * @brief Copy every interleaved sample of one view into owning storage.
 * @param view Validated view to copy.
 * @return Double-precision samples in frame/channel order.
 */
auto copy_samples(const ztick_buffer_view &view) -> std::vector<double> {
  const std::size_t count = static_cast<std::size_t>(view.frames) * view.format.channels;
  std::vector<double> samples;
  samples.reserve(count);
  for (std::uint32_t frame = 0U; frame < view.frames; ++frame) {
    for (std::uint32_t channel = 0U; channel < view.format.channels; ++channel) {
      samples.push_back(read_sample(view, frame, channel));
    }
  }
  return samples;
}

/**
 * @brief Convert a cutoff frequency into a stable one-pole smoothing coefficient.
 * @param sample_rate Sample rate in Hz; must be positive.
 * @param cutoff_hz Cutoff frequency in Hz; must be positive and below Nyquist.
 * @return Coefficient in the open interval `(0, 1)`.
 */
auto smoothing_alpha(double sample_rate, double cutoff_hz) noexcept -> double {
  constexpr double kPi = 3.14159265358979323846;
  const double nyquist = sample_rate * 0.5;
  const double cutoff = std::clamp(cutoff_hz, 1.0, nyquist * 0.99);
  const double alpha = 1.0 - std::exp(-2.0 * kPi * cutoff / sample_rate);
  return std::clamp(alpha, 1e-6, 1.0 - 1e-6);
}

/**
 * @brief Apply one supplied metric through the host sink.
 * @param sink Host callback; must not be NULL.
 * @param context Host context forwarded to @p sink.
 * @param name Stable metric name.
 * @param value Numeric metric value.
 * @param unit Unit string, or NULL.
 * @param status Provenance label.
 * @param source Collection method, or NULL.
 * @return The sink's status code.
 */
auto emit_metric(ztick_metric_sink_fn sink, void *context, const char *name, double value,
                 const char *unit, ztick_metric_status status,
                 const char *source) noexcept -> std::int32_t {
  return sink(context, name, value, unit, status, source);
}

} // namespace detail

/** @brief C ABI callbacks implementing the App A lifecycle. */
namespace callbacks {

/**
 * @brief Create one independent adapter instance.
 * @param out_adapter Receives the new handle; set to NULL on failure.
 * @return A `ztick_status` code.
 */
auto create(ztick_adapter **out_adapter) noexcept -> std::int32_t {
  if (out_adapter == nullptr) {
    return ztick_status_invalid_argument;
  }
  *out_adapter = nullptr;
  try {
    *out_adapter = new ztick_adapter{};
    return ztick_status_success;
  } catch (...) {
    return ztick_status_internal_error;
  }
}

/**
 * @brief Destroy one adapter instance.
 * @param adapter Handle to release; NULL is accepted.
 * @return Always `ztick_status_success`.
 */
auto destroy(ztick_adapter *adapter) noexcept -> std::int32_t {
  delete adapter;
  return ztick_status_success;
}

/**
 * @brief Describe declared formats, typed parameters and unsupported configurations.
 * @param adapter Instance handle; must not be NULL.
 * @param formats Receives the non-owning format array.
 * @param format_count Receives the format count.
 * @param parameters Receives the non-owning parameter array.
 * @param parameter_count Receives the parameter count.
 * @param unsupported Receives the non-owning unsupported-reason array.
 * @param unsupported_count Receives the unsupported count.
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
 * @brief Apply one validated typed parameter to an adapter instance.
 * @param adapter Instance to configure.
 * @param name Declared parameter name.
 * @param value Pointer to the value in its declared representation.
 * @param value_size Size of @p value in bytes.
 * @return A `ztick_status` code; unknown names and values are unsupported.
 */
auto configure(ztick_adapter *adapter, const char *name, const void *value,
               std::size_t value_size) noexcept -> std::int32_t {
  if (adapter == nullptr || name == nullptr || value == nullptr) {
    return ztick_status_invalid_argument;
  }
  try {
    const std::string parameter(name);
    if (parameter == ztick::example::app_a::kParameterWorkload) {
      if (value_size != sizeof(std::uint32_t)) {
        return ztick_status_invalid_argument;
      }
      std::uint32_t selection = 0U;
      std::memcpy(&selection, value, sizeof(selection));
      if (selection == 0U) {
        adapter->chain_workload = false;
      } else if (selection == 1U) {
        adapter->chain_workload = true;
      } else {
        return ztick_status_unsupported;
      }
      return ztick_status_success;
    }
    if (parameter == ztick::example::app_a::kParameterGain) {
      if (value_size != sizeof(double)) {
        return ztick_status_invalid_argument;
      }
      double gain = 0.0;
      std::memcpy(&gain, value, sizeof(gain));
      if (!std::isfinite(gain) || gain < 0.0 || gain > 4.0) {
        return ztick_status_unsupported;
      }
      adapter->gain = gain;
      return ztick_status_success;
    }
    if (parameter == ztick::example::app_a::kParameterStages) {
      if (value_size != sizeof(std::int64_t)) {
        return ztick_status_invalid_argument;
      }
      std::int64_t stages = 0;
      std::memcpy(&stages, value, sizeof(stages));
      if (stages < 1 || stages > 2) {
        return ztick_status_unsupported;
      }
      adapter->stages = stages;
      return ztick_status_success;
    }
    if (parameter == ztick::example::app_a::kParameterSmoothing) {
      if (value_size != sizeof(std::uint32_t)) {
        return ztick_status_invalid_argument;
      }
      std::uint32_t enabled = 2U;
      std::memcpy(&enabled, value, sizeof(enabled));
      if (enabled > 1U) {
        return ztick_status_unsupported;
      }
      adapter->smoothing = enabled == 1U;
      return ztick_status_success;
    }
    if (parameter == ztick::example::app_a::kParameterCutoffHz) {
      if (value_size != sizeof(double)) {
        return ztick_status_invalid_argument;
      }
      double cutoff = 0.0;
      std::memcpy(&cutoff, value, sizeof(cutoff));
      if (!std::isfinite(cutoff) || cutoff < 100.0 || cutoff > 12000.0) {
        return ztick_status_unsupported;
      }
      adapter->cutoff_hz = cutoff;
      return ztick_status_success;
    }
    if (parameter == ztick::example::app_a::kParameterLabel) {
      const auto *text = static_cast<const char *>(value);
      if (value_size == 0U || value_size > kMaxLabelLength || text[value_size - 1U] != '\0' ||
          std::strlen(text) != value_size - 1U) {
        return ztick_status_invalid_argument;
      }
      adapter->label = text;
      return ztick_status_success;
    }
    return ztick_status_unsupported;
  } catch (...) {
    return ztick_status_internal_error;
  }
}

/**
 * @brief Copy input and reference data outside the primary timing interval.
 * @param adapter Instance to prepare.
 * @param input Generated input view.
 * @param reference Optional reference view, or NULL.
 * @return A `ztick_status` code.
 */
auto prepare(ztick_adapter *adapter, const ztick_buffer_view *input,
             const ztick_buffer_view *reference) noexcept -> std::int32_t {
  if (adapter == nullptr || input == nullptr || !detail::view_valid(*input) ||
      !detail::format_supported(input->format)) {
    return ztick_status_invalid_argument;
  }
  if (reference != nullptr &&
      (!detail::view_valid(*reference) || !detail::same_format(input->format, reference->format))) {
    return ztick_status_invalid_argument;
  }
  try {
    adapter->sample_rate = input->format.sample_rate;
    adapter->channels = input->format.channels;
    adapter->frames = input->format.frames;
    adapter->input = detail::copy_samples(*input);
    adapter->reference =
        reference == nullptr ? std::vector<double>{} : detail::copy_samples(*reference);
    adapter->output.clear();
    adapter->filter_state.assign(adapter->channels, 0.0);
    adapter->blocks = 0U;
    adapter->processed_frames = 0U;
    adapter->processing_seconds = 0.0;
    adapter->output_peak = 0.0;
    adapter->prepared = true;
    adapter->finished = false;
    return ztick_status_success;
  } catch (...) {
    return ztick_status_internal_error;
  }
}

/**
 * @brief Process one block and optionally emit a raw per-block timing sample.
 * @param adapter Prepared instance.
 * @param input Constant input view for this block.
 * @param output Writable output view for this block.
 * @param timing Optional timing sample; NULL selects aggregate-only timing.
 * @return A `ztick_status` code.
 */
// cppcheck-suppress constParameterCallback
auto process(ztick_adapter *adapter, const ztick_buffer_view *input, ztick_buffer_view *output,
             ztick_timing_sample *timing) noexcept -> std::int32_t {
  if (adapter == nullptr || input == nullptr || output == nullptr || !adapter->prepared ||
      adapter->finished || !detail::view_valid(*input) || !detail::view_valid(*output) ||
      !detail::same_format(input->format, output->format) ||
      !detail::format_declared(input->format)) {
    return ztick_status_invalid_argument;
  }
  if (timing != nullptr && !detail::header_valid(timing->size, timing->version, sizeof(*timing))) {
    return ztick_status_invalid_argument;
  }
  if (input->format.sample_rate != adapter->sample_rate ||
      input->format.channels != adapter->channels || input->format.frames > adapter->frames) {
    return ztick_status_invalid_argument;
  }

  try {
    const double multiplier = std::pow(adapter->gain, static_cast<double>(adapter->stages));
    const double alpha = adapter->smoothing
                             ? detail::smoothing_alpha(adapter->sample_rate, adapter->cutoff_hz)
                             : 0.0;
    const auto start = detail::Clock::now();
    for (std::uint32_t frame = 0U; frame < input->format.frames; ++frame) {
      for (std::uint32_t channel = 0U; channel < input->format.channels; ++channel) {
        double value = detail::read_sample(*input, frame, channel) * multiplier;
        if (adapter->smoothing) {
          double &state = adapter->filter_state[channel];
          state += alpha * (value - state);
          value = state;
        }
        detail::write_sample(*output, frame, channel, value);
        adapter->output.push_back(value);
        adapter->output_peak = std::max(adapter->output_peak, std::abs(value));
      }
    }
    const auto stop = detail::Clock::now();
    const double elapsed = std::chrono::duration<double>(stop - start).count();
    adapter->processing_seconds += elapsed;
    adapter->processed_frames += input->format.frames;
    if (timing != nullptr) {
      timing->block_index = adapter->blocks;
      timing->duration_seconds = elapsed;
    }
    ++adapter->blocks;
    return ztick_status_success;
  } catch (...) {
    return ztick_status_internal_error;
  }
}

/**
 * @brief Finalize counters after all timed processing has stopped.
 * @param adapter Prepared instance.
 * @return A `ztick_status` code.
 */
auto finish(ztick_adapter *adapter) noexcept -> std::int32_t {
  if (adapter == nullptr || !adapter->prepared) {
    return ztick_status_invalid_argument;
  }
  adapter->finished = true;
  return ztick_status_success;
}

/**
 * @brief Run requested post-timing checks and write per-check outcomes.
 * @param adapter Finished instance.
 * @param requested Requested checks with tolerances and reference sources.
 * @param requested_count Number of requested checks.
 * @param results Output array for outcomes.
 * @param result_capacity In/out capacity on entry, count written on return.
 * @return A `ztick_status` code for the validate operation itself.
 */
auto validate(const ztick_adapter *adapter, const ztick_validation_result *requested,
              std::uint32_t requested_count, ztick_validation_result *results,
              std::uint32_t *result_capacity) noexcept -> std::int32_t {
  if (adapter == nullptr || !adapter->finished || results == nullptr ||
      result_capacity == nullptr) {
    return ztick_status_invalid_argument;
  }
  if (requested_count == 0U) {
    *result_capacity = 0U;
    return ztick_status_success;
  }
  if (requested == nullptr || *result_capacity < requested_count) {
    *result_capacity = 0U;
    return ztick_status_invalid_argument;
  }
  if (adapter->output.empty()) {
    return ztick_status_internal_error;
  }

  const double multiplier = std::pow(adapter->gain, static_cast<double>(adapter->stages));
  for (std::uint32_t index = 0U; index < requested_count; ++index) {
    ztick_validation_result &result = results[index];
    result = ztick_validation_result{};
    result.size = static_cast<std::uint32_t>(sizeof(result));
    result.version = ZTICK_STRUCT_LAYOUT_VERSION;
    result.absolute_tolerance = requested[index].absolute_tolerance;
    result.relative_tolerance = requested[index].relative_tolerance;
    result.reference_source = requested[index].reference_source != nullptr
                                  ? requested[index].reference_source
                                  : "unspecified";
    result.details = "check passed";
    result.outcome = ztick_status_success;

    const char *check_name = requested[index].check_name;
    result.check_name = check_name;
    if (!detail::header_valid(requested[index].size, requested[index].version,
                              sizeof(requested[index])) ||
        check_name == nullptr) {
      result.outcome = ztick_status_unsupported;
      result.details = "malformed_request";
      continue;
    }

    const std::string check(check_name);
    if (check == ztick::example::app_a::kCheckFinite) {
      const bool finite = std::all_of(adapter->output.begin(), adapter->output.end(),
                                      [](double value) noexcept { return std::isfinite(value); });
      if (!finite) {
        result.outcome = ztick_status_validation_failure;
        result.details = "non_finite_output";
      }
    } else if (check == ztick::example::app_a::kCheckRange) {
      const bool in_range =
          std::all_of(adapter->output.begin(), adapter->output.end(), [](double value) noexcept {
            return std::isfinite(value) && std::abs(value) <= kRangeLimit;
          });
      if (!in_range) {
        result.outcome = ztick_status_validation_failure;
        result.details = "output_out_of_range";
      }
    } else if (check == ztick::example::app_a::kCheckReference) {
      if (adapter->reference.size() != adapter->output.size()) {
        result.outcome = ztick_status_unsupported;
        result.details = "no_reference";
      } else {
        bool matched = true;
        for (std::size_t sample = 0U; sample < adapter->output.size(); ++sample) {
          const double reference = adapter->reference[sample];
          const double allowance =
              result.absolute_tolerance + result.relative_tolerance * std::abs(reference);
          if (std::abs(adapter->output[sample] - reference) > allowance) {
            matched = false;
            break;
          }
        }
        if (!matched) {
          result.outcome = ztick_status_validation_failure;
          result.details = "reference_mismatch";
        }
      }
    } else if (check == ztick::example::app_a::kCheckExpectedChange) {
      bool changed = false;
      const std::size_t count = std::min(adapter->output.size(), adapter->input.size());
      for (std::size_t sample = 0U; sample < count; ++sample) {
        const double allowance = result.absolute_tolerance +
                                 result.relative_tolerance * std::abs(adapter->input[sample]);
        if (std::abs(adapter->output[sample] - adapter->input[sample]) > allowance) {
          changed = true;
          break;
        }
      }
      if (!changed) {
        result.outcome = ztick_status_validation_failure;
        result.details = "output_unchanged";
      }
    } else if (check == ztick::example::app_a::kCheckCustom) {
      const double limit = multiplier * 2.0 + (adapter->chain_workload ? 1.0 : 0.5);
      const bool bounded = std::all_of(adapter->output.begin(), adapter->output.end(),
                                       [limit](double value) noexcept {
                                         return std::isfinite(value) && std::abs(value) <= limit;
                                       });
      if (!bounded) {
        result.outcome = ztick_status_validation_failure;
        result.details = "custom_bound_exceeded";
      }
    } else {
      result.outcome = ztick_status_unsupported;
      result.details = "unknown_check";
    }
  }
  *result_capacity = requested_count;
  return ztick_status_success;
}

/**
 * @brief Report adapter measurements through the host-provided metric sink.
 * @param adapter Finished instance.
 * @param sink Host metric callback.
 * @param context Opaque host context.
 * @return A `ztick_status` code, including a propagated sink failure.
 */
auto report(const ztick_adapter *adapter, ztick_metric_sink_fn sink,
            void *context) noexcept -> std::int32_t {
  if (adapter == nullptr || sink == nullptr || !adapter->finished) {
    return ztick_status_invalid_argument;
  }
  std::int32_t status = detail::emit_metric(sink, context, ztick::example::app_a::kMetricFrames,
                                            static_cast<double>(adapter->processed_frames),
                                            ztick::example::app_a::kUnitFrames,
                                            ztick_metric_status_measured, "adapter_counter");
  if (status != ztick_status_success) {
    return status;
  }
  status = detail::emit_metric(
      sink, context, ztick::example::app_a::kMetricBlocks, static_cast<double>(adapter->blocks),
      ztick::example::app_a::kUnitBlocks, ztick_metric_status_measured, "adapter_counter");
  if (status != ztick_status_success) {
    return status;
  }
  status = detail::emit_metric(sink, context, ztick::example::app_a::kMetricProcessingSeconds,
                               adapter->processing_seconds, ztick::example::app_a::kUnitSeconds,
                               ztick_metric_status_measured, "adapter_clock");
  if (status != ztick_status_success) {
    return status;
  }
  status = detail::emit_metric(sink, context, ztick::example::app_a::kMetricOutputPeak,
                               adapter->output_peak, nullptr, ztick_metric_status_measured,
                               adapter->label.c_str());
  if (status != ztick_status_success) {
    return status;
  }
  status = detail::emit_metric(sink, context, ztick::example::app_a::kMetricOverheadSeconds, 0.0,
                               ztick::example::app_a::kUnitSeconds, ztick_metric_status_unavailable,
                               "not_calibrated");
  if (status != ztick_status_success) {
    return status;
  }
  return detail::emit_metric(sink, context, ztick::example::app_a::kMetricScalingEfficiency, 0.0,
                             nullptr, ztick_metric_status_not_applicable, "single_instance");
}

} // namespace callbacks

/** @brief Complete lifecycle table published by the static App A adapter. */
const ztick_adapter_ops kOps{static_cast<std::uint32_t>(sizeof(ztick_adapter_ops)),
                             ZTICK_STRUCT_LAYOUT_VERSION,
                             callbacks::create,
                             callbacks::destroy,
                             callbacks::describe,
                             callbacks::configure,
                             callbacks::prepare,
                             callbacks::process,
                             callbacks::finish,
                             callbacks::validate,
                             callbacks::report};

} // namespace

/**
 * @brief Static registration-point descriptor for the App A adapter.
 * @note The descriptor has static storage duration so it outlives the SDK
 *       registry. Inspecting it never calls the lifecycle table.
 */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor = {
    static_cast<std::uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<std::uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     ztick::example::app_a::kAdapterId, ztick::example::app_a::kAdapterName, kAdapterVersion,
     kSdkVersion, ztick::example::app_a::kRequiredCapabilities,
     ztick::example::app_a::kAdapterMetadata},
    kOps};

/**
 * @brief Register the App A descriptor with the host SDK registry.
 * @return The `ztick_status` code from @ref ztick_register_adapter; a second call
 *         returns `ztick_status_invalid_argument` for the duplicate identifier.
 * @note Validation stores metadata only; it creates no instance and calls no
 *       lifecycle callback (SDK_SPECIFICATION §2, §10).
 */
extern "C" int32_t ztick_test_app_static_register(void) {
  return ztick_register_adapter(&ztick_test_app_static_descriptor);
}
