// SPDX-License-Identifier: MIT
/**
 * @file sdk.h
 * @brief Versioned, C-only integration boundary for ztick-bench adapters.
 *
 * This header is the single public C ABI surface (SDK_SPECIFICATION §1.1). It
 * uses only fixed-width C types and the headers permitted by the frozen header
 * contract; no C++ types, templates, exceptions or C++ ABI appear here. The
 * `#ifdef __cplusplus` guard only requests C linkage for C++ consumers, which
 * preserves the C ABI rather than leaking C++.
 *
 * Adapters expose a read-only @ref ztick_adapter_descriptor. Dynamic adapters
 * export it under @ref ZTICK_ADAPTER_EXPORT_SYMBOL so a host can inspect
 * identity and capabilities without executing plugin code (BR §6.1). Static
 * adapters publish the same descriptor through @ref ztick_register_adapter.
 * Callers zero-initialize every public struct, set its `size` and `version`
 * fields, and use the `ztick_status` codes as the `int32_t` return values.
 */

/** @brief Include guard for the public SDK C ABI header. */
#ifndef ZTICK_SDK_H
#define ZTICK_SDK_H

#include <ztick/types.h>
#include <ztick/version.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Export/import decoration for public SDK functions.
 * @note Define `ZTICK_BUILDING_SDK` when compiling the SDK implementation;
 *       adapter implementations do not need it.
 */
#if defined(_WIN32)
#if defined(ZTICK_BUILDING_SDK)
#define ZTICK_API __declspec(dllexport)
#else
#define ZTICK_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define ZTICK_API __attribute__((visibility("default")))
#else
#define ZTICK_API
#endif

/**
 * @brief Symbol name of the read-only descriptor exported by a dynamic adapter.
 * @note Hosts resolve this data symbol through the platform loader; reading it
 *       never calls adapter code.
 */
#define ZTICK_ADAPTER_EXPORT_SYMBOL "ztick_adapter_export"

/** @brief Opaque handle to one adapter instance, created and destroyed by its ops. */
typedef struct ztick_adapter ztick_adapter;

/**
 * @brief Callback used by an adapter to emit one reported metric value.
 * @param context Opaque host context supplied to @ref ztick_adapter_report_fn.
 * @param name Stable machine-readable metric identifier.
 * @param value Numeric value; ignored when @p status is not measured or estimated.
 * @param unit Unit of @p value, or NULL for dimensionless metrics.
 * @param status Provenance label; distinct from unavailable and not-applicable.
 * @param source Collection method or instrument reference, or NULL when unknown.
 * @return A `ztick_status` code; a failed sink aborts the report operation.
 */
typedef int32_t (*ztick_metric_sink_fn)(void *context, const char *name, double value,
                                        const char *unit, ztick_metric_status status,
                                        const char *source);

/**
 * @brief Create one adapter instance.
 * @param out_adapter Receives the new opaque handle; must not be NULL.
 * @return A `ztick_status` code.
 * @note The handle is owned by the adapter and released by @ref ztick_adapter_destroy_fn.
 */
typedef int32_t (*ztick_adapter_create_fn)(ztick_adapter **out_adapter);

/**
 * @brief Destroy an adapter instance and release all of its resources.
 * @param adapter Handle returned by @ref ztick_adapter_create_fn; may be NULL.
 * @return A `ztick_status` code.
 */
typedef int32_t (*ztick_adapter_destroy_fn)(ztick_adapter *adapter);

/**
 * @brief Describe supported formats, parameters and unsupported configurations.
 * @param adapter Constant adapter handle; must not be NULL.
 * @param formats Receives a non-owning array of supported formats.
 * @param format_count Receives the number of entries in @p formats.
 * @param parameters Receives a non-owning array of typed parameter descriptors.
 * @param parameter_count Receives the number of entries in @p parameters.
 * @param unsupported Receives non-owning machine-readable reason strings.
 * @param unsupported_count Receives the number of entries in @p unsupported.
 * @return A `ztick_status` code.
 * @note Every output array remains valid for the adapter's lifetime.
 */
typedef int32_t (*ztick_adapter_describe_fn)(
    const ztick_adapter *adapter, const ztick_audio_format **formats, uint32_t *format_count,
    const ztick_parameter_descriptor **parameters, uint32_t *parameter_count,
    const char *const **unsupported, uint32_t *unsupported_count);

/**
 * @brief Apply one validated typed configuration parameter.
 * @param adapter Adapter handle; must not be NULL.
 * @param name Parameter name from @ref ztick_adapter_describe_fn.
 * @param value Pointer to the value in the representation declared by the descriptor.
 * @param value_size Size of @p value in bytes, including a string terminator.
 * @return A `ztick_status` code, e.g. `ztick_status_unsupported` for an unknown value.
 * @note Representations: uint32_t boolean, int64_t integer, double floating_point,
 *       uint32_t enumeration index, or a NUL-terminated char array for string.
 */
typedef int32_t (*ztick_adapter_configure_fn)(ztick_adapter *adapter, const char *name,
                                              const void *value, size_t value_size);

/**
 * @brief Accept generated input and optional reference data before timing starts.
 * @param adapter Adapter handle; must not be NULL.
 * @param input Input buffer view supplied by the host.
 * @param reference Reference buffer view, or NULL when none is supplied.
 * @return A `ztick_status` code.
 * @note Preparation is always outside the primary DSP interval (BR §4.2, §7.4).
 */
