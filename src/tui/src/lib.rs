// SPDX-License-Identifier: MIT
//! Raw FFI bindings for the private `ztick_frontend` C11 boundary.
//!
//! These declarations mirror `src/frontend/include/ztick/frontend.h` exactly.
//! They are the only path from the Rust TUI to core, results and host services;
//! no C++ type or exception crosses the boundary.

#![deny(missing_docs)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

/// Owned run-state model for the terminal application.
pub mod app;
/// Portable v1 config defaults and field-edit helpers.
pub mod config;
/// Typed, read-only views over front-end JSON envelopes.
pub mod data;
/// Safe ownership of the private C11 front end.
pub mod frontend;
/// Minimal JSON reader used by the typed envelope views.
mod json;
/// Named profile persistence as portable config documents.
pub mod profile;
/// Stateless ratatui rendering for the dashboard and configuration screens.
pub mod ui;

/// Successful status returned by a private-boundary operation.
pub const ZTICK_STATUS_SUCCESS: i32 = 0;
/// Requested capability, format or value is not supported.
pub const ZTICK_STATUS_UNSUPPORTED: i32 = 1;
/// Adapter SDK compatibility check failed.
pub const ZTICK_STATUS_INCOMPATIBLE: i32 = 2;
/// One or more arguments were invalid.
pub const ZTICK_STATUS_INVALID_ARGUMENT: i32 = 3;
/// A processing operation, such as reading a file, failed.
pub const ZTICK_STATUS_PROCESSING_FAILURE: i32 = 4;
/// Validation check failed outside the timed interval.
pub const ZTICK_STATUS_VALIDATION_FAILURE: i32 = 5;
/// An internal error with no more specific status.
pub const ZTICK_STATUS_INTERNAL_ERROR: i32 = 6;

/// Workload-level capability declared by an adapter.
pub const ZTICK_CAPABILITY_PROCESSOR: u64 = 1 << 0;
/// Processor-chain capability declared by an adapter.
pub const ZTICK_CAPABILITY_CHAIN: u64 = 1 << 1;
/// Complete-engine capability declared by an adapter.
pub const ZTICK_CAPABILITY_ENGINE: u64 = 1 << 2;
/// Per-block timing capability declared by an adapter.
pub const ZTICK_CAPABILITY_PER_BLOCK_TIMING: u64 = 1 << 3;
/// Internal threading capability declared by an adapter.
pub const ZTICK_CAPABILITY_INTERNAL_THREADING: u64 = 1 << 4;
/// Reference-data capability declared by an adapter.
pub const ZTICK_CAPABILITY_REFERENCE_DATA: u64 = 1 << 5;
/// Custom validation capability declared by an adapter.
pub const ZTICK_CAPABILITY_CUSTOM_VALIDATION: u64 = 1 << 6;

