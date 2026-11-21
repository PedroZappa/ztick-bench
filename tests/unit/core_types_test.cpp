// SPDX-License-Identifier: MIT
/**
 * @file core_types_test.cpp
 * @brief Catch2 unit tests for frozen core type values, layout and versions.
 *
 * Expected values are written as literals from SDK_SPECIFICATION §1.1 and §1.2
 * rather than derived from the header under test, so a changed ABI constant
 * fails this suite. The standalone header checks remain responsible for proving
 * self-contained C11/C++20 inclusion; these tests assert the frozen contract.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/cpp/adapter.hpp>
#include <ztick/types.h>
#include <ztick/version.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/** @brief Internal helpers for the frozen core type tests. */
namespace {

/**
 * @brief Report whether an array contains no duplicate values.
 * @tparam T Array element type supporting equality comparison.
 * @tparam N Number of array elements.
 * @param values Values to inspect.
 * @return True when every pair of elements differs.
 */
template <typename T, std::size_t N> bool all_distinct(const std::array<T, N> &values) {
  for (std::size_t i = 0; i < N; ++i) {
    for (std::size_t j = i + 1; j < N; ++j) {
      if (values[i] == values[j]) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

/**
 * @brief Verify the SDK ABI version macros and their C++/C struct projections.
 * @note Values are frozen by BR §22 and SDK_SPECIFICATION §1.1.
 */
TEST_CASE("SDK ABI version constants are frozen", "[sdk][version]") {
  STATIC_REQUIRE(ZTICK_SDK_VERSION_MAJOR == 1);
  STATIC_REQUIRE(ZTICK_SDK_VERSION_MINOR == 0);
  STATIC_REQUIRE(ZTICK_SDK_VERSION_PATCH == 0);

  REQUIRE(ZTICK_SDK_VERSION_MAJOR == 1);
  REQUIRE(ZTICK_SDK_VERSION_MINOR == 0);
  REQUIRE(ZTICK_SDK_VERSION_PATCH == 0);

  constexpr ztick_version abi{sizeof(ztick_version), 1, 1, 0, 0};
  constexpr ztick::sdk::Version wrapped{abi};
  STATIC_REQUIRE(wrapped.major == 1);
  STATIC_REQUIRE(wrapped.minor == 0);
  STATIC_REQUIRE(wrapped.patch == 0);
  STATIC_REQUIRE(ztick::sdk::kSdkVersion.major == 1);
  STATIC_REQUIRE(ztick::sdk::kSdkVersion.minor == 0);
  STATIC_REQUIRE(ztick::sdk::kSdkVersion.patch == 0);
}

/**
 * @brief Verify all seven C ABI status codes and their distinctness.
 * @note Numeric values are frozen by SDK_SPECIFICATION §1.2 and §8.
 */
TEST_CASE("ztick_status codes are frozen and distinct", "[sdk][types][status]") {
  STATIC_REQUIRE(ztick_status_success == 0);
  STATIC_REQUIRE(ztick_status_unsupported == 1);
  STATIC_REQUIRE(ztick_status_incompatible == 2);
  STATIC_REQUIRE(ztick_status_invalid_argument == 3);
  STATIC_REQUIRE(ztick_status_processing_failure == 4);
  STATIC_REQUIRE(ztick_status_validation_failure == 5);
  STATIC_REQUIRE(ztick_status_internal_error == 6);

  const std::array<int, 7> actual{ztick_status_success,
                                  ztick_status_unsupported,
                                  ztick_status_incompatible,
                                  ztick_status_invalid_argument,
                                  ztick_status_processing_failure,
                                  ztick_status_validation_failure,
                                  ztick_status_internal_error};
  const std::array<int, 7> expected{0, 1, 2, 3, 4, 5, 6};
  REQUIRE(actual == expected);
  REQUIRE(all_distinct(actual));
}

/**
 * @brief Verify availability states and metric value status labels.
 * @note Both enums are frozen; a not-applicable value is never an omission.
 */
TEST_CASE("availability and metric status codes are frozen", "[sdk][types][status]") {
  STATIC_REQUIRE(ztick_availability_value == 0);
  STATIC_REQUIRE(ztick_availability_unavailable == 1);
  STATIC_REQUIRE(ztick_availability_unsupported == 2);
  STATIC_REQUIRE(ztick_availability_not_applicable == 3);
  STATIC_REQUIRE(ztick_metric_status_measured == 0);
  STATIC_REQUIRE(ztick_metric_status_estimated == 1);
  STATIC_REQUIRE(ztick_metric_status_unavailable == 2);
  STATIC_REQUIRE(ztick_metric_status_not_applicable == 3);

  const std::array<int, 4> availability{ztick_availability_value, ztick_availability_unavailable,
                                        ztick_availability_unsupported,
                                        ztick_availability_not_applicable};
  const std::array<int, 4> availability_expected{0, 1, 2, 3};
  REQUIRE(availability == availability_expected);
  REQUIRE(all_distinct(availability));

  const std::array<int, 4> metric_status{
      ztick_metric_status_measured, ztick_metric_status_estimated, ztick_metric_status_unavailable,
      ztick_metric_status_not_applicable};
  const std::array<int, 4> metric_expected{0, 1, 2, 3};
  REQUIRE(metric_status == metric_expected);
  REQUIRE(all_distinct(metric_status));
}

/**
 * @brief Verify format, layout, execution mode and parameter type codes.
 * @note `float32` is mandatory and every listed value has an explicit ABI code.
 */
TEST_CASE("format layout mode and parameter codes are frozen", "[sdk][types]") {
  STATIC_REQUIRE(ztick_sample_format_float32 == 0);
  STATIC_REQUIRE(ztick_sample_format_float64 == 1);
  STATIC_REQUIRE(ztick_channel_layout_interleaved == 0);
  STATIC_REQUIRE(ztick_channel_layout_planar == 1);
  STATIC_REQUIRE(ztick_execution_mode_in_process == 0);
  STATIC_REQUIRE(ztick_execution_mode_out_of_process == 1);
  STATIC_REQUIRE(ztick_parameter_type_boolean == 0);
  STATIC_REQUIRE(ztick_parameter_type_integer == 1);
  STATIC_REQUIRE(ztick_parameter_type_floating_point == 2);
  STATIC_REQUIRE(ztick_parameter_type_enumeration == 3);
  STATIC_REQUIRE(ztick_parameter_type_string == 4);

  const std::array<int, 2> formats{ztick_sample_format_float32, ztick_sample_format_float64};
  const std::array<int, 2> layouts{ztick_channel_layout_interleaved, ztick_channel_layout_planar};
  const std::array<int, 2> modes{ztick_execution_mode_in_process,
                                 ztick_execution_mode_out_of_process};
  const std::array<int, 5> parameter_types{
      ztick_parameter_type_boolean, ztick_parameter_type_integer,
      ztick_parameter_type_floating_point, ztick_parameter_type_enumeration,
      ztick_parameter_type_string};
  const std::array<int, 2> binary_expected{0, 1};
  const std::array<int, 5> parameter_expected{0, 1, 2, 3, 4};
  REQUIRE(formats == binary_expected);
  REQUIRE(layouts == binary_expected);
  REQUIRE(modes == binary_expected);
  REQUIRE(parameter_types == parameter_expected);
  REQUIRE(all_distinct(formats));
  REQUIRE(all_distinct(layouts));
  REQUIRE(all_distinct(modes));
  REQUIRE(all_distinct(parameter_types));
}

/**
 * @brief Verify every public struct begins with 32-bit `size` and `version`.
 * @note This prefix is the struct-evolution contract in SDK_SPECIFICATION §1.1.
 */
TEST_CASE("public structs keep the evolution prefix", "[sdk][types][layout]") {
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_version>);
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_audio_format>);
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_parameter_descriptor>);
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_adapter_info>);
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_buffer_view>);
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_timing_sample>);
  STATIC_REQUIRE(std::is_standard_layout_v<ztick_validation_result>);

  STATIC_REQUIRE(offsetof(ztick_version, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_version, version) == sizeof(std::uint32_t));
  STATIC_REQUIRE(offsetof(ztick_audio_format, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_audio_format, version) == sizeof(std::uint32_t));
  STATIC_REQUIRE(offsetof(ztick_parameter_descriptor, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_parameter_descriptor, version) == sizeof(std::uint32_t));
  STATIC_REQUIRE(offsetof(ztick_adapter_info, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_adapter_info, version) == sizeof(std::uint32_t));
  STATIC_REQUIRE(offsetof(ztick_buffer_view, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_buffer_view, version) == sizeof(std::uint32_t));
  STATIC_REQUIRE(offsetof(ztick_timing_sample, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_timing_sample, version) == sizeof(std::uint32_t));
  STATIC_REQUIRE(offsetof(ztick_validation_result, size) == 0);
  STATIC_REQUIRE(offsetof(ztick_validation_result, version) == sizeof(std::uint32_t));
}

