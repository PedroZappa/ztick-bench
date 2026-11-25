// SPDX-License-Identifier: MIT
/**
 * @file frontend.h
 * @brief Private, internal C11 control boundary for the Rust TUI front end.
 *
 * This header implements SDK_SPECIFICATION §14. It is deliberately private: it
 * is not installed, not exported by find_package and is not part of the adapter
 * SDK or the installed `1.0.0` SDK ABI. The Rust TUI calls these `ztick_`
 * symbols through FFI; no C++ type, exception or iterator crosses this header.
 *
 * Every caller-owned struct starts zero-initialized. Before passing one, set
 * `size = sizeof(struct)` and `version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION`.
 * All functions return an `int32_t` `ztick_status` code unless documented
 * otherwise. Output strings are NUL-terminated and copied into caller-owned
 * buffers; borrowed progress events are copied into caller-owned event structs.
 * When an output buffer is too small, `out_length` still receives the required
 * byte count and no partial copy is made. Each session is owned by one front-end
 * thread; separate sessions may be used concurrently. An asynchronous run uses a
 * session-owned worker, but the caller still performs every other session call
 * from the owning thread.
 */

#ifndef ZTICK_FRONTEND_H
#define ZTICK_FRONTEND_H

#include <ztick/types.h>

#include <stddef.h>
#include <stdint.h>

/** @brief Major version of the private front-end interface. */
#define ZTICK_FRONTEND_VERSION_MAJOR 0
/** @brief Minor version of the private front-end interface. */
#define ZTICK_FRONTEND_VERSION_MINOR 5
/** @brief Patch version of the private front-end interface. */
#define ZTICK_FRONTEND_VERSION_PATCH 0
/** @brief Layout version required in every public struct header. */
#define ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION 1
/** @brief Maximum message bytes carried by one copied progress event. */
#define ZTICK_FRONTEND_EVENT_MESSAGE_SIZE 256
/** @brief Select a two-sided significance interval for policy comparison.
 * @note Stable value 0; forwarded to the CLI as `--sided two`.
 */
#define ZTICK_FRONTEND_COMPARE_TWO_SIDED 0
/** @brief Select a one-sided significance interval for policy comparison.
 * @note Stable value 1; forwarded to the CLI as `--sided one`.
 */
#define ZTICK_FRONTEND_COMPARE_ONE_SIDED 1

/** @brief Mark a private boundary symbol for export from the built library. */
#if defined(_WIN32) && defined(ZTICK_BUILDING_FRONTEND)
#define ZTICK_FRONTEND_API __declspec(dllexport)
#elif defined(__GNUC__) && defined(ZTICK_BUILDING_FRONTEND)
#define ZTICK_FRONTEND_API __attribute__((visibility("default")))
#else
#define ZTICK_FRONTEND_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run states the front end must present (TUI_SPECIFICATION §3).
 * @note Values are stable within private interface major version 0.
 */
typedef enum ztick_frontend_run_state {
  /** @brief No run is active. */
  ztick_frontend_state_idle = 0,
  /** @brief Settings are being edited. */
  ztick_frontend_state_configuring = 1,
  /** @brief Capability and config checks are in progress. */
  ztick_frontend_state_validating = 2,
  /** @brief Timed execution is active. */
  ztick_frontend_state_running = 3,
  /** @brief An instability warning was recorded. */
  ztick_frontend_state_warning = 4,
  /** @brief A workload or infrastructure failure was classified. */
  ztick_frontend_state_failed = 5,
  /** @brief The run ended with failures or an interruption. */
  ztick_frontend_state_incomplete = 6,
  /** @brief Every configured test finished successfully. */
  ztick_frontend_state_complete = 7,
} ztick_frontend_run_state;

/**
 * @brief Kind of one copied progress event.
 * @note Unknown future kinds must be treated as unsupported by the front end.
 */
typedef enum ztick_frontend_event_kind {
  /** @brief The active run state changed. */
  ztick_frontend_event_state_change = 0,
  /** @brief A progress update for one test point. */
  ztick_frontend_event_progress = 1,
  /** @brief A warning was recorded while a run was active. */
  ztick_frontend_event_warning = 2,
  /** @brief A command finished and its output is available. */
  ztick_frontend_event_completion = 3,
} ztick_frontend_event_kind;

/**
 * @brief Timing level reported by a progress event.
 */
