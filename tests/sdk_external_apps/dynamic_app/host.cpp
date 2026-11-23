// SPDX-License-Identifier: MIT
/**
 * @file host.cpp
 * @brief External host for the out-of-tree dynamic integration (P3.9).
 *
 * The host loads a shared module with the platform loader, inspects its
 * read-only descriptor before executing any callback, verifies SDK
 * compatibility and then drives the full engine lifecycle. Only the installed
 * public C ABI and standard/platform headers are used, so this host can be
 * compiled outside the ztick-bench source tree.
 */

#include <ztick/sdk.h>

#include <dlfcn.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

/** @brief Number of measured blocks in one run. */
constexpr std::uint32_t kBlocks = 8U;
/** @brief Frames in one processing block. */
constexpr std::uint32_t kFrames = 64U;
/** @brief Explicit sample rate of the declared format, in Hz. */
constexpr std::uint32_t kSampleRate = 48000U;
/** @brief Absolute and relative tolerance used by both validation checks. */
constexpr double kTolerance = 1.0e-5;

/** @brief Name of the floating-point mix parameter. */
constexpr const char kMixParameter[] = "mix";
/** @brief Name of the finite-output validation check. */
constexpr const char kFiniteCheck[] = "engine.finite";
/** @brief Name of the reference-comparison validation check. */
constexpr const char kReferenceCheck[] = "engine.matches_reference";

/** @brief Collected metric name and provenance status. */
struct MetricRecord {
  /** @brief Stable machine-readable metric name. */
  const char *name;
  /** @brief Provenance label reported with the value. */
  ztick_metric_status status;
};

/** @brief Sink context that owns all collected metric records. */
struct MetricContext {
  /** @brief Metrics emitted by the module's report callback. */
  std::vector<MetricRecord> metrics;
};

/** @brief Explicit format expected from the loaded module. */
const ztick_audio_format kExpectedFormat{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                         ZTICK_STRUCT_LAYOUT_VERSION,
                                         ztick_sample_format_float32,
                                         ztick_channel_layout_interleaved,
                                         kSampleRate,
                                         1U,
                                         kFrames};

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
 * @brief Report one failed expectation and return its result.
 * @param condition Condition that must hold.
 * @param message Explanation printed when the condition fails.
 * @return True when the condition held.
 */
auto expect(bool condition, const char *message) noexcept -> bool {
  if (!condition) {
    std::cerr << "external dynamic host: " << message << '\n';
  }
  return condition;
}

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
 * @brief Collect one reported metric in the host sink.
 * @param context Opaque @ref MetricContext pointer.
 * @param name Stable metric name.
 * @param value Numeric metric value, ignored by this sink.
 * @param unit Unit string, ignored by this sink.
 * @param status Provenance label.
 * @param source Collection method, ignored by this sink.
 * @return `ztick_status_success`.
 */
auto collect_metric(void *context, const char *name, double, const char *,
                    ztick_metric_status status, const char *) noexcept -> std::int32_t {
  auto *metrics = static_cast<MetricContext *>(context);
  metrics->metrics.push_back(MetricRecord{name, status});
  return ztick_status_success;
}

/**
 * @brief Inspect and drive one loaded module through its full lifecycle.
 * @param descriptor Read-only descriptor resolved from the shared module.
 * @return True when metadata, compatibility and every lifecycle step pass.
 */
