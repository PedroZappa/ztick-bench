// SPDX-License-Identifier: MIT
/**
 * @file types_header_cpp20_test.cpp
 * @brief Standalone C++20 compile check and inventory assertions for types.h.
 *
 * The public header is included first and on its own, so a successful build
 * proves it is self-contained C++20. The checks lock the frozen type inventory
 * (SDK_SPECIFICATION §1.2), explicit enum values and the leading `size` and
 * `version` fields required for struct evolution (SDK_SPECIFICATION §1.1).
 * Every check is evaluated at compile time.
 */

#include <ztick/types.h>

#include <cstddef>
#include <type_traits>

// The build selects C++20 with extensions disabled; verify the effective mode.
static_assert(__cplusplus >= 202002L, "types.h test requires C++20");

// Explicit status codes (SDK_SPECIFICATION §8).
static_assert(ztick_status_success == 0, "unexpected success status code");
static_assert(ztick_status_unsupported == 1, "unexpected unsupported status code");
static_assert(ztick_status_incompatible == 2, "unexpected incompatible status code");
static_assert(ztick_status_invalid_argument == 3, "unexpected invalid argument status code");
static_assert(ztick_status_processing_failure == 4, "unexpected processing failure status code");
static_assert(ztick_status_validation_failure == 5, "unexpected validation failure status code");
static_assert(ztick_status_internal_error == 6, "unexpected internal error status code");

// Explicit format, layout, status and mode codes.
static_assert(ztick_sample_format_float32 == 0, "unexpected float32 code");
static_assert(ztick_sample_format_float64 == 1, "unexpected float64 code");
static_assert(ztick_channel_layout_interleaved == 0, "unexpected interleaved code");
static_assert(ztick_channel_layout_planar == 1, "unexpected planar code");
static_assert(ztick_metric_status_measured == 0, "unexpected measured code");
static_assert(ztick_metric_status_estimated == 1, "unexpected estimated code");
static_assert(ztick_metric_status_unavailable == 2, "unexpected unavailable code");
static_assert(ztick_metric_status_not_applicable == 3, "unexpected not_applicable code");
static_assert(ztick_availability_value == 0, "unexpected value availability code");
static_assert(ztick_availability_unavailable == 1, "unexpected unavailable availability code");
static_assert(ztick_availability_unsupported == 2, "unexpected unsupported availability code");
static_assert(ztick_availability_not_applicable == 3,
              "unexpected not_applicable availability code");
static_assert(ztick_execution_mode_in_process == 0, "unexpected in_process code");
static_assert(ztick_execution_mode_out_of_process == 1, "unexpected out_of_process code");
static_assert(ztick_parameter_type_boolean == 0, "unexpected boolean parameter code");
static_assert(ztick_parameter_type_integer == 1, "unexpected integer parameter code");
static_assert(ztick_parameter_type_floating_point == 2, "unexpected floating-point parameter code");
static_assert(ztick_parameter_type_enumeration == 3, "unexpected enumeration parameter code");
static_assert(ztick_parameter_type_string == 4, "unexpected string parameter code");

// Every public struct begins with size and version.
static_assert(offsetof(ztick_version, size) == 0, "ztick_version::size offset");
static_assert(offsetof(ztick_version, version) == sizeof(uint32_t),
              "ztick_version::version offset");
static_assert(sizeof(((ztick_version *)0)->version) == sizeof(uint32_t),
              "ztick_version::version type");
static_assert(offsetof(ztick_audio_format, size) == 0, "ztick_audio_format::size offset");
static_assert(offsetof(ztick_audio_format, version) == sizeof(uint32_t),
              "ztick_audio_format::version offset");
static_assert(sizeof(((ztick_audio_format *)0)->version) == sizeof(uint32_t),
              "ztick_audio_format::version type");
static_assert(offsetof(ztick_parameter_descriptor, size) == 0,
              "ztick_parameter_descriptor::size offset");
static_assert(offsetof(ztick_parameter_descriptor, version) == sizeof(uint32_t),
              "ztick_parameter_descriptor::version offset");
static_assert(sizeof(((ztick_parameter_descriptor *)0)->version) == sizeof(uint32_t),
              "ztick_parameter_descriptor::version type");
static_assert(offsetof(ztick_adapter_info, size) == 0, "ztick_adapter_info::size offset");
static_assert(offsetof(ztick_adapter_info, version) == sizeof(uint32_t),
              "ztick_adapter_info::version offset");
static_assert(sizeof(((ztick_adapter_info *)0)->version) == sizeof(uint32_t),
              "ztick_adapter_info::version type");