typedef int32_t (*ztick_adapter_prepare_fn)(ztick_adapter *adapter, const ztick_buffer_view *input,
                                            const ztick_buffer_view *reference);

/**
 * @brief Process one audio block, optionally emitting per-block timing.
 * @param adapter Adapter handle; must not be NULL.
 * @param input Constant input buffer view; read-only to the adapter.
 * @param output Writable output buffer view.
 * @param timing Receives one timing sample when non-NULL; NULL selects aggregate timing.
 * @return A `ztick_status` code; a failure here is a workload failure.
 * @note No validation work may occur inside this measured interval.
 */
typedef int32_t (*ztick_adapter_process_fn)(ztick_adapter *adapter, const ztick_buffer_view *input,
                                            ztick_buffer_view *output, ztick_timing_sample *timing);

/**
 * @brief Drain or flush the adapter and finalize instrumented counters.
 * @param adapter Adapter handle; must not be NULL.
 * @return A `ztick_status` code.
 */
typedef int32_t (*ztick_adapter_finish_fn)(ztick_adapter *adapter);

/**
 * @brief Run validation checks after timing has stopped.
 * @param adapter Constant adapter handle; must not be NULL.
 * @param requested Requested checks with tolerances and reference sources.
 * @param requested_count Number of entries in @p requested.
 * @param results Output array for per-check outcomes.
 * @param result_count In/out capacity on entry, number of results written on return.
 * @return A `ztick_status` code.
 * @note Validation never changes measured values or runs inside the DSP interval.
 */
typedef int32_t (*ztick_adapter_validate_fn)(const ztick_adapter *adapter,
                                             const ztick_validation_result *requested,
                                             uint32_t requested_count,
                                             ztick_validation_result *results,
                                             uint32_t *result_count);

/**
 * @brief Report measurements through a host-provided sink.
 * @param adapter Constant adapter handle; must not be NULL.
 * @param sink Callback that receives each metric value.
 * @param context Opaque host context passed through to @p sink.
 * @return A `ztick_status` code.
 * @note Raw timings and overhead estimates are emitted as distinct metrics;
 *       the SDK never subtracts one from the other (BR §7.3).
 */
typedef int32_t (*ztick_adapter_report_fn)(const ztick_adapter *adapter, ztick_metric_sink_fn sink,
                                           void *context);

/**
 * @brief Function table implementing the adapter lifecycle (SDK_SPECIFICATION §3).
 * @note Every entry is required; unused optional capabilities return
 *       `ztick_status_unsupported` with an explanation in the descriptor.
 */
typedef struct ztick_adapter_ops {
  /** Size of this struct in bytes; set by the adapter before use. */
  uint32_t size;
  /** Layout version of this struct; set by the adapter before use. */
  uint32_t version;
  /** Create an adapter instance. */
  ztick_adapter_create_fn create;
  /** Destroy an adapter instance. */
  ztick_adapter_destroy_fn destroy;
  /** Declare formats, parameters and unsupported configurations. */
  ztick_adapter_describe_fn describe;
  /** Apply one typed configuration parameter. */
  ztick_adapter_configure_fn configure;
  /** Prepare inputs and references outside timing. */
  ztick_adapter_prepare_fn prepare;
  /** Process one measured audio block. */
  ztick_adapter_process_fn process;
  /** Drain and finalize counters. */
  ztick_adapter_finish_fn finish;
  /** Run post-timing validation checks. */
  ztick_adapter_validate_fn validate;
  /** Emit measurements with explicit value statuses. */
  ztick_adapter_report_fn report;
} ztick_adapter_ops;

/**
 * @brief Read-only identity, compatibility data and lifecycle operations.
 * @note A host may inspect `info` without invoking any function in `ops`.
 */
typedef struct ztick_adapter_descriptor {
  /** Size of this struct in bytes; set by the adapter before use. */
  uint32_t size;
  /** Layout version of this struct; set by the adapter before use. */
  uint32_t version;
  /** Identity, version, SDK version and declared capabilities. */
  ztick_adapter_info info;
  /** Lifecycle implementation for this adapter. */
  ztick_adapter_ops ops;
} ztick_adapter_descriptor;

/**
 * @brief Query the SDK ABI version at runtime.
 * @param out_version Receives major, minor and patch; must not be NULL.
 * @return A `ztick_status` code.
 * @note Reports the same values as the `ZTICK_SDK_VERSION_*` macros.
 */
ZTICK_API int32_t ztick_sdk_get_version(ztick_version *out_version);

/**
 * @brief Check an adapter's SDK version against this host's compatibility rules.
 * @param adapter_sdk_version Version the adapter was built against; must not be NULL.
 * @return `ztick_status_success` when compatible, otherwise `ztick_status_incompatible`.
 * @note Major versions must match exactly; an older host refuses a newer minor.
 */
ZTICK_API int32_t ztick_sdk_is_compatible(const ztick_version *adapter_sdk_version);

/**
 * @brief Register a static adapter descriptor during host initialization.
 * @param descriptor Read-only descriptor; must not be NULL and must outlive the registry.
 * @return A `ztick_status` code, e.g. `ztick_status_incompatible` on version mismatch.
 * @note Registration alone never creates an instance or executes processing.
 */
ZTICK_API int32_t ztick_register_adapter(const ztick_adapter_descriptor *descriptor);

/**
 * @brief Return a human-readable explanation for a status code.
 * @param status A `ztick_status` code.
 * @return A NUL-terminated static string; never NULL.
 */
ZTICK_API const char *ztick_status_message(int32_t status);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZTICK_SDK_H
