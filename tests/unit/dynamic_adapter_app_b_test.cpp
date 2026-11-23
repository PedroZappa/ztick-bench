// SPDX-License-Identifier: MIT
/**
 * @file dynamic_adapter_app_b_test.cpp
 * @brief Deterministic tests for the dynamic App B engine application.
 *
 * One test exercises the host harness end to end and checks the collected Strict
 * validation and instrumented metric evidence. Additional tests drive the loaded
 * descriptor directly through invalid arguments and unsupported configurations so
 * failure paths are observable. Expected values come from the published App B
 * contract, not from adapter internals.
 */

#include <catch2/catch_test_macros.hpp>

#include <app_b_contract.hpp>
#include <app_b_harness.hpp>

#include <ztick/plugin/dynamic_runtime.hpp>

#include <ztick/sdk.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

/** @brief Stable identifier expected from the dynamic App B module. */
constexpr const char kExpectedIdentifier[] = "ztick.test.dynamic.app_b";

/**
 * @brief Find one harness metric by name and provenance label.
 * @param metrics Metrics to search.
 * @param name Stable metric name.
 * @param status Required provenance label.
 * @return Pointer to the matching metric, or NULL when absent.
 */
const ztick::example::app_b::MetricReport *
find_metric(const std::vector<ztick::example::app_b::MetricReport> &metrics, const char *name,
            ztick_metric_status status) noexcept {
  const auto found =
      std::find_if(metrics.begin(), metrics.end(), [name, status](const auto &metric) noexcept {
        return metric.name == name && metric.status == status;
      });
  return found == metrics.end() ? nullptr : &*found;
}

/**
 * @brief Release an adapter handle on every exit path.
 */
struct AdapterGuard {
  /** @brief Lifecycle table that owns the destroy callback. */
  const ztick_adapter_ops *ops{nullptr};
  /** @brief Handle to release, or NULL. */
  ztick_adapter *adapter{nullptr};

  /** @brief Construct a guard from one lifecycle table and handle. */
  AdapterGuard(const ztick_adapter_ops *ops_in, ztick_adapter *adapter_in) noexcept
      : ops(ops_in), adapter(adapter_in) {}

  /** @brief Destroy the retained adapter. */
  ~AdapterGuard() {
    if (ops != nullptr && adapter != nullptr && ops->destroy != nullptr) {
      (void)ops->destroy(adapter);
    }
  }

  /** @brief Copying an adapter owner is disabled. */
  AdapterGuard(const AdapterGuard &) = delete;

  /** @brief Copy assignment of an adapter owner is disabled. */
  AdapterGuard &operator=(const AdapterGuard &) = delete;
};

/** @brief Plane-pointer storage referenced by one planar buffer view. */
struct PlanarStorage {
  /** @brief One pointer per declared channel. */
  std::array<void *, ztick::example::app_b::kChannels> planes{};
};

/** @brief Metric sink state used by the direct lifecycle test. */
struct SinkState {
  /** @brief Number of metrics passed to the sink. */
  std::uint32_t calls{0U};
};

/**
 * @brief Build one planar float32 view over caller-owned planes.
 * @param planes Per-channel sample storage; must outlive the returned view.
 * @param frames Number of frames exposed by the view.
 * @param storage Plane-pointer array referenced by the view.
 * @return Buffer view with initialized C ABI headers.
 */
// cppcheck-suppress constParameterReference
auto make_planar_view(std::array<std::vector<float>, ztick::example::app_b::kChannels> &planes,
                      std::uint32_t frames, PlanarStorage &storage) noexcept -> ztick_buffer_view {
  for (std::uint32_t channel = 0U; channel < ztick::example::app_b::kChannels; ++channel) {
    storage.planes[channel] = planes[channel].data();
  }
  const ztick_audio_format format{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                  ZTICK_STRUCT_LAYOUT_VERSION,
                                  ztick_sample_format_float32,
                                  ztick_channel_layout_planar,
                                  ztick::example::app_b::kSampleRateHz,
                                  ztick::example::app_b::kChannels,
                                  frames};
  return ztick_buffer_view{static_cast<std::uint32_t>(sizeof(ztick_buffer_view)),
                           ZTICK_STRUCT_LAYOUT_VERSION,
                           storage.planes.data(),
                           nullptr,
                           ztick::example::app_b::kChannels,
                           frames,
                           format};
}

