// SPDX-License-Identifier: MIT
/**
 * @file types.h
 * @brief Core value types shared by the SDK, core engine and result schema.
 *
 * The inventory in SDK_SPECIFICATION §1.2 is frozen: each listed type is
 * declared once here and used everywhere else. Public structs begin with
 * `size` and `version` (SDK_SPECIFICATION §1.1); callers zero-initialize them
 * and set both fields before passing them across the C ABI. Only the permitted
 * C headers are used, so this header is self-contained C11 and C++20.
 *
 * Enum constants use the `ztick_` prefix like all other exported C identifiers;
 * only macros use the `ZTICK_` prefix. Numeric values are explicit to keep the
 * ABI stable. Unknown enum or capability values must be treated as unsupported,
 * never assumed to be supported (SDK_SPECIFICATION §2).
 */

/** @brief Include guard for the public core types header. */
#ifndef ZTICK_TYPES_H
#define ZTICK_TYPES_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Outcome of a C ABI call or a validation check.
 * @note A processing failure is a workload failure; an internal error or
 *       incompatible SDK is an infrastructure failure. Classification rules
 *       are owned by ARCHITECTURE §6.
 */
typedef enum ztick_status {
  /** Operation completed successfully. */
  ztick_status_success = 0,
  /** The requested format, capability or value is not supported. */
  ztick_status_unsupported = 1,
  /** SDK major/minor compatibility check failed. */
  ztick_status_incompatible = 2,
  /** One or more arguments are invalid. */
  ztick_status_invalid_argument = 3,
  /** DSP processing failed; reported as a workload failure. */
  ztick_status_processing_failure = 4,
  /** A validation check failed; reported outside the timed interval. */
  ztick_status_validation_failure = 5,
  /** An internal error occurred; reported as an infrastructure failure. */
  ztick_status_internal_error = 6,
} ztick_status;

/**
 * @brief Sample storage format (BR §5.4).
 * @note `float32` is mandatory for every adapter; `float64` is optional.
 */
typedef enum ztick_sample_format {
  /** IEEE-754 single precision; mandatory in v1.0. */
  ztick_sample_format_float32 = 0,
  /** IEEE-754 double precision; optional in v1.0. */
  ztick_sample_format_float64 = 1,
} ztick_sample_format;

/**
 * @brief Arrangement of channel samples in a buffer (BR §5.4).
 */
typedef enum ztick_channel_layout {
  /** Channel samples are alternated in one contiguous buffer. */
  ztick_channel_layout_interleaved = 0,
  /** Each channel has its own contiguous plane. */
  ztick_channel_layout_planar = 1,
} ztick_channel_layout;

/**
 * @brief Provenance label for a reported metric value (BR §7.1).
 * @note A `not_applicable` value is present in the result, never omitted.
 */
typedef enum ztick_metric_status {
  /** Directly observed by instrumentation or monitoring. */
  ztick_metric_status_measured = 0,
  /** Derived or approximated, including overhead estimates. */
  ztick_metric_status_estimated = 1,
  /** The platform or integration could not provide the value. */
  ztick_metric_status_unavailable = 2,
  /** Undefined for the selected benchmark type or mode. */
  ztick_metric_status_not_applicable = 3,
} ztick_metric_status;

/**
 * @brief Availability state of a platform-provided field (BR §17).
 * @note Platform states map to metric value statuses in METRICS_REFERENCE §1.
 */
typedef enum ztick_availability {
  /** Supported and successfully read. */
  ztick_availability_value = 0,
  /** Supported in principle but the value could not be obtained now. */
  ztick_availability_unavailable = 1,
  /** Not implemented for this OS, backend or mode. */
  ztick_availability_unsupported = 2,
  /** Meaningless for the selected benchmark mode. */
  ztick_availability_not_applicable = 3,
} ztick_availability;

/**
 * @brief Isolation mode used to execute an integration (BR §6.3).
 */
typedef enum ztick_execution_mode {
  /** Integration runs inside the benchmark process. */
  ztick_execution_mode_in_process = 0,
  /** Integration runs in a separate process for fault isolation. */
  ztick_execution_mode_out_of_process = 1,
} ztick_execution_mode;

/**
 * @brief Semantic version triple used for adapters and the SDK (BR §22).
 * @note `size` and `version` describe this struct; `major`, `minor` and
 *       `patch` are the version being represented. Compatibility rules are
 *       defined in SDK_SPECIFICATION §2.
 */
typedef struct ztick_version {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Major component: must match exactly for compatibility. */
  uint32_t major;
  /** Minor component: a newer host is backward-compatible. */
  uint32_t minor;
  /** Patch component: differences are compatible. */
  uint32_t patch;
} ztick_version;

/**
 * @brief Explicit audio format of a workload or adapter capability.
 * @note Sample rate is in Hz; block size is expressed as `frames`.
 */
