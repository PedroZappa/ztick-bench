// SPDX-License-Identifier: MIT
/**
 * @file types_header_c11_test.c
 * @brief Standalone C11 compile check and inventory assertions for types.h.
 *
 * The public header is included first and on its own, so a successful build
 * proves it is self-contained C11. The checks lock the frozen type inventory
 * (SDK_SPECIFICATION §1.2), explicit enum values and the leading `size` and
 * `version` fields required for struct evolution (SDK_SPECIFICATION §1.1).
 * Every check is evaluated at compile time.
 */

#include <ztick/types.h>

#include <stddef.h>

/* The build selects C11 with extensions disabled; verify the effective mode. */
_Static_assert(__STDC_VERSION__ >= 201112L, "types.h test requires C11");

/* Explicit status codes (SDK_SPECIFICATION §8). */
_Static_assert(ztick_status_success == 0, "unexpected success status code");
_Static_assert(ztick_status_unsupported == 1, "unexpected unsupported status code");
_Static_assert(ztick_status_incompatible == 2, "unexpected incompatible status code");
_Static_assert(ztick_status_invalid_argument == 3, "unexpected invalid argument status code");
_Static_assert(ztick_status_processing_failure == 4, "unexpected processing failure status code");
_Static_assert(ztick_status_validation_failure == 5, "unexpected validation failure status code");
_Static_assert(ztick_status_internal_error == 6, "unexpected internal error status code");

/* Explicit format, layout, status and mode codes. */
_Static_assert(ztick_sample_format_float32 == 0, "unexpected float32 code");
_Static_assert(ztick_sample_format_float64 == 1, "unexpected float64 code");
_Static_assert(ztick_channel_layout_interleaved == 0, "unexpected interleaved code");
_Static_assert(ztick_channel_layout_planar == 1, "unexpected planar code");
_Static_assert(ztick_metric_status_measured == 0, "unexpected measured code");
_Static_assert(ztick_metric_status_estimated == 1, "unexpected estimated code");
_Static_assert(ztick_metric_status_unavailable == 2, "unexpected unavailable code");
_Static_assert(ztick_metric_status_not_applicable == 3, "unexpected not_applicable code");
_Static_assert(ztick_availability_value == 0, "unexpected value availability code");
_Static_assert(ztick_availability_unavailable == 1, "unexpected unavailable availability code");
_Static_assert(ztick_availability_unsupported == 2, "unexpected unsupported availability code");
_Static_assert(ztick_availability_not_applicable == 3,
               "unexpected not_applicable availability code");
_Static_assert(ztick_execution_mode_in_process == 0, "unexpected in_process code");
_Static_assert(ztick_execution_mode_out_of_process == 1, "unexpected out_of_process code");
_Static_assert(ztick_parameter_type_boolean == 0, "unexpected boolean parameter code");
_Static_assert(ztick_parameter_type_integer == 1, "unexpected integer parameter code");
_Static_assert(ztick_parameter_type_floating_point == 2,
               "unexpected floating-point parameter code");
_Static_assert(ztick_parameter_type_enumeration == 3, "unexpected enumeration parameter code");
_Static_assert(ztick_parameter_type_string == 4, "unexpected string parameter code");

/* Every public struct begins with size and version. */
_Static_assert(offsetof(ztick_version, size) == 0, "ztick_version::size offset");
_Static_assert(offsetof(ztick_version, version) == sizeof(uint32_t),
               "ztick_version::version offset");
_Static_assert(sizeof(((ztick_version *)0)->version) == sizeof(uint32_t),
               "ztick_version::version type");
_Static_assert(offsetof(ztick_audio_format, size) == 0, "ztick_audio_format::size offset");
_Static_assert(offsetof(ztick_audio_format, version) == sizeof(uint32_t),
               "ztick_audio_format::version offset");
_Static_assert(sizeof(((ztick_audio_format *)0)->version) == sizeof(uint32_t),
               "ztick_audio_format::version type");
_Static_assert(offsetof(ztick_parameter_descriptor, size) == 0,
               "ztick_parameter_descriptor::size offset");
_Static_assert(offsetof(ztick_parameter_descriptor, version) == sizeof(uint32_t),
               "ztick_parameter_descriptor::version offset");
_Static_assert(sizeof(((ztick_parameter_descriptor *)0)->version) == sizeof(uint32_t),
               "ztick_parameter_descriptor::version type");