/**
 * @brief Verify every remaining inventory field keeps its declared type.
 * @note Unknown capability bits and parameter types remain unsupported rather
 *       than being assumed valid.
 */
TEST_CASE("public struct fields keep their declared types", "[sdk][types][layout]") {
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_version::size), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_version::version), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_version::major), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_version::minor), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_version::patch), std::uint32_t>);

  STATIC_REQUIRE(std::is_same_v<decltype(ztick_audio_format::format), ztick_sample_format>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_audio_format::layout), ztick_channel_layout>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_audio_format::sample_rate), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_audio_format::channels), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_audio_format::frames), std::uint32_t>);

  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::name), const char *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::type), ztick_parameter_type>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::units), const char *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::minimum), double>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::maximum), double>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::default_value), double>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(ztick_parameter_descriptor::allowed_values), const char *const *>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(ztick_parameter_descriptor::allowed_value_count), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_parameter_descriptor::description), const char *>);

  STATIC_REQUIRE(std::is_same_v<decltype(ztick_adapter_info::identifier), const char *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_adapter_info::name), const char *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_adapter_info::adapter_version), ztick_version>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_adapter_info::sdk_version), ztick_version>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_adapter_info::capabilities), std::uint64_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_adapter_info::metadata), const char *>);

  STATIC_REQUIRE(std::is_same_v<decltype(ztick_buffer_view::planes), void *const *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_buffer_view::strides), const std::size_t *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_buffer_view::plane_count), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_buffer_view::frames), std::uint32_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_buffer_view::format), ztick_audio_format>);

  STATIC_REQUIRE(std::is_same_v<decltype(ztick_timing_sample::block_index), std::uint64_t>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_timing_sample::duration_seconds), double>);

  STATIC_REQUIRE(std::is_same_v<decltype(ztick_validation_result::check_name), const char *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_validation_result::outcome), ztick_status>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_validation_result::absolute_tolerance), double>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_validation_result::relative_tolerance), double>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_validation_result::reference_source), const char *>);
  STATIC_REQUIRE(std::is_same_v<decltype(ztick_validation_result::details), const char *>);
}