typedef struct ztick_audio_format {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Sample storage format; `float32` is mandatory. */
  ztick_sample_format format;
  /** Channel arrangement: interleaved or planar. */
  ztick_channel_layout layout;
  /** Sample rate in Hz; explicitly declared, never inferred. */
  uint32_t sample_rate;
  /** Number of channels. */
  uint32_t channels;
  /** Frame count per processing call (block size). */
  uint32_t frames;
} ztick_audio_format;

/**
 * @brief Declared kind of an adapter configuration parameter.
 * @note Derived from SDK_SPECIFICATION §4 to give
 *       @ref ztick_parameter_descriptor a typed `type` field. Unknown values
 *       are treated as unsupported.
 */
typedef enum ztick_parameter_type {
  /** Boolean value. */
  ztick_parameter_type_boolean = 0,
  /** Integer value. */
  ztick_parameter_type_integer = 1,
  /** Floating-point value. */
  ztick_parameter_type_floating_point = 2,
  /** Enumeration value selected from `allowed_values`. */
  ztick_parameter_type_enumeration = 3,
  /** String value, optionally constrained by `allowed_values`. */
  ztick_parameter_type_string = 4,
} ztick_parameter_type;

/**
 * @brief Typed metadata for one adapter configuration parameter.
 * @note Bounds apply to numeric types; `allowed_values` applies to enumeration
 *       and string types. All pointers are non-owning and remain valid for the
 *       lifetime of the adapter's descriptor.
 */
typedef struct ztick_parameter_descriptor {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Stable machine-readable parameter name. */
  const char *name;
  /** Declared parameter value kind. */
  ztick_parameter_type type;
  /** Unit of the value, or NULL when dimensionless. */
  const char *units;
  /** Inclusive lower bound for numeric types; unused otherwise. */
  double minimum;
  /** Inclusive upper bound for numeric types; unused otherwise. */
  double maximum;
  /** Default value for numeric types; unused otherwise. */
  double default_value;
  /** Non-owning array of allowed identifiers, or NULL when unconstrained. */
  const char *const *allowed_values;
  /** Number of entries in `allowed_values`; zero when NULL. */
  uint32_t allowed_value_count;
  /** Human-readable description of the parameter. */
  const char *description;
} ztick_parameter_descriptor;

/**
 * @brief Read-only identity and declared capabilities of an adapter.
 * @note Capabilities are an additive bitmask: unknown bits are unsupported.
 *       Metadata can be inspected without executing processing (BR §6.1).
 */
typedef struct ztick_adapter_info {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Stable machine-readable integration identifier. */
  const char *identifier;
  /** Human-readable integration name. */
  const char *name;
  /** Version of the integration itself. */
  ztick_version adapter_version;
  /** SDK ABI version the integration was built against. */
  ztick_version sdk_version;
  /** Additive bitmask of declared benchmark capabilities. */
  uint64_t capabilities;
  /** Optional extra metadata, or NULL when none is provided. */
  const char *metadata;
} ztick_adapter_info;

/**
 * @brief Non-owning view of audio sample planes.
 * @note Input views are read-only to the adapter during processing; output
 *       views are writable. Pointers must not be retained after the call
 *       returns unless the adapter declares that behavior (SDK_SPECIFICATION §5).
 */
typedef struct ztick_buffer_view {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Non-owning array of `plane_count` plane pointers, possibly writable. */
  void *const *planes;
  /** Non-owning array of per-plane strides in bytes, or NULL when packed. */
  const size_t *strides;
  /** Number of entries in `planes` (and `strides` when present). */
  uint32_t plane_count;
  /** Frame count in each plane. */
  uint32_t frames;
  /** Explicit format of the sample data. */
  ztick_audio_format format;
} ztick_buffer_view;

/**
 * @brief One per-block timing observation (SDK_SPECIFICATION §6).
 * @note Emitted only when per-block timing is selected; raw samples are
 *       retained and overhead estimates are never subtracted from them.
 */
typedef struct ztick_timing_sample {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Zero-based index of the processing block. */
  uint64_t block_index;
  /** Measured DSP interval duration in seconds. */
  double duration_seconds;
} ztick_timing_sample;

/**
 * @brief Outcome of one validation check (SDK_SPECIFICATION §7).
 * @note Validation runs after timing and must not distort measured intervals.
 *       Outcomes are reported per check, not only as one pass/fail flag.
 */
typedef struct ztick_validation_result {
  /** Size of this struct in bytes; set by the caller before use. */
  uint32_t size;
  /** Layout version of this struct; set by the caller before use. */
  uint32_t version;
  /** Stable machine-readable check identifier. */
  const char *check_name;
  /** Outcome: success or validation_failure for a completed check. */
  ztick_status outcome;
  /** Configured absolute tolerance; zero when not applicable. */
  double absolute_tolerance;
  /** Configured relative tolerance; zero when not applicable. */
  double relative_tolerance;
  /** Reference source: adapter, workload_file, generated or reference_implementation. */
  const char *reference_source;
  /** Human-readable detail, or NULL when none is available. */
  const char *details;
} ztick_validation_result;

#endif // ZTICK_TYPES_H