_Static_assert(offsetof(ztick_adapter_info, size) == 0, "ztick_adapter_info::size offset");
_Static_assert(offsetof(ztick_adapter_info, version) == sizeof(uint32_t),
               "ztick_adapter_info::version offset");
_Static_assert(sizeof(((ztick_adapter_info *)0)->version) == sizeof(uint32_t),
               "ztick_adapter_info::version type");
_Static_assert(offsetof(ztick_buffer_view, size) == 0, "ztick_buffer_view::size offset");
_Static_assert(offsetof(ztick_buffer_view, version) == sizeof(uint32_t),
               "ztick_buffer_view::version offset");
_Static_assert(sizeof(((ztick_buffer_view *)0)->version) == sizeof(uint32_t),
               "ztick_buffer_view::version type");
_Static_assert(offsetof(ztick_timing_sample, size) == 0, "ztick_timing_sample::size offset");
_Static_assert(offsetof(ztick_timing_sample, version) == sizeof(uint32_t),
               "ztick_timing_sample::version offset");
_Static_assert(sizeof(((ztick_timing_sample *)0)->version) == sizeof(uint32_t),
               "ztick_timing_sample::version type");
_Static_assert(offsetof(ztick_validation_result, size) == 0,
               "ztick_validation_result::size offset");
_Static_assert(offsetof(ztick_validation_result, version) == sizeof(uint32_t),
               "ztick_validation_result::version offset");
_Static_assert(sizeof(((ztick_validation_result *)0)->version) == sizeof(uint32_t),
               "ztick_validation_result::version type");

/* Every remaining inventory field must be declared. */
_Static_assert(sizeof(((ztick_version *)0)->major) > 0 && sizeof(((ztick_version *)0)->minor) > 0 &&
                   sizeof(((ztick_version *)0)->patch) > 0,
               "ztick_version inventory incomplete");
_Static_assert(sizeof(((ztick_audio_format *)0)->format) > 0 &&
                   sizeof(((ztick_audio_format *)0)->layout) > 0 &&
                   sizeof(((ztick_audio_format *)0)->sample_rate) > 0 &&
                   sizeof(((ztick_audio_format *)0)->channels) > 0 &&
                   sizeof(((ztick_audio_format *)0)->frames) > 0,
               "ztick_audio_format inventory incomplete");
_Static_assert(sizeof(((ztick_parameter_descriptor *)0)->name) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->type) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->units) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->minimum) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->maximum) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->default_value) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->allowed_values) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->allowed_value_count) > 0 &&
                   sizeof(((ztick_parameter_descriptor *)0)->description) > 0,
               "ztick_parameter_descriptor inventory incomplete");
_Static_assert(sizeof(((ztick_adapter_info *)0)->identifier) > 0 &&
                   sizeof(((ztick_adapter_info *)0)->name) > 0 &&
                   sizeof(((ztick_adapter_info *)0)->adapter_version) > 0 &&
                   sizeof(((ztick_adapter_info *)0)->sdk_version) > 0 &&
                   sizeof(((ztick_adapter_info *)0)->capabilities) > 0 &&
                   sizeof(((ztick_adapter_info *)0)->metadata) > 0,
               "ztick_adapter_info inventory incomplete");
_Static_assert(sizeof(((ztick_buffer_view *)0)->planes) > 0 &&
                   sizeof(((ztick_buffer_view *)0)->strides) > 0 &&
                   sizeof(((ztick_buffer_view *)0)->plane_count) > 0 &&
                   sizeof(((ztick_buffer_view *)0)->frames) > 0 &&
                   sizeof(((ztick_buffer_view *)0)->format) > 0,
               "ztick_buffer_view inventory incomplete");
_Static_assert(sizeof(((ztick_timing_sample *)0)->block_index) > 0 &&
                   sizeof(((ztick_timing_sample *)0)->duration_seconds) > 0,
               "ztick_timing_sample inventory incomplete");
_Static_assert(sizeof(((ztick_validation_result *)0)->check_name) > 0 &&
                   sizeof(((ztick_validation_result *)0)->outcome) > 0 &&
                   sizeof(((ztick_validation_result *)0)->absolute_tolerance) > 0 &&
                   sizeof(((ztick_validation_result *)0)->relative_tolerance) > 0 &&
                   sizeof(((ztick_validation_result *)0)->reference_source) > 0 &&
                   sizeof(((ztick_validation_result *)0)->details) > 0,
               "ztick_validation_result inventory incomplete");

/**
 * @brief Entry point for the standalone C11 types check.
 * @return Zero; every assertion is evaluated during compilation.
 */
int main(void) { return 0; }