typedef enum ztick_frontend_timing_level {
  /** @brief No timing level is active. */
  ztick_frontend_timing_unknown = 0,
  /** @brief Per-block timing is active. */
  ztick_frontend_timing_per_block = 1,
  /** @brief Whole-run timing is active. */
  ztick_frontend_timing_whole_run = 2,
  /** @brief Explicitly enabled end-to-end timing is active. */
  ztick_frontend_timing_end_to_end = 3,
} ztick_frontend_timing_level;

/**
 * @brief Isolation mode reported by a progress event.
 */
typedef enum ztick_frontend_isolation_mode {
  /** @brief No isolation mode is active. */
  ztick_frontend_isolation_unknown = 0,
  /** @brief The integration runs in the host process. */
  ztick_frontend_isolation_in_process = 1,
  /** @brief The integration runs out of process. */
  ztick_frontend_isolation_out_of_process = 2,
} ztick_frontend_isolation_mode;

/**
 * @brief Version of the private front-end interface.
 */
typedef struct ztick_frontend_version {
  /** @brief Byte size of this struct, set by the caller. */
  uint32_t size;
  /** @brief Struct layout version, set by the caller. */
  uint32_t version;
  /** @brief Interface major version; breaking changes only. */
  uint32_t major;
  /** @brief Interface minor version; additive changes. */
  uint32_t minor;
  /** @brief Interface patch version. */
  uint32_t patch;
} ztick_frontend_version;

/**
 * @brief Optional session construction settings.
 * @note A NULL options pointer selects the platform default trust store.
 */
typedef struct ztick_frontend_session_options {
  /** @brief Byte size of this struct, set by the caller. */
  uint32_t size;
  /** @brief Struct layout version, set by the caller. */
  uint32_t version;
  /** @brief Optional trusted-plugin store path, or NULL for the platform default. */
  const char *trust_store_path;
} ztick_frontend_session_options;

/**
 * @brief One copied progress event delivered to the front end.
 */
typedef struct ztick_frontend_event {
  /** @brief Byte size of this struct, set by the caller. */
  uint32_t size;
  /** @brief Struct layout version, set by the caller. */
  uint32_t version;
  /** @brief Event kind from @ref ztick_frontend_event_kind. */
  int32_t kind;
  /** @brief Active run state from @ref ztick_frontend_run_state. */
  int32_t state;
  /** @brief One-based test point, or zero when not applicable. */
  uint32_t test_point;
  /** @brief Timing level from @ref ztick_frontend_timing_level. */
  int32_t timing_level;
  /** @brief Isolation mode from @ref ztick_frontend_isolation_mode. */
  int32_t isolation_mode;
  /** @brief NUL-terminated human-readable context; may be empty. */
  char message[ZTICK_FRONTEND_EVENT_MESSAGE_SIZE];
} ztick_frontend_event;

/** @brief Opaque front-end session handle owned by the caller. */
typedef struct ztick_frontend_session ztick_frontend_session;

/**
 * @brief Callback receiving one copied progress event.
 * @param event Borrowed pointer valid only for the duration of the callback.
 * @param user_data Opaque caller context passed back unchanged.
 * @note The callback must not call into DSP code or destroy the session.
 */
typedef void (*ztick_frontend_progress_callback)(const ztick_frontend_event *event,
                                                 void *user_data);

/**
 * @brief Query the private front-end interface version.
 * @param out_version Caller-owned struct; must not be NULL and must have its header
 *        initialized.
 * @return `ztick_status_success`, or `ztick_status_invalid_argument` for a malformed
 *         struct header or NULL pointer.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_get_version(ztick_frontend_version *out_version);

/**
 * @brief Read a stable message for one `ztick_status` code.
 * @param status Status code to explain, including unknown values.
 * @return Static NUL-terminated string owned by the library.
 */
ZTICK_FRONTEND_API const char *ztick_frontend_status_message(int32_t status);

/**
 * @brief Create one front-end session.
 * @param options Optional settings; NULL selects the platform default trust store.
 * @return Owned session handle, or NULL when construction fails.
 * @note The caller owns the returned handle and must pass it to
 *       @ref ztick_frontend_session_destroy. Handle values are never reused, so
 *       a destroyed handle cannot become valid again through allocator reuse.
 */
ZTICK_FRONTEND_API ztick_frontend_session *
ztick_frontend_session_create(const ztick_frontend_session_options *options);