/**
 * @brief Count metrics received from the adapter.
 * @param context Pointer to @ref SinkState; must not be NULL.
 * @param name Metric name; unused.
 * @param value Metric value; unused.
 * @param unit Metric unit; unused.
 * @param status Provenance label; unused.
 * @param source Metric source; unused.
 * @return Always `ztick_status_success`.
 */
auto counting_sink(void *context, const char *name, double value, const char *unit,
                   ztick_metric_status status, const char *source) noexcept -> std::int32_t {
  (void)name;
  (void)value;
  (void)unit;
  (void)status;
  (void)source;
  if (context != nullptr) {
    ++static_cast<SinkState *>(context)->calls;
  }
  return ztick_status_success;
}

/**
 * @brief Refuse every metric to exercise the adapter's sink-failure path.
 * @param context Opaque host context; unused.
 * @param name Metric name; unused.
 * @param value Metric value; unused.
 * @param unit Metric unit; unused.
 * @param status Provenance label; unused.
 * @param source Metric source; unused.
 * @return `ztick_status_internal_error` for every metric.
 */
auto failing_sink(void *context, const char *name, double value, const char *unit,
                  ztick_metric_status status, const char *source) noexcept -> std::int32_t {
  (void)context;
  (void)name;
  (void)value;
  (void)unit;
  (void)status;
  (void)source;
  return ztick_status_internal_error;
}

} // namespace

/**
 * @brief Verify the dynamic App B harness succeeds with complete Strict evidence.
 */