/// Layout version required in the header of every copied boundary struct.
pub const ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION: u32 = 1;
/// Maximum message bytes carried by one copied progress event.
pub const ZTICK_FRONTEND_EVENT_MESSAGE_SIZE: usize = 256;
/// No run is active.
pub const ZTICK_FRONTEND_STATE_IDLE: i32 = 0;
/// Settings are being edited.
pub const ZTICK_FRONTEND_STATE_CONFIGURING: i32 = 1;
/// Capability and config checks are in progress.
pub const ZTICK_FRONTEND_STATE_VALIDATING: i32 = 2;
/// Timed execution is active.
pub const ZTICK_FRONTEND_STATE_RUNNING: i32 = 3;
/// An instability warning was recorded.
pub const ZTICK_FRONTEND_STATE_WARNING: i32 = 4;
/// A workload or infrastructure failure was classified.
pub const ZTICK_FRONTEND_STATE_FAILED: i32 = 5;
/// The run ended with failures or an interruption.
pub const ZTICK_FRONTEND_STATE_INCOMPLETE: i32 = 6;
/// Every configured test finished successfully.
pub const ZTICK_FRONTEND_STATE_COMPLETE: i32 = 7;
/// The active run state changed.
pub const ZTICK_FRONTEND_EVENT_STATE_CHANGE: i32 = 0;
/// A progress update for one test point.
pub const ZTICK_FRONTEND_EVENT_PROGRESS: i32 = 1;
/// A warning was recorded while a run was active.
pub const ZTICK_FRONTEND_EVENT_WARNING: i32 = 2;
/// A command finished and its output is available.
pub const ZTICK_FRONTEND_EVENT_COMPLETION: i32 = 3;
/// No timing level is active.
pub const ZTICK_FRONTEND_TIMING_UNKNOWN: i32 = 0;
/// Per-block timing is active.
pub const ZTICK_FRONTEND_TIMING_PER_BLOCK: i32 = 1;
/// Whole-run timing is active.
pub const ZTICK_FRONTEND_TIMING_WHOLE_RUN: i32 = 2;
/// Explicitly enabled end-to-end timing is active.
pub const ZTICK_FRONTEND_TIMING_END_TO_END: i32 = 3;
/// No isolation mode is active.
pub const ZTICK_FRONTEND_ISOLATION_UNKNOWN: i32 = 0;
/// The integration runs in the host process.
pub const ZTICK_FRONTEND_ISOLATION_IN_PROCESS: i32 = 1;
/// The integration runs out of process.
pub const ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS: i32 = 2;

/// Version of the private front-end interface.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ZtickFrontendVersion {
    /// Byte size of this struct, set by the caller.
    pub size: u32,
    /// Struct layout version, set by the caller.
    pub version: u32,
    /// Interface major version.
    pub major: u32,
    /// Interface minor version.
    pub minor: u32,
    /// Interface patch version.
    pub patch: u32,
}

/// Optional session construction settings.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ZtickFrontendSessionOptions {
    /// Byte size of this struct, set by the caller.
    pub size: u32,
    /// Struct layout version, set by the caller.
    pub version: u32,
    /// Optional trusted-plugin store path, or NULL for the platform default.
    pub trust_store_path: *const c_char,
}

/// One copied progress event delivered to the front end.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ZtickFrontendEvent {
    /// Byte size of this struct, set by the caller.
    pub size: u32,
    /// Struct layout version, set by the caller.
    pub version: u32,
    /// Event kind code.
    pub kind: i32,
    /// Active run state code.
    pub state: i32,
    /// One-based test point, or zero when not applicable.
    pub test_point: u32,
    /// Timing level code.
    pub timing_level: i32,
    /// Isolation mode code.
    pub isolation_mode: i32,
    /// NUL-terminated human-readable context.
    pub message: [c_char; ZTICK_FRONTEND_EVENT_MESSAGE_SIZE],
}

/// Opaque front-end session handle owned by the caller.
#[repr(C)]
pub struct ZtickFrontendSession {
    /// Opaque storage; never read or written by Rust.
    _private: [u8; 0],
}

/// Callback receiving one copied progress event.
pub type ZtickFrontendProgressCallback =
    Option<unsafe extern "C" fn(event: *const ZtickFrontendEvent, user_data: *mut c_void)>;

