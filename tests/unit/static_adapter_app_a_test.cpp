// SPDX-License-Identifier: MIT
/**
 * @file static_adapter_app_a_test.cpp
 * @brief Deterministic unit and integration tests for static test app A.
 *
 * The tests cover the complete C ABI lifecycle, typed parameter edge cases,
 * independent instance state, float64/planar buffers, report sink failures and
 * the executable host harness's end-to-end result. Expected values are computed
 * from the written App A contract rather than from adapter internals.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <app_a_contract.hpp>
#include <app_a_harness.hpp>
#include <static_adapter.hpp>

#include <ztick/sdk.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

/** @brief Read-only descriptor published by the static App A adapter. */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor;

namespace {

/** @brief Stable identifier published by the static App A adapter. */
constexpr const char kAdapterId[] = "ztick.test.static.app_a";

/** @brief Plane pointer storage for an interleaved float32 test view. */
struct InterleavedStorage {
  /** @brief Single plane pointer referenced by the buffer view. */
  void *planes[1]{nullptr};
};

/** @brief Plane pointer storage for a planar float64 test view. */
struct PlanarStorage {
  /** @brief Two plane pointers referenced by the buffer view. */
  void *planes[2]{nullptr};
};

/** @brief Owning context for metrics received from the report callback. */
struct MetricCollector {
  /** @brief Metrics retained in callback order. */
  std::vector<ztick::example::app_a::MetricReport> metrics;
};

/**
 * @brief Build an interleaved float32 view over caller-owned storage.
 * @param samples Sample vector to expose.
 * @param frames Frame count represented by the view.
 * @param storage Plane pointer storage that outlives the returned view.
 * @return Initialized C ABI buffer view.
 */
auto make_float_view(std::vector<float> &samples, std::uint32_t frames,
                     InterleavedStorage &storage) noexcept -> ztick_buffer_view {
  storage.planes[0] = samples.data();
  const ztick_audio_format format{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                  ZTICK_STRUCT_LAYOUT_VERSION,
                                  ztick_sample_format_float32,
                                  ztick_channel_layout_interleaved,
                                  ztick::example::app_a::kSampleRateHz,
                                  ztick::example::app_a::kChannels,
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
 * @brief Build a planar float64 view over two caller-owned channel vectors.
 * @param left First channel samples.
 * @param right Second channel samples.
 * @param frames Frame count represented by the view.
 * @param storage Plane pointer storage that outlives the returned view.
 * @return Initialized C ABI buffer view.
 */
auto make_planar_view(std::vector<double> &left, std::vector<double> &right, std::uint32_t frames,
                      PlanarStorage &storage) noexcept -> ztick_buffer_view {
  storage.planes[0] = left.data();
  storage.planes[1] = right.data();
  const ztick_audio_format format{static_cast<std::uint32_t>(sizeof(ztick_audio_format)),
                                  ZTICK_STRUCT_LAYOUT_VERSION,
                                  ztick_sample_format_float64,
                                  ztick_channel_layout_planar,
                                  ztick::example::app_a::kSampleRateHz,
                                  ztick::example::app_a::kChannels,
                                  frames};
  return ztick_buffer_view{static_cast<std::uint32_t>(sizeof(ztick_buffer_view)),
                           ZTICK_STRUCT_LAYOUT_VERSION,
                           storage.planes,
                           nullptr,
                           2U,
                           frames,
                           format};
}

/**
 * @brief Retain a reported metric in a test-owned collector.
 * @param context Pointer to @ref MetricCollector; must not be NULL.
 * @param name Stable metric name.
 * @param value Reported value.
 * @param unit Reported unit, or NULL.
 * @param status Reported provenance label.
 * @param source Reported collection method, or NULL.
 * @return `ztick_status_success` when the metric was retained.
 */
auto collect_metric(void *context, const char *name, double value, const char *unit,
                    ztick_metric_status status, const char *source) -> std::int32_t {
  if (context == nullptr || name == nullptr) {
    return ztick_status_invalid_argument;
  }
  auto &collector = *static_cast<MetricCollector *>(context);
  collector.metrics.push_back(
      ztick::example::app_a::MetricReport{name, value, unit == nullptr ? std::string{} : unit,
                                          status, source == nullptr ? std::string{} : source});
  return ztick_status_success;
}

/**
 * @brief Reject every metric in a report to exercise sink-failure propagation.
 * @return Always `ztick_status_internal_error`.
 */
auto reject_metric(void *, const char *, double, const char *, ztick_metric_status, const char *)
    -> std::int32_t {
  return ztick_status_internal_error;
}

/**
 * @brief Find one metric by name and provenance label.
 * @param metrics Metrics to search.
 * @param name Stable metric name.
 * @param status Required provenance label.
 * @return Pointer to the retained metric, or NULL when absent.
 */
auto find_metric(const std::vector<ztick::example::app_a::MetricReport> &metrics, const char *name,
                 ztick_metric_status status) noexcept
    -> const ztick::example::app_a::MetricReport * {
  const auto found =
      std::find_if(metrics.begin(), metrics.end(), [name, status](const auto &metric) noexcept {
        return metric.name == name && metric.status == status;
      });
  return found == metrics.end() ? nullptr : &*found;
}

/**
 * @brief Verify that one workload result has the expected contract counts.
 * @param result Workload result to inspect.
 * @param kind Expected workload variant.
 */
void check_workload_result(const ztick::example::app_a::WorkloadResult &result,
                           ztick::example::app_a::WorkloadKind kind) {
  using namespace ztick::example::app_a;
  CHECK(result.kind == kind);
  CHECK(result.ok);
  CHECK(result.error.empty());
  CHECK(result.block_count == kBlockCount);
  CHECK(result.processed_frames == kBlockCount * kFramesPerBlock);
  CHECK(result.validation_check_count == 5U);
  CHECK(result.validation_results.size() == 5U);
  CHECK(result.metrics.size() == 6U);
  CHECK(result.whole_run_seconds >= 0.0);
  CHECK(result.per_block_seconds_sum >= 0.0);

  const auto *frames = find_metric(result.metrics, kMetricFrames, ztick_metric_status_measured);
  const auto *blocks = find_metric(result.metrics, kMetricBlocks, ztick_metric_status_measured);
  REQUIRE(frames != nullptr);
  REQUIRE(blocks != nullptr);
  CHECK(frames->value == static_cast<double>(result.processed_frames));
  CHECK(blocks->value == static_cast<double>(result.block_count));
  CHECK(find_metric(result.metrics, kMetricProcessingSeconds, ztick_metric_status_measured) !=
        nullptr);
  CHECK(find_metric(result.metrics, kMetricOutputPeak, ztick_metric_status_measured) != nullptr);
  CHECK(find_metric(result.metrics, kMetricOverheadSeconds, ztick_metric_status_unavailable) !=
        nullptr);
  CHECK(find_metric(result.metrics, kMetricScalingEfficiency, ztick_metric_status_not_applicable) !=
        nullptr);

  REQUIRE(result.validation_results.size() == 5U);
  CHECK(result.validation_results[0U].check_name == kCheckFinite);
  CHECK(result.validation_results[1U].check_name == kCheckRange);
  CHECK(result.validation_results[2U].check_name == kCheckReference);
  CHECK(result.validation_results[2U].reference_source == "deterministic_generation");
  CHECK(result.validation_results[3U].check_name == kCheckExpectedChange);
  CHECK(result.validation_results[4U].check_name == kCheckCustom);
  for (const auto &entry : result.validation_results) {
    CHECK(entry.status == ztick_status_success);
  }
}

} // namespace

/**
 * @brief Verify that the static adapter publishes a complete negotiable contract.
 */
TEST_CASE("static app A descriptor is complete", "[sdk][static][app_a]") {
  const ztick_adapter_descriptor &descriptor = ztick_test_app_static_descriptor;
  CHECK(descriptor.size >= sizeof(descriptor));
  CHECK(descriptor.version == ZTICK_STRUCT_LAYOUT_VERSION);
  CHECK(std::strcmp(descriptor.info.identifier, kAdapterId) == 0);
  CHECK(descriptor.info.name != nullptr);
  CHECK(descriptor.info.metadata != nullptr);
  CHECK(ztick_sdk_is_compatible(&descriptor.info.sdk_version) == ztick_status_success);

  const std::uint64_t unknown = descriptor.info.capabilities & ~ZTICK_CAPABILITY_KNOWN_MASK;
  const std::uint64_t missing =
      ztick::example::app_a::kRequiredCapabilities & ~descriptor.info.capabilities;
  CHECK(unknown == 0U);
  CHECK(missing == 0U);
  CHECK((descriptor.info.capabilities & ZTICK_CAPABILITY_PROCESSOR) != 0U);
  CHECK((descriptor.info.capabilities & ZTICK_CAPABILITY_CHAIN) != 0U);
  CHECK((descriptor.info.capabilities & ZTICK_CAPABILITY_PER_BLOCK_TIMING) != 0U);
  CHECK((descriptor.info.capabilities & ZTICK_CAPABILITY_CUSTOM_VALIDATION) != 0U);
  CHECK((descriptor.info.capabilities & ZTICK_CAPABILITY_REFERENCE_DATA) == 0U);
  CHECK((descriptor.info.capabilities & ZTICK_CAPABILITY_ENGINE) == 0U);
}

/**
 * @brief Verify that the App A host harness runs both workloads end to end.
 */
TEST_CASE("static app A harness runs end to end", "[sdk][static][app_a][integration]") {
  using namespace ztick::example::app_a;
  const HarnessResult result = run_harness();
  REQUIRE(result.ok);
  CHECK(result.error.empty());
  CHECK(result.sdk_version_status == ztick_status_success);
  CHECK((result.registration_status == ztick_status_success ||
         result.registration_status == ztick_status_invalid_argument));
  CHECK(result.execution_mode == ztick_execution_mode_in_process);
  CHECK((result.required_capabilities & ~ZTICK_CAPABILITY_KNOWN_MASK) == 0U);
  CHECK(result.format_count >= 4U);
  CHECK(result.parameter_count >= 6U);
  CHECK(result.unsupported_count >= 4U);

  REQUIRE(result.workloads.size() == 2U);
  check_workload_result(result.workloads[0U], WorkloadKind::processor);
  check_workload_result(result.workloads[1U], WorkloadKind::chain);

  CHECK(result.block_count == 2U * kBlockCount);
  CHECK(result.processed_frames == 2U * kBlockCount * kFramesPerBlock);
  CHECK(result.validation_check_count == 10U);
  CHECK(result.metrics.size() == 12U);
  CHECK(result.validation_results.size() == 10U);
  CHECK(result.whole_run_seconds >= 0.0);
  CHECK(result.per_block_seconds_sum >= 0.0);

  const std::string summary = format_summary(result);
  CHECK(summary.find("app_a ok=1") == 0U);
  CHECK(summary.find("workloads=2") != std::string::npos);
  CHECK(summary.find("blocks=16") != std::string::npos);
  CHECK(summary.find("error=") == std::string::npos);
}

/**
 * @brief Verify lifecycle error paths and typed parameter rejection.
 */
TEST_CASE("static app A lifecycle rejects malformed calls", "[sdk][static][app_a]") {
  using namespace ztick::example::app_a;
  const ztick_adapter_ops &ops = ztick_test_app_static_descriptor.ops;

  CHECK(ops.create(nullptr) == ztick_status_invalid_argument);
  ztick_adapter *adapter = nullptr;
  REQUIRE(ops.create(&adapter) == ztick_status_success);
  REQUIRE(adapter != nullptr);

  const ztick_audio_format *formats = nullptr;
  const ztick_parameter_descriptor *parameters = nullptr;
  const char *const *unsupported = nullptr;
  std::uint32_t format_count = 0U;
  std::uint32_t parameter_count = 0U;
  std::uint32_t unsupported_count = 0U;
  CHECK(ops.describe(nullptr, &formats, &format_count, &parameters, &parameter_count, &unsupported,
                     &unsupported_count) == ztick_status_invalid_argument);
  CHECK(ops.describe(adapter, nullptr, &format_count, &parameters, &parameter_count, &unsupported,
                     &unsupported_count) == ztick_status_invalid_argument);
  CHECK(ops.describe(adapter, &formats, &format_count, &parameters, &parameter_count, &unsupported,
                     &unsupported_count) == ztick_status_success);
  CHECK(formats != nullptr);
  CHECK(format_count >= 4U);
  CHECK(parameters != nullptr);
  CHECK(parameter_count >= 6U);
  CHECK(unsupported != nullptr);
  CHECK(unsupported_count >= 4U);

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double bad_gain = 5.0;
  const std::int64_t bad_stages = 3;
  const std::uint32_t bad_bool = 2U;
  const std::uint32_t bad_selection = 9U;
  const std::uint32_t wrong_size = 1U;
  const char unknown_name[] = "unknown_parameter";
  const char unterminated[] = {'a', 'b'};
  CHECK(ops.configure(adapter, nullptr, &bad_gain, sizeof(bad_gain)) ==
        ztick_status_invalid_argument);
  CHECK(ops.configure(adapter, kParameterGain, nullptr, sizeof(bad_gain)) ==
        ztick_status_invalid_argument);
  CHECK(ops.configure(adapter, kParameterGain, &nan, sizeof(nan)) == ztick_status_unsupported);
  CHECK(ops.configure(adapter, kParameterGain, &bad_gain, sizeof(bad_gain)) ==
        ztick_status_unsupported);
  CHECK(ops.configure(adapter, kParameterStages, &bad_stages, sizeof(bad_stages)) ==
        ztick_status_unsupported);
  CHECK(ops.configure(adapter, kParameterSmoothing, &bad_bool, sizeof(bad_bool)) ==
        ztick_status_unsupported);
  CHECK(ops.configure(adapter, kParameterWorkload, &bad_selection, sizeof(bad_selection)) ==
        ztick_status_unsupported);
  CHECK(ops.configure(adapter, kParameterGain, &wrong_size, sizeof(wrong_size)) ==
        ztick_status_invalid_argument);
  CHECK(ops.configure(adapter, kParameterLabel, unterminated, sizeof(unterminated)) ==
        ztick_status_invalid_argument);
  CHECK(ops.configure(adapter, unknown_name, &bad_gain, sizeof(bad_gain)) ==
        ztick_status_unsupported);

  std::vector<float> samples(static_cast<std::size_t>(kFramesPerBlock) * kChannels, 0.25F);
  InterleavedStorage storage{};
  const ztick_buffer_view view = make_float_view(samples, kFramesPerBlock, storage);
  CHECK(ops.prepare(nullptr, &view, nullptr) == ztick_status_invalid_argument);
  CHECK(ops.prepare(adapter, nullptr, nullptr) == ztick_status_invalid_argument);
  ztick_buffer_view malformed = view;
  malformed.size = 0U;
  CHECK(ops.prepare(adapter, &malformed, nullptr) == ztick_status_invalid_argument);

  ztick_buffer_view output_view = view;
  CHECK(ops.process(adapter, &view, &output_view, nullptr) == ztick_status_invalid_argument);
  CHECK(ops.finish(adapter) == ztick_status_invalid_argument);
  CHECK(ops.report(adapter, collect_metric, nullptr) == ztick_status_invalid_argument);

  REQUIRE(ops.prepare(adapter, &view, nullptr) == ztick_status_success);
  REQUIRE(ops.process(adapter, &view, &output_view, nullptr) == ztick_status_success);
  REQUIRE(ops.finish(adapter) == ztick_status_success);

  ztick_validation_result request{};
  request.size = static_cast<std::uint32_t>(sizeof(request));
  request.version = ZTICK_STRUCT_LAYOUT_VERSION;
  request.check_name = kCheckFinite;
  ztick_validation_result result{};
  std::uint32_t capacity = 1U;
  CHECK(ops.validate(nullptr, &request, 1U, &result, &capacity) == ztick_status_invalid_argument);
  CHECK(ops.validate(adapter, &request, 1U, nullptr, &capacity) == ztick_status_invalid_argument);
  CHECK(ops.validate(adapter, &request, 1U, &result, nullptr) == ztick_status_invalid_argument);
  capacity = 7U;
  CHECK(ops.validate(adapter, nullptr, 0U, &result, &capacity) == ztick_status_success);
  CHECK(capacity == 0U);

  CHECK(ops.destroy(nullptr) == ztick_status_success);
  CHECK(ops.destroy(adapter) == ztick_status_success);
}

/**
 * @brief Verify that separate adapter instances retain independent configuration.
 */
TEST_CASE("static app A instances keep independent state", "[sdk][static][app_a]") {
  using namespace ztick::example::app_a;
  const ztick_adapter_ops &ops = ztick_test_app_static_descriptor.ops;

  ztick_adapter *first = nullptr;
  ztick_adapter *second = nullptr;
  REQUIRE(ops.create(&first) == ztick_status_success);
  REQUIRE(ops.create(&second) == ztick_status_success);

  const double first_gain = 1.0;
  const double second_gain = 2.0;
  CHECK(ops.configure(first, kParameterGain, &first_gain, sizeof(first_gain)) ==
        ztick_status_success);
  CHECK(ops.configure(second, kParameterGain, &second_gain, sizeof(second_gain)) ==
        ztick_status_success);

  std::vector<float> input(static_cast<std::size_t>(kFramesPerBlock) * kChannels, 0.4F);
  std::vector<float> first_output(input.size(), 0.0F);
  std::vector<float> second_output(input.size(), 0.0F);
  InterleavedStorage input_storage{};
  InterleavedStorage first_output_storage{};
  InterleavedStorage second_output_storage{};
  const ztick_buffer_view input_view = make_float_view(input, kFramesPerBlock, input_storage);
  ztick_buffer_view first_output_view =
      make_float_view(first_output, kFramesPerBlock, first_output_storage);
  ztick_buffer_view second_output_view =
      make_float_view(second_output, kFramesPerBlock, second_output_storage);

  REQUIRE(ops.prepare(first, &input_view, nullptr) == ztick_status_success);
  REQUIRE(ops.prepare(second, &input_view, nullptr) == ztick_status_success);
  REQUIRE(ops.process(first, &input_view, &first_output_view, nullptr) == ztick_status_success);
  REQUIRE(ops.process(second, &input_view, &second_output_view, nullptr) == ztick_status_success);
  REQUIRE(ops.finish(first) == ztick_status_success);
  REQUIRE(ops.finish(second) == ztick_status_success);

  MetricCollector first_metrics;
  MetricCollector second_metrics;
  REQUIRE(ops.report(first, collect_metric, &first_metrics) == ztick_status_success);
  REQUIRE(ops.report(second, collect_metric, &second_metrics) == ztick_status_success);
  const auto *first_peak =
      find_metric(first_metrics.metrics, kMetricOutputPeak, ztick_metric_status_measured);
  const auto *second_peak =
      find_metric(second_metrics.metrics, kMetricOutputPeak, ztick_metric_status_measured);
  REQUIRE(first_peak != nullptr);
  REQUIRE(second_peak != nullptr);
  CHECK(first_peak->value == Catch::Approx(0.4).margin(1e-6));
  CHECK(second_peak->value == Catch::Approx(0.8).margin(1e-6));

  CHECK(ops.destroy(first) == ztick_status_success);
  CHECK(ops.destroy(second) == ztick_status_success);
}

/**
 * @brief Verify that a planar float64 block follows the declared data contract.
 */
TEST_CASE("static app A processes planar float64 blocks", "[sdk][static][app_a]") {
  using namespace ztick::example::app_a;
  const ztick_adapter_ops &ops = ztick_test_app_static_descriptor.ops;
  ztick_adapter *adapter = nullptr;
  REQUIRE(ops.create(&adapter) == ztick_status_success);

  std::vector<double> left(kFramesPerBlock, 0.5);
  std::vector<double> right(kFramesPerBlock, -0.25);
  std::vector<double> out_left(kFramesPerBlock, 0.0);
  std::vector<double> out_right(kFramesPerBlock, 0.0);
  PlanarStorage input_storage{};
  PlanarStorage output_storage{};
  const ztick_buffer_view input = make_planar_view(left, right, kFramesPerBlock, input_storage);
  ztick_buffer_view output = make_planar_view(out_left, out_right, kFramesPerBlock, output_storage);
  REQUIRE(ops.prepare(adapter, &input, nullptr) == ztick_status_success);
  REQUIRE(ops.process(adapter, &input, &output, nullptr) == ztick_status_success);
  REQUIRE(ops.finish(adapter) == ztick_status_success);
  CHECK(out_left.front() == Catch::Approx(0.5).margin(1e-9));
  CHECK(out_right.front() == Catch::Approx(-0.25).margin(1e-9));

  MetricCollector metrics;
  REQUIRE(ops.report(adapter, collect_metric, &metrics) == ztick_status_success);
  const auto *peak = find_metric(metrics.metrics, kMetricOutputPeak, ztick_metric_status_measured);
  REQUIRE(peak != nullptr);
  CHECK(peak->value == Catch::Approx(0.5).margin(1e-9));

  CHECK(ops.destroy(adapter) == ztick_status_success);
}

/**
 * @brief Verify that a failing metric sink aborts the report operation.
 */
TEST_CASE("static app A propagates report sink failure", "[sdk][static][app_a]") {
  using namespace ztick::example::app_a;
  const ztick_adapter_ops &ops = ztick_test_app_static_descriptor.ops;
  ztick_adapter *adapter = nullptr;
  REQUIRE(ops.create(&adapter) == ztick_status_success);

  std::vector<float> samples(static_cast<std::size_t>(kFramesPerBlock) * kChannels, 0.5F);
  std::vector<float> output(samples.size(), 0.0F);
  InterleavedStorage input_storage{};
  InterleavedStorage output_storage{};
  const ztick_buffer_view input_view = make_float_view(samples, kFramesPerBlock, input_storage);
  ztick_buffer_view output_view = make_float_view(output, kFramesPerBlock, output_storage);
  REQUIRE(ops.prepare(adapter, &input_view, nullptr) == ztick_status_success);
  REQUIRE(ops.process(adapter, &input_view, &output_view, nullptr) == ztick_status_success);
  REQUIRE(ops.finish(adapter) == ztick_status_success);
  CHECK(ops.report(adapter, reject_metric, nullptr) == ztick_status_internal_error);
  CHECK(ops.destroy(adapter) == ztick_status_success);
}

/**
 * @brief Verify that a failed harness result renders a diagnostic summary.
 */
TEST_CASE("static app A summary reports failure", "[sdk][static][app_a]") {
  ztick::example::app_a::HarnessResult result;
  result.error = "injected_failure";
  result.workloads.push_back(ztick::example::app_a::WorkloadResult{});

  const std::string summary = ztick::example::app_a::format_summary(result);
  CHECK(summary.find("app_a ok=0") == 0U);
  CHECK(summary.find("workloads=1") != std::string::npos);
  CHECK(summary.find("error=injected_failure") != std::string::npos);
}