TEST_CASE("dynamic app B harness runs the engine lifecycle end to end", "[sdk][dynamic][app_b]") {
  const ztick::example::app_b::HarnessResult result = ztick::example::app_b::run_harness(
      ZTICK_DYNAMIC_APP_B_PATH, ztick::example::app_b::HarnessIsolation::in_process);

  REQUIRE(result.ok);
  CHECK(result.error.empty());
  CHECK(result.inspection_status == ztick_status_success);
  CHECK(result.sdk_version_status == ztick_status_success);
  CHECK(result.negotiation_status == ztick_status_success);
  CHECK(result.runtime_load_status == ztick_status_success);
  CHECK(result.trust_authorized);
  CHECK(result.trust_status == 0);
  CHECK(result.runner_status == ztick_status_success);
  CHECK(result.execution_mode == ztick_execution_mode_in_process);
  CHECK(result.required_capabilities ==
        (ZTICK_CAPABILITY_ENGINE | ZTICK_CAPABILITY_PER_BLOCK_TIMING |
         ZTICK_CAPABILITY_INTERNAL_THREADING | ZTICK_CAPABILITY_REFERENCE_DATA |
         ZTICK_CAPABILITY_CUSTOM_VALIDATION));
  CHECK(result.format_count == 2U);
  CHECK(result.parameter_count == 7U);
  CHECK(result.unsupported_count >= 5U);
  CHECK(result.block_count == ztick::example::app_b::kBlockCount);
  CHECK(result.processed_frames ==
        ztick::example::app_b::kBlockCount * ztick::example::app_b::kFramesPerBlock);
  CHECK(result.validation_check_count == 6U);
  CHECK(result.validation_results.size() == 6U);

  const auto has_check = [&result](const char *name) noexcept {
    return std::any_of(result.validation_results.begin(), result.validation_results.end(),
                       [name](const auto &entry) noexcept {
                         return entry.check_name == name && entry.status == ztick_status_success;
                       });
  };
  CHECK(has_check(ztick::example::app_b::kCheckFinite));
  CHECK(has_check(ztick::example::app_b::kCheckRange));
  CHECK(has_check(ztick::example::app_b::kCheckReference));
  CHECK(has_check(ztick::example::app_b::kCheckExpectedChange));
  CHECK(has_check(ztick::example::app_b::kCheckThreadBalance));
  CHECK(has_check(ztick::example::app_b::kCheckCustom));

  REQUIRE(result.metrics.size() == 9U);
  const auto *frames = find_metric(result.metrics, ztick::example::app_b::kMetricFrames,
                                   ztick_metric_status_measured);
  const auto *blocks = find_metric(result.metrics, ztick::example::app_b::kMetricBlocks,
                                   ztick_metric_status_measured);
  const auto *worker_calls = find_metric(result.metrics, ztick::example::app_b::kMetricWorkerCalls,
                                         ztick_metric_status_measured);
  REQUIRE(frames != nullptr);
  REQUIRE(blocks != nullptr);
  REQUIRE(worker_calls != nullptr);
  CHECK(frames->value == static_cast<double>(result.processed_frames));
  CHECK(blocks->value == static_cast<double>(ztick::example::app_b::kBlockCount));
  CHECK(worker_calls->value == static_cast<double>(ztick::example::app_b::kThreadCount *
                                                   ztick::example::app_b::kBlockCount));
  CHECK(find_metric(result.metrics, ztick::example::app_b::kMetricWorkerSeconds,
                    ztick_metric_status_measured) != nullptr);
  CHECK(find_metric(result.metrics, ztick::example::app_b::kMetricScalingEfficiency,
                    ztick_metric_status_unavailable) != nullptr);
  CHECK(find_metric(result.metrics, ztick::example::app_b::kMetricReferenceFiles,
                    ztick_metric_status_not_applicable) != nullptr);
  CHECK(find_metric(result.metrics, "app_b.overhead_estimate_seconds",
                    ztick_metric_status_estimated) != nullptr);

  const std::string summary = ztick::example::app_b::format_summary(result);
  CHECK(summary.find("app_b ok=1") == 0U);
  CHECK(summary.find("execution=in_process") != std::string::npos);
  CHECK(summary.find("checks=6") != std::string::npos);
  CHECK(summary.find("metrics=9") != std::string::npos);
}

/**
 * @brief Verify the child-mode entry point runs the same lifecycle successfully.
 */
TEST_CASE("dynamic app B child mode completes the engine lifecycle", "[sdk][dynamic][app_b]") {
  CHECK(ztick::example::app_b::run_child_engine(nullptr) == ztick_status_invalid_argument);
  CHECK(ztick::example::app_b::run_child_engine("") == ztick_status_invalid_argument);
  CHECK(ztick::example::app_b::run_child_engine(ZTICK_DYNAMIC_APP_B_PATH) == ztick_status_success);
}

/**
 * @brief Verify malformed descriptor use from the host is refused before execution.
 */