static_assert(offsetof(ztick_buffer_view, size) == 0, "ztick_buffer_view::size offset");
static_assert(offsetof(ztick_buffer_view, version) == sizeof(uint32_t),
              "ztick_buffer_view::version offset");
static_assert(sizeof(((ztick_buffer_view *)0)->version) == sizeof(uint32_t),
              "ztick_buffer_view::version type");
static_assert(offsetof(ztick_timing_sample, size) == 0, "ztick_timing_sample::size offset");
static_assert(offsetof(ztick_timing_sample, version) == sizeof(uint32_t),
              "ztick_timing_sample::version offset");
static_assert(sizeof(((ztick_timing_sample *)0)->version) == sizeof(uint32_t),
              "ztick_timing_sample::version type");
static_assert(offsetof(ztick_validation_result, size) == 0, "ztick_validation_result::size offset");
static_assert(offsetof(ztick_validation_result, version) == sizeof(uint32_t),
              "ztick_validation_result::version offset");
static_assert(sizeof(((ztick_validation_result *)0)->version) == sizeof(uint32_t),
              "ztick_validation_result::version type");

// Every remaining inventory field must have its declared type.
static_assert(std::is_same_v<decltype(((ztick_version *)0)->major), uint32_t>,
              "ztick_version::major missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_version *)0)->minor), uint32_t>,
              "ztick_version::minor missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_version *)0)->patch), uint32_t>,
              "ztick_version::patch missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_audio_format *)0)->format), ztick_sample_format>,
              "ztick_audio_format::format missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_audio_format *)0)->layout), ztick_channel_layout>,
              "ztick_audio_format::layout missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_audio_format *)0)->sample_rate), uint32_t>,
              "ztick_audio_format::sample_rate missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_audio_format *)0)->channels), uint32_t>,
              "ztick_audio_format::channels missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_audio_format *)0)->frames), uint32_t>,
              "ztick_audio_format::frames missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->name), const char *>,
              "ztick_parameter_descriptor::name missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->type), ztick_parameter_type>,
    "ztick_parameter_descriptor::type missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->units), const char *>,
              "ztick_parameter_descriptor::units missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->minimum), double>,
              "ztick_parameter_descriptor::minimum missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->maximum), double>,
              "ztick_parameter_descriptor::maximum missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->default_value), double>,
              "ztick_parameter_descriptor::default_value missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->allowed_values),
                             const char *const *>,
              "ztick_parameter_descriptor::allowed_values missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->allowed_value_count), uint32_t>,
    "ztick_parameter_descriptor::allowed_value_count missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_parameter_descriptor *)0)->description), const char *>,
    "ztick_parameter_descriptor::description missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_info *)0)->identifier), const char *>,
              "ztick_adapter_info::identifier missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_info *)0)->name), const char *>,
              "ztick_adapter_info::name missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_info *)0)->adapter_version), ztick_version>,
              "ztick_adapter_info::adapter_version missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_info *)0)->sdk_version), ztick_version>,
              "ztick_adapter_info::sdk_version missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_info *)0)->capabilities), uint64_t>,
              "ztick_adapter_info::capabilities missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_info *)0)->metadata), const char *>,
              "ztick_adapter_info::metadata missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_buffer_view *)0)->planes), void *const *>,
              "ztick_buffer_view::planes missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_buffer_view *)0)->strides), const size_t *>,
              "ztick_buffer_view::strides missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_buffer_view *)0)->plane_count), uint32_t>,
              "ztick_buffer_view::plane_count missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_buffer_view *)0)->frames), uint32_t>,
              "ztick_buffer_view::frames missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_buffer_view *)0)->format), ztick_audio_format>,
              "ztick_buffer_view::format missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_timing_sample *)0)->block_index), uint64_t>,
              "ztick_timing_sample::block_index missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_timing_sample *)0)->duration_seconds), double>,
              "ztick_timing_sample::duration_seconds missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_validation_result *)0)->check_name), const char *>,
              "ztick_validation_result::check_name missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_validation_result *)0)->outcome), ztick_status>,
              "ztick_validation_result::outcome missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_validation_result *)0)->absolute_tolerance), double>,
              "ztick_validation_result::absolute_tolerance missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_validation_result *)0)->relative_tolerance), double>,
              "ztick_validation_result::relative_tolerance missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_validation_result *)0)->reference_source), const char *>,
    "ztick_validation_result::reference_source missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_validation_result *)0)->details), const char *>,
              "ztick_validation_result::details missing or wrong type");

/**
 * @brief Entry point for the standalone C++20 types check.
 * @return Zero; every assertion is evaluated during compilation.
 */
int main() { return 0; }