/**
 * @brief Destroy one front-end session.
 * @param session Session to destroy; NULL is a no-op.
 * @note Destroy is idempotent: a second call with the same handle does nothing.
 *       Every other call with a destroyed handle returns
 *       `ztick_status_invalid_argument`; borrowed views and callbacks are invalid.
 */
ZTICK_FRONTEND_API void ztick_frontend_session_destroy(ztick_frontend_session *session);

/**
 * @brief Copy the most recent session error into a caller-owned buffer.
 * @param session Session to query; must not be NULL.
 * @param buffer Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p buffer.
 * @param out_length Optional receiver for the message length excluding the NUL.
 * @return Success with an empty string when no error is recorded, or
 *         `ztick_status_invalid_argument` for an invalid argument or short buffer.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_last_error(const ztick_frontend_session *session,
                                                     char *buffer, size_t capacity,
                                                     size_t *out_length);

/**
 * @brief Parse and retain one v1 config document from JSON text.
 * @param session Session to update; must not be NULL.
 * @param text Complete JSON text; must not be NULL when @p length is non-zero.
 * @param length Byte length of @p text.
 * @return Success, `ztick_status_invalid_argument` for malformed input, or
 *         `ztick_status_validation_failure` for schema or core semantic rejection.
 * @note A rejected document is not retained; validation matches the core planner.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_config_parse(ztick_frontend_session *session,
                                                       const char *text, size_t length);

/**
 * @brief Load and parse one v1 config document from a file.
 * @param session Session to update; must not be NULL.
 * @param path NUL-terminated file path; must not be NULL.
 * @return Success, `ztick_status_invalid_argument` for malformed input,
 *         `ztick_status_validation_failure` for schema or semantic rejection, or
 *         `ztick_status_processing_failure` when the file cannot be read.
 * @note A rejected document is not retained; validation matches the core planner.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_config_load(ztick_frontend_session *session,
                                                      const char *path);

/**
 * @brief Revalidate the config retained by a session.
 * @param session Session holding a parsed config; must not be NULL.
 * @return Success, `ztick_status_invalid_argument` when no config is retained, or
 *         `ztick_status_validation_failure` when the document fails schema or core
 *         semantic validation.
 * @note Semantic validation uses the core execution planner, so a document that
 *       the core would reject at run time cannot be reported as valid here.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_config_validate(ztick_frontend_session *session);

/**
 * @brief Apply dotted-path overrides to the retained config and copy its JSON.
 * @param session Session holding a parsed config; must not be NULL.
 * @param assignments Array of NUL-terminated `path=value` strings; must not be NULL
 *        when @p count is non-zero.
 * @param count Number of assignments, applied in order.
 * @param buffer Destination buffer for the updated config JSON; must not be NULL when
 *        @p capacity is non-zero.
 * @param capacity Byte capacity of @p buffer.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Success, `ztick_status_invalid_argument` for malformed arguments, or
 *         `ztick_status_validation_failure` when an override or the resulting
 *         config is rejected by the core planner.
 * @note The retained config is unchanged when any override or semantic check fails.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_config_override(ztick_frontend_session *session,
                                                          const char *const *assignments,
                                                          size_t count, char *buffer,
                                                          size_t capacity, size_t *out_length);

/**
 * @brief Serialize the retained config as compact JSON.
 * @param session Session holding a parsed config; must not be NULL.
 * @param buffer Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p buffer.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Success, `ztick_status_invalid_argument` for an invalid argument or short
 *         buffer, or `ztick_status_validation_failure` when serialization fails.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_config_serialize(ztick_frontend_session *session,
                                                           char *buffer, size_t capacity,
                                                           size_t *out_length);

/**
 * @brief Atomically save the retained config as a v1 JSON document.
 * @param session Session holding a parsed config; must not be NULL.
 * @param path NUL-terminated destination path; must not be NULL.
 * @return Success, `ztick_status_invalid_argument` for an invalid argument, or
 *         `ztick_status_processing_failure` when the write fails.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_config_save(ztick_frontend_session *session,
                                                      const char *path);

/**
 * @brief Run `list` and copy its JSON envelope into a caller-owned buffer.
 * @param session Session to use; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_list(ztick_frontend_session *session, char *out_json,
                                               size_t capacity, size_t *out_length);

/**
 * @brief Run `system` and copy its JSON envelope into a caller-owned buffer.
 * @param session Session to use; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_system(ztick_frontend_session *session, char *out_json,
                                                 size_t capacity, size_t *out_length);

/**
 * @brief Run `run` for one config file and copy its JSON envelope.
 * @param session Session to use; must not be NULL.
 * @param config_path NUL-terminated config path; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_run(ztick_frontend_session *session,
                                              const char *config_path, char *out_json,
                                              size_t capacity, size_t *out_length);

/**
 * @brief Start `run` on a session-owned worker and return without blocking.
 * @param session Session to use; must not be NULL.
 * @param config_path NUL-terminated config path copied before the worker starts.
 * @return `ztick_status_success` when the worker started,
 *         `ztick_status_invalid_argument` for a NULL argument, or
 *         `ztick_status_processing_failure` when another run is active.
 * @note Core progress is delivered through queued copied events and, when
 *       registered, the progress callback. The callback runs on the worker; it
 *       must not call any other session operation or destroy the session.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_run_start(ztick_frontend_session *session,
                                                    const char *config_path);

/**
 * @brief Copy the result of the most recent asynchronous run.
 * @param session Session to use; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @param out_available Required receiver: 1 when a completed result is copied,
 *        0 while a run is pending or no run has been started.
 * @return `ztick_status_success` for an available or pending result, or a status
 *         describing a completed run that failed.
 * @note When @p out_available is 0 no bytes are written to @p out_json. The
 *       result remains available until the next @ref ztick_frontend_run_start.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_run_result(ztick_frontend_session *session,
                                                     char *out_json, size_t capacity,
                                                     size_t *out_length, int32_t *out_available);

/**
 * @brief Run `report` for one stored result and copy its JSON envelope.
 * @param session Session to use; must not be NULL.
 * @param result_path NUL-terminated result path; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_report(ztick_frontend_session *session,
                                                 const char *result_path, char *out_json,
                                                 size_t capacity, size_t *out_length);

/**
 * @brief Run policy-driven `compare` for two stored results and copy its JSON envelope.
 * @param session Session to use; must not be NULL and must refer to a live session.
 * @param baseline_path NUL-terminated baseline path; must not be NULL or empty.
 * @param candidate_path NUL-terminated candidate path; must not be NULL or empty.
 * @param threshold_percent NUL-terminated percentage text; must not be NULL or empty.
 * @param alpha NUL-terminated significance-level text; must not be NULL or empty.
 * @param sided Significance interval, either @ref ZTICK_FRONTEND_COMPARE_TWO_SIDED or
 *        @ref ZTICK_FRONTEND_COMPARE_ONE_SIDED; no other value is accepted.
 * @param fail_on_regression One to pass `--fail-on-regression`, zero to omit it; no other
 *        value is accepted.
 * @param out_json Destination buffer for the JSON envelope; must not be NULL when
 *        @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code: success, invalid argument for
 *         usage errors, unsupported for an unavailable command, processing failure for
 *         runtime failures, or validation failure for a regression. Malformed arguments
 *         and a short output buffer also return `ztick_status_invalid_argument`.
 * @note Invalid arguments that reach a live session record a human-readable session
 *       error; a NULL or destroyed session has no state in which to record one. The
 *       envelope is NUL-terminated and copied into @p out_json even when the mapped
 *       status is non-zero. When the buffer is too small, @p out_length receives the
 *       required byte count and no partial copy is made. This additive operation
 *       leaves @ref ztick_frontend_compare semantics and every struct layout unchanged.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_compare_with_policy(
    ztick_frontend_session *session, const char *baseline_path, const char *candidate_path,
    const char *threshold_percent, const char *alpha, int32_t sided, int32_t fail_on_regression,
    char *out_json, size_t capacity, size_t *out_length);

/**
 * @brief Run `compare` for two stored results and copy its JSON envelope.
 * @param session Session to use; must not be NULL.
 * @param baseline_path NUL-terminated baseline path; must not be NULL.
 * @param candidate_path NUL-terminated candidate path; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code, with regression mapped to
 *         `ztick_status_validation_failure`.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_compare(ztick_frontend_session *session,
                                                  const char *baseline_path,
                                                  const char *candidate_path, char *out_json,
                                                  size_t capacity, size_t *out_length);

/**
 * @brief Read one stored result document as compact JSON.
 * @param session Session to use; must not be NULL.
 * @param result_path NUL-terminated result path; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Success, or an explicit status explaining why the document is unavailable.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_result_read(ztick_frontend_session *session,
                                                      const char *result_path, char *out_json,
                                                      size_t capacity, size_t *out_length);

/**
 * @brief Export one stored result document as CSV text.
 * @param session Session to use; must not be NULL.
 * @param result_path NUL-terminated result path; must not be NULL.
 * @param row_mode `per_test_point` or `per_metric_sample`; NULL selects
 *        `per_test_point`.
 * @param out_csv Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_csv.
 * @param out_length Optional receiver for the CSV length excluding the NUL.
 * @return Success, `ztick_status_invalid_argument` for malformed arguments or an
 *         unknown row mode, or an explicit failure when the document is unavailable.
 * @note No metric is recomputed in the front end; the stored document is projected.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_result_export_csv(ztick_frontend_session *session,
                                                            const char *result_path,
                                                            const char *row_mode, char *out_csv,
                                                            size_t capacity, size_t *out_length);

/**
 * @brief Run `plugin list` and copy its JSON envelope into a caller-owned buffer.
 * @param session Session to use; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_plugin_list(ztick_frontend_session *session,
                                                      char *out_json, size_t capacity,
                                                      size_t *out_length);

/**
 * @brief Run metadata-only `plugin inspect` and copy its JSON envelope.
 * @param session Session to use; must not be NULL.
 * @param plugin_path NUL-terminated plugin path; must not be NULL.
 * @param out_json Destination buffer; must not be NULL when @p capacity is non-zero.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the JSON length excluding the NUL.
 * @return Mapped `ztick_status` from the CLI exit code.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_plugin_inspect(ztick_frontend_session *session,
                                                         const char *plugin_path, char *out_json,
                                                         size_t capacity, size_t *out_length);

/**
 * @brief Query whether one stable plugin identifier is trusted.
 * @param session Session to use; must not be NULL.
 * @param identifier NUL-terminated stable identifier; must not be NULL or empty.
 * @param out_trusted Receives one when trusted, otherwise zero; must not be NULL.
 * @return Success, `ztick_status_invalid_argument` for malformed arguments, or
 *         `ztick_status_processing_failure` when the trust store cannot be read.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_trust_query(ztick_frontend_session *session,
                                                      const char *identifier, int32_t *out_trusted);

/**
 * @brief Record an explicit first-run consent decision for one plugin identifier.
 * @param session Session to use; must not be NULL.
 * @param identifier NUL-terminated stable identifier; must not be NULL or empty.
 * @param granted One to approve and persist trust, zero to deny; no other value is
 *        accepted.
 * @return Success for either explicit decision, `ztick_status_invalid_argument` for
 *         malformed arguments, or an explicit failure when the store cannot be updated.
 * @note A denial never authorizes execution and never writes to the trust store.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_trust_consent(ztick_frontend_session *session,
                                                        const char *identifier, int32_t granted);

/**
 * @brief Register one progress callback for a session.
 * @param session Session to use; must not be NULL.
 * @param callback C function pointer; NULL clears the subscription.
 * @param user_data Opaque context passed to @p callback unchanged.
 * @return Success, or `ztick_status_invalid_argument` for a NULL session.
 * @note Subsequent events invoke the callback with copied, borrowed event structs.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_progress_subscribe(
    ztick_frontend_session *session, ztick_frontend_progress_callback callback, void *user_data);

/**
 * @brief Copy the next queued progress event.
 * @param session Session to use; must not be NULL.
 * @param out_event Caller-owned event with an initialized header; must not be NULL.
 * @param out_available Receives one when an event was copied, otherwise zero; must
 *        not be NULL.
 * @return Success, or `ztick_status_invalid_argument` for malformed arguments.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_progress_poll(ztick_frontend_session *session,
                                                        ztick_frontend_event *out_event,
                                                        int32_t *out_available);

/**
 * @brief Drain queued progress events through one callback.
 * @param session Session to use; must not be NULL.
 * @param callback C function pointer; must not be NULL.
 * @param user_data Opaque context passed to @p callback unchanged.
 * @param out_count Optional receiver for the number of delivered events.
 * @return Success, or `ztick_status_invalid_argument` for malformed arguments.
 * @note The queue is cleared before callbacks run; callbacks receive copied events.
 */
ZTICK_FRONTEND_API int32_t ztick_frontend_progress_drain(ztick_frontend_session *session,
                                                         ztick_frontend_progress_callback callback,
                                                         void *user_data, int32_t *out_count);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZTICK_FRONTEND_H