TEST_CASE("dynamic app B adapter rejects invalid lifecycle arguments", "[sdk][dynamic][app_b]") {
  const ztick::plugin::RuntimeLoadResult loaded =
      ztick::plugin::load_dynamic_adapter_runtime(ZTICK_DYNAMIC_APP_B_PATH);
  REQUIRE(loaded.loaded());
  const ztick_adapter_descriptor *descriptor = loaded.adapter.get();
  REQUIRE(descriptor != nullptr);
  CHECK(std::strcmp(descriptor->info.identifier, kExpectedIdentifier) == 0);
  CHECK(descriptor->ops.create != nullptr);
  CHECK(descriptor->ops.destroy != nullptr);
  CHECK(descriptor->ops.describe != nullptr);
  CHECK(descriptor->ops.configure != nullptr);
  CHECK(descriptor->ops.prepare != nullptr);
  CHECK(descriptor->ops.process != nullptr);
  CHECK(descriptor->ops.finish != nullptr);
  CHECK(descriptor->ops.validate != nullptr);
  CHECK(descriptor->ops.report != nullptr);

  const ztick_adapter_ops &ops = descriptor->ops;
  CHECK(ops.create(nullptr) == ztick_status_invalid_argument);
  CHECK(ops.destroy(nullptr) == ztick_status_success);

  ztick_adapter *raw_adapter = nullptr;
  REQUIRE(ops.create(&raw_adapter) == ztick_status_success);
  REQUIRE(raw_adapter != nullptr);
  AdapterGuard guard{&ops, raw_adapter};

  const ztick_audio_format *formats = nullptr;
  const ztick_parameter_descriptor *parameters = nullptr;
  const char *const *unsupported = nullptr;
  std::uint32_t format_count = 0U;
  std::uint32_t parameter_count = 0U;
  std::uint32_t unsupported_count = 0U;
  CHECK(ops.describe(nullptr, &formats, &format_count, &parameters, &parameter_count, &unsupported,
                     &unsupported_count) == ztick_status_invalid_argument);
  CHECK(ops.describe(raw_adapter, &formats, &format_count, &parameters, &parameter_count,
                     &unsupported, &unsupported_count) == ztick_status_success);
  CHECK(format_count == 2U);
  CHECK(parameter_count == 7U);
  CHECK(unsupported_count >= 5U);

  const std::int64_t too_many_threads = 5;
  const std::uint32_t wrong_mix_size = 0U;
  CHECK(ops.configure(raw_adapter, "missing_parameter", &too_many_threads,
                      sizeof(too_many_threads)) == ztick_status_unsupported);
  CHECK(ops.configure(raw_adapter, ztick::example::app_b::kParameterThreads, &too_many_threads,
                      sizeof(too_many_threads)) == ztick_status_unsupported);
  CHECK(ops.configure(raw_adapter, ztick::example::app_b::kParameterMix, &wrong_mix_size,
                      sizeof(wrong_mix_size)) == ztick_status_invalid_argument);
  CHECK(ops.configure(raw_adapter, ztick::example::app_b::kParameterProfile, "unterminated", 11U) ==
        ztick_status_invalid_argument);
  CHECK(ops.prepare(raw_adapter, nullptr, nullptr) == ztick_status_invalid_argument);
  CHECK(ops.process(raw_adapter, nullptr, nullptr, nullptr) == ztick_status_invalid_argument);
  CHECK(ops.finish(raw_adapter) == ztick_status_invalid_argument);
  CHECK(ops.validate(raw_adapter, nullptr, 0U, nullptr, nullptr) == ztick_status_invalid_argument);
  CHECK(ops.report(raw_adapter, nullptr, nullptr) == ztick_status_invalid_argument);
}

/**
 * @brief Verify the harness reports invalid module and child paths explicitly.
 */
TEST_CASE("dynamic app B harness refuses invalid inputs", "[sdk][dynamic][app_b]") {
  const ztick::example::app_b::HarnessResult invalid = ztick::example::app_b::run_harness(
      nullptr, ztick::example::app_b::HarnessIsolation::in_process);
  CHECK_FALSE(invalid.ok);
  CHECK(invalid.error == "module path is required");
  CHECK(ztick::example::app_b::format_summary(invalid).find("error=module path is required") !=
        std::string::npos);

  const ztick::example::app_b::HarnessResult missing_child = ztick::example::app_b::run_harness(
      ZTICK_DYNAMIC_APP_B_PATH, ztick::example::app_b::HarnessIsolation::out_of_process);
  CHECK_FALSE(missing_child.ok);
  CHECK(missing_child.error == "child executable is required for out-of-process mode");
  CHECK(missing_child.trust_authorized);
  CHECK(missing_child.execution_mode == ztick_execution_mode_out_of_process);
}

/**
 * @brief Verify the loaded adapter's planar path, generated references and sink failure.
 */