auto run_lifecycle(const ztick_adapter_descriptor *descriptor) noexcept -> bool {
  if (!expect(header_valid(descriptor->size, descriptor->version, sizeof(*descriptor)),
              "descriptor header is invalid") ||
      !expect(
          header_valid(descriptor->info.size, descriptor->info.version, sizeof(descriptor->info)),
          "descriptor info header is invalid") ||
      !expect(header_valid(descriptor->ops.size, descriptor->ops.version, sizeof(descriptor->ops)),
              "descriptor ops header is invalid")) {
    return false;
  }

  const auto &info = descriptor->info;
  if (!expect(info.identifier != nullptr && info.name != nullptr,
              "descriptor identity is incomplete") ||
      !expect((info.capabilities & ZTICK_CAPABILITY_ENGINE) != 0U,
              "module does not declare the engine capability") ||
      !expect(ztick_sdk_is_compatible(&info.sdk_version) == ztick_status_success,
              "module SDK version is incompatible")) {
    return false;
  }

  const auto &ops = descriptor->ops;
  if (!expect(ops.create != nullptr && ops.destroy != nullptr && ops.describe != nullptr &&
                  ops.configure != nullptr && ops.prepare != nullptr && ops.process != nullptr &&
                  ops.finish != nullptr && ops.validate != nullptr && ops.report != nullptr,
              "module does not implement every lifecycle callback")) {
    return false;
  }

  ztick_adapter *adapter = nullptr;
  if (!expect(ops.create(&adapter) == ztick_status_success && adapter != nullptr,
              "create failed")) {
    return false;
  }

  const ztick_audio_format *formats = nullptr;
  std::uint32_t format_count = 0U;
  const ztick_parameter_descriptor *parameters = nullptr;
  std::uint32_t parameter_count = 0U;
  const char *const *unsupported = nullptr;
  std::uint32_t unsupported_count = 0U;
  bool ok =
      expect(ops.describe(adapter, &formats, &format_count, &parameters, &parameter_count,
                          &unsupported, &unsupported_count) == ztick_status_success,
             "describe failed") &&
      expect(format_count == 1U && formats != nullptr &&
                 header_valid(formats[0].size, formats[0].version, sizeof(formats[0])) &&
                 formats[0].format == ztick_sample_format_float32 && formats[0].frames == kFrames &&
                 formats[0].sample_rate == kSampleRate,
             "module did not declare the expected float32 format") &&
      expect(parameter_count == 1U && parameters != nullptr &&
                 std::strcmp(parameters[0].name, kMixParameter) == 0 && unsupported_count > 0U,
             "module did not declare its typed parameter and unsupported reasons");

  const double mix = 0.75;
  if (ok) {
    ok = expect(ops.configure(adapter, kMixParameter, &mix, sizeof(mix)) == ztick_status_success,
                "configure failed");
  }

  std::vector<float> input(static_cast<std::size_t>(kBlocks) * kFrames);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    input[index] = static_cast<float>(
        0.5 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * static_cast<double>(index) /
                       static_cast<double>(kSampleRate)));
  }
  void *full_planes[1]{input.data()};
  const auto full_view =
      make_view(full_planes, static_cast<std::uint32_t>(input.size()), kExpectedFormat);
  if (ok) {
    ok =
        expect(ops.prepare(adapter, &full_view, nullptr) == ztick_status_success, "prepare failed");
  }

  for (std::uint32_t block = 0U; ok && block < kBlocks; ++block) {
    float block_input[kFrames]{};
    float block_output[kFrames]{};
    std::memcpy(block_input, input.data() + block * kFrames, sizeof(block_input));
    void *input_planes[1]{block_input};
    void *output_planes[1]{block_output};
    const auto input_view = make_view(input_planes, kFrames, kExpectedFormat);
    auto output_view = make_view(output_planes, kFrames, kExpectedFormat);
    ok = expect(ops.process(adapter, &input_view, &output_view, nullptr) == ztick_status_success,
                "process failed");
  }

  if (ok) {
    ok = expect(ops.finish(adapter) == ztick_status_success, "finish failed");
  }

  ztick_validation_result requested[2]{};
  for (auto &check : requested) {
    check.size = static_cast<std::uint32_t>(sizeof(check));
    check.version = ZTICK_STRUCT_LAYOUT_VERSION;
    check.absolute_tolerance = kTolerance;
    check.relative_tolerance = kTolerance;
    check.reference_source = "module_generated_reference";
  }
  requested[0].check_name = kFiniteCheck;
  requested[1].check_name = kReferenceCheck;
  ztick_validation_result results[2]{};
  std::uint32_t result_count = 2U;
  if (ok) {
    ok = expect(ops.validate(adapter, requested, 2U, results, &result_count) ==
                        ztick_status_success &&
                    result_count == 2U && results[0].outcome == ztick_status_success &&
                    results[1].outcome == ztick_status_success,
                "validation failed");
  }

  MetricContext context;
  if (ok) {
    ok = expect(ops.report(adapter, &collect_metric, &context) == ztick_status_success,
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

  const std::int32_t destroy_status = ops.destroy(adapter);
  return expect(destroy_status == ztick_status_success, "destroy failed") && ok;
}

/**
 * @brief Load one module and verify its descriptor and lifecycle.
 * @param module_path Path of the shared module to load.
 * @return True when the module is conformant and every lifecycle step passes.
 */
auto run_module(const char *module_path) noexcept -> bool {
  void *handle = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    std::cerr << "external dynamic host: dlopen failed: " << dlerror() << '\n';
    return false;
  }

  dlerror();
  void *symbol = dlsym(handle, ZTICK_ADAPTER_EXPORT_SYMBOL);
  const char *symbol_error = dlerror();
  if (symbol_error != nullptr || symbol == nullptr) {
    std::cerr << "external dynamic host: export symbol not found: "
              << (symbol_error != nullptr ? symbol_error : "NULL") << '\n';
    dlclose(handle);
    return false;
  }

  const auto *descriptor = static_cast<const ztick_adapter_descriptor *>(symbol);
  const bool passed = run_lifecycle(descriptor);
  dlclose(handle);
  return passed;
}

} // namespace

/**
 * @brief Entry point for the out-of-tree dynamic integration host.
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments; @p argv[1] is the module path.
 * @return Zero when the module loads and its full lifecycle passes.
 */
auto main(int argc, char **argv) -> int {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <module-path>\n";
    return 2;
  }
  if (!run_module(argv[1])) {
    return 1;
  }
  std::cout << "external dynamic host: module lifecycle completed\n";
  return 0;
}