#[link(name = "ztick_frontend", kind = "dylib")]
unsafe extern "C" {
    /// Query the private front-end interface version.
    pub fn ztick_frontend_get_version(out_version: *mut ZtickFrontendVersion) -> i32;

    /// Read a stable message for one `ztick_status` code.
    pub fn ztick_frontend_status_message(status: i32) -> *const c_char;

    /// Create one front-end session.
    ///
    /// Handle values are never reused; a destroyed handle cannot alias a later
    /// session.
    pub fn ztick_frontend_session_create(
        options: *const ZtickFrontendSessionOptions,
    ) -> *mut ZtickFrontendSession;

    /// Destroy one front-end session.
    ///
    /// This operation is idempotent; every later operation on the handle
    /// returns `ztick_status_invalid_argument`.
    pub fn ztick_frontend_session_destroy(session: *mut ZtickFrontendSession);

    /// Copy the most recent session error into a caller-owned buffer.
    pub fn ztick_frontend_last_error(
        session: *const ZtickFrontendSession,
        buffer: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Parse and retain one v1 config document from JSON text.
    pub fn ztick_frontend_config_parse(
        session: *mut ZtickFrontendSession,
        text: *const c_char,
        length: usize,
    ) -> i32;

    /// Load and parse one v1 config document from a file.
    pub fn ztick_frontend_config_load(
        session: *mut ZtickFrontendSession,
        path: *const c_char,
    ) -> i32;

    /// Revalidate the config retained by a session.
    pub fn ztick_frontend_config_validate(session: *mut ZtickFrontendSession) -> i32;

    /// Apply dotted-path overrides to the retained config and copy its JSON.
    pub fn ztick_frontend_config_override(
        session: *mut ZtickFrontendSession,
        assignments: *const *const c_char,
        count: usize,
        buffer: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Serialize the retained config as compact JSON.
    pub fn ztick_frontend_config_serialize(
        session: *mut ZtickFrontendSession,
        buffer: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Atomically save the retained config as a v1 JSON document.
    pub fn ztick_frontend_config_save(
        session: *mut ZtickFrontendSession,
        path: *const c_char,
    ) -> i32;

    /// Run `list` and copy its JSON envelope.
    pub fn ztick_frontend_list(
        session: *mut ZtickFrontendSession,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Run `system` and copy its JSON envelope.
    pub fn ztick_frontend_system(
        session: *mut ZtickFrontendSession,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Run `run` for one config file and copy its JSON envelope.
    pub fn ztick_frontend_run(
        session: *mut ZtickFrontendSession,
        config_path: *const c_char,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Run `report` for one stored result and copy its JSON envelope.
    pub fn ztick_frontend_report(
        session: *mut ZtickFrontendSession,
        result_path: *const c_char,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Run `compare` for two stored results and copy its JSON envelope.
    pub fn ztick_frontend_compare(
        session: *mut ZtickFrontendSession,
        baseline_path: *const c_char,
        candidate_path: *const c_char,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Read one stored result document as compact JSON.
    pub fn ztick_frontend_result_read(
        session: *mut ZtickFrontendSession,
        result_path: *const c_char,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Export one stored result document as CSV text.
    pub fn ztick_frontend_result_export_csv(
        session: *mut ZtickFrontendSession,
        result_path: *const c_char,
        row_mode: *const c_char,
        out_csv: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Run `plugin list` and copy its JSON envelope.
    pub fn ztick_frontend_plugin_list(
        session: *mut ZtickFrontendSession,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Run metadata-only `plugin inspect` and copy its JSON envelope.
    pub fn ztick_frontend_plugin_inspect(
        session: *mut ZtickFrontendSession,
        plugin_path: *const c_char,
        out_json: *mut c_char,
        capacity: usize,
        out_length: *mut usize,
    ) -> i32;

    /// Query whether one stable plugin identifier is trusted.
    pub fn ztick_frontend_trust_query(
        session: *mut ZtickFrontendSession,
        identifier: *const c_char,
        out_trusted: *mut i32,
    ) -> i32;

    /// Record an explicit first-run consent decision for one plugin identifier.
    pub fn ztick_frontend_trust_consent(
        session: *mut ZtickFrontendSession,
        identifier: *const c_char,
        granted: i32,
    ) -> i32;

    /// Register one progress callback for a session.
    pub fn ztick_frontend_progress_subscribe(
        session: *mut ZtickFrontendSession,
        callback: ZtickFrontendProgressCallback,
        user_data: *mut c_void,
    ) -> i32;

    /// Copy the next queued progress event.
    pub fn ztick_frontend_progress_poll(
        session: *mut ZtickFrontendSession,
        out_event: *mut ZtickFrontendEvent,
        out_available: *mut i32,
    ) -> i32;

    /// Drain queued progress events through one callback.
    pub fn ztick_frontend_progress_drain(
        session: *mut ZtickFrontendSession,
        callback: ZtickFrontendProgressCallback,
        user_data: *mut c_void,
        out_count: *mut i32,
    ) -> i32;
}