TEST_CASE("dynamic app B adapter processes planar buffers with generated references",
          "[sdk][dynamic][app_b]") {
  const ztick::plugin::RuntimeLoadResult loaded =
      ztick::plugin::load_dynamic_adapter_runtime(ZTICK_DYNAMIC_APP_B_PATH);
  REQUIRE(loaded.loaded());
  const ztick_adapter_descriptor *descriptor = loaded.adapter.get();
  REQUIRE(descriptor != nullptr);
  const ztick_adapter_ops &ops = descriptor->ops;

  ztick_adapter *raw_adapter = nullptr;
  REQUIRE(ops.create(&raw_adapter) == ztick_status_success);
  REQUIRE(raw_adapter != nullptr);
  AdapterGuard guard{&ops, raw_adapter};

  constexpr std::uint32_t kFrames = ztick::example::app_b::kFramesPerBlock;
  std::array<std::vector<float>, ztick::example::app_b::kChannels> input_planes;
  std::array<std::vector<float>, ztick::example::app_b::kChannels> output_planes;
  for (std::uint32_t channel = 0U; channel < ztick::example::app_b::kChannels; ++channel) {
    input_planes[channel].resize(kFrames);
    output_planes[channel].assign(kFrames, 0.0F);
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
      const double phase =
          0.01 * static_cast<double>(frame + 1U) * static_cast<double>(channel + 1U);
      input_planes[channel][frame] = static_cast<float>(std::sin(phase));
    }
  }
  PlanarStorage input_storage{};
  PlanarStorage output_storage{};
  const ztick_buffer_view input_view = make_planar_view(input_planes, kFrames, input_storage);
  ztick_buffer_view output_view = make_planar_view(output_planes, kFrames, output_storage);
  REQUIRE(ops.prepare(raw_adapter, &input_view, nullptr) == ztick_status_success);

  ztick_timing_sample timing{static_cast<std::uint32_t>(sizeof(ztick_timing_sample)),
                             ZTICK_STRUCT_LAYOUT_VERSION, 0U, 0.0};
  REQUIRE(ops.process(raw_adapter, &input_view, &output_view, &timing) == ztick_status_success);
  CHECK(timing.block_index == 0U);
  CHECK(timing.duration_seconds >= 0.0);
  REQUIRE(ops.finish(raw_adapter) == ztick_status_success);

  constexpr std::size_t kCheckCount = 6U;
  const std::array<const char *, kCheckCount> check_names{
      ztick::example::app_b::kCheckFinite,        ztick::example::app_b::kCheckRange,
      ztick::example::app_b::kCheckReference,     ztick::example::app_b::kCheckExpectedChange,
      ztick::example::app_b::kCheckThreadBalance, ztick::example::app_b::kCheckCustom};
  std::array<ztick_validation_result, kCheckCount> requests{};
  std::array<ztick_validation_result, kCheckCount> results{};
  for (std::size_t index = 0U; index < kCheckCount; ++index) {
    requests[index].size = static_cast<std::uint32_t>(sizeof(requests[index]));
    requests[index].version = ZTICK_STRUCT_LAYOUT_VERSION;
    requests[index].check_name = check_names[index];
    requests[index].absolute_tolerance = ztick::example::app_b::kAbsoluteTolerance;
    requests[index].relative_tolerance = ztick::example::app_b::kRelativeTolerance;
    requests[index].reference_source = "unit_generated_reference";
  }
  std::uint32_t result_count = static_cast<std::uint32_t>(results.size());
  REQUIRE(ops.validate(raw_adapter, requests.data(), static_cast<std::uint32_t>(requests.size()),
                       results.data(), &result_count) == ztick_status_success);
  CHECK(result_count == kCheckCount);
  for (const ztick_validation_result &entry : results) {
    CHECK(entry.outcome == ztick_status_success);
    CHECK(std::strcmp(entry.reference_source, "unit_generated_reference") == 0);
  }

  SinkState state;
  REQUIRE(ops.report(raw_adapter, counting_sink, &state) == ztick_status_success);
  CHECK(state.calls == 9U);
  CHECK(ops.report(raw_adapter, failing_sink, nullptr) == ztick_status_internal_error);
}
