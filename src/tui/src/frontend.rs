// SPDX-License-Identifier: MIT
//! Safe ownership and string conversion around the private C11 front end.
//!
//! The raw declarations in the crate root remain the only FFI surface. This
//! module keeps the opaque handle private, destroys it deterministically and
//! turns non-zero `ztick_status` values into owned Rust errors.

use std::error::Error;
use std::ffi::{CStr, CString, c_char};
use std::fmt;
use std::ptr;

use crate::data::{DashboardData, PluginInspection};
use crate::{
    ZTICK_FRONTEND_EVENT_MESSAGE_SIZE, ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION, ZtickFrontendEvent,
    ZtickFrontendSession, ZtickFrontendSessionOptions, ztick_frontend_config_validate,
    ztick_frontend_last_error, ztick_frontend_list, ztick_frontend_plugin_inspect,
    ztick_frontend_plugin_list, ztick_frontend_progress_poll, ztick_frontend_session_create,
    ztick_frontend_session_destroy, ztick_frontend_system,
};

/// Status returned by every successful private-boundary operation.
const STATUS_SUCCESS: i32 = 0;
/// Local status used when a successful boundary call returns undecodable data.
const STATUS_DATA_ERROR: i32 = -2;
/// Capacity used for compact JSON envelopes returned by the front end.
const JSON_CAPACITY: usize = 64 * 1024;
/// Capacity used for copied session error messages.
const ERROR_CAPACITY: usize = 1024;

/// An owned error produced by one private-boundary call.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FrontendError {
    /// The raw or local status code that caused the error.
    status: i32,
    /// The owned human-readable explanation.
    message: String,
}

/// Construct and inspect owned boundary errors.
impl FrontendError {
    /// Construct an error from a boundary status and an owned explanation.
    ///
    /// # Parameters
    /// - `status`: Raw `ztick_status` value returned by the boundary.
    /// - `message`: Human-readable explanation copied from the session or
    ///   produced by the wrapper.
    pub fn new(status: i32, message: impl Into<String>) -> Self {
        Self {
            status,
            message: message.into(),
        }
    }

    /// Construct an error for data that could not be interpreted locally.
    ///
    /// # Parameters
    /// - `detail`: Explanation produced by the envelope reader.
    ///
    /// # Returns
    /// A local error that never masquerades as a C boundary status.
    pub fn data(detail: impl Into<String>) -> Self {
        Self::new(
            STATUS_DATA_ERROR,
            format!("front-end data could not be decoded: {}", detail.into()),
        )
    }

    /// Return the raw `ztick_status` value that caused this error.
    pub fn status(&self) -> i32 {
        self.status
    }

    /// Return the owned explanation associated with this error.
    pub fn message(&self) -> &str {
        &self.message
    }
}

/// Format a boundary error for display.
impl fmt::Display for FrontendError {
    /// Write the error or value into the supplied formatter.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "front-end call failed with status {}: {}",
            self.status, self.message
        )
    }
}

/// Mark boundary errors as standard errors.
impl Error for FrontendError {}

/// Read-only dashboard data copied from the front end.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Snapshot {
    /// The copied `system` envelope.
    system: String,
    /// The copied `list` envelope.
    integrations: String,
    /// The copied `plugin list` envelope.
    plugins: String,
}

/// Construct and inspect copied JSON envelopes.
impl Snapshot {
    /// Construct a snapshot from copied JSON envelopes.
    ///
    /// # Parameters
    /// - `system`: JSON returned by the `system` operation.
    /// - `integrations`: JSON returned by the `list` operation.
    /// - `plugins`: JSON returned by the `plugin list` operation.
    pub fn new(system: String, integrations: String, plugins: String) -> Self {
        Self {
            system,
            integrations,
            plugins,
        }
    }

    /// Return the copied `system` envelope.
    pub fn system(&self) -> &str {
        &self.system
    }

    /// Return the copied `list` envelope.
    pub fn integrations(&self) -> &str {
        &self.integrations
    }

    /// Return the copied `plugin list` envelope.
    pub fn plugins(&self) -> &str {
        &self.plugins
    }
}

/// An owned session handle for the private front end.
///
/// The handle is destroyed when the session leaves scope. A session is neither
/// `Send` nor `Sync` because the C boundary owns its state and does not document
/// cross-thread use of one handle.
#[derive(Debug)]
pub struct Session {
    /// The opaque front-end session handle owned by this value.
    handle: *mut ZtickFrontendSession,
}

/// Own the front-end handle and call read-only operations.
impl Session {
    /// Create one front-end session using platform defaults.
    ///
    /// # Returns
    /// The owned session, or an error when construction fails.
    pub fn new() -> Result<Self, FrontendError> {
        let options = ZtickFrontendSessionOptions {
            size: size_of::<ZtickFrontendSessionOptions>() as u32,
            version: ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION,
            trust_store_path: ptr::null(),
        };
        let handle = unsafe { ztick_frontend_session_create(&options) };
        if handle.is_null() {
            return Err(FrontendError::new(
                -1,
                "session creation returned a NULL handle",
            ));
        }
        Ok(Self { handle })
    }

    /// Revalidate the retained config and report any boundary error.
    ///
    /// # Returns
    /// Success when a valid config is retained, otherwise the copied session
    /// error.
    pub fn validate_config(&self) -> Result<(), FrontendError> {
        let status = unsafe { ztick_frontend_config_validate(self.handle) };
        if status == STATUS_SUCCESS {
            Ok(())
        } else {
            Err(FrontendError::new(status, self.last_error()))
        }
    }

    /// Copy the dashboard's read-only envelopes from the front end.
    ///
    /// # Returns
    /// A snapshot containing the `system`, `list` and `plugin list` JSON.
    pub fn snapshot(&self) -> Result<Snapshot, FrontendError> {
        Ok(Snapshot::new(
            self.call_json(|session, buffer, capacity, length| unsafe {
                ztick_frontend_system(session, buffer, capacity, length)
            })?,
            self.call_json(|session, buffer, capacity, length| unsafe {
                ztick_frontend_list(session, buffer, capacity, length)
            })?,
            self.call_json(|session, buffer, capacity, length| unsafe {
                ztick_frontend_plugin_list(session, buffer, capacity, length)
            })?,
        ))
    }

    /// Copy and decode all read-only inputs used by the dashboard screens.
    ///
    /// Each envelope is copied independently, so one unreadable section cannot
    /// erase the others. Both boundary copy errors and envelope interpretation
    /// errors remain visible through [`DashboardData::load_error`].
    ///
    /// # Returns
    /// Parsed system inventory, profile list and plugin list, with every failure
    /// recorded rather than returning early.
    pub fn dashboard_data(&self) -> Result<DashboardData, FrontendError> {
        let system = self.call_json(|session, buffer, capacity, length| unsafe {
            ztick_frontend_system(session, buffer, capacity, length)
        });
        let integrations = self.call_json(|session, buffer, capacity, length| unsafe {
            ztick_frontend_list(session, buffer, capacity, length)
        });
        let plugins = self.call_json(|session, buffer, capacity, length| unsafe {
            ztick_frontend_plugin_list(session, buffer, capacity, length)
        });
        let mut data = DashboardData::from_json(
            system.as_deref().unwrap_or_default(),
            integrations.as_deref().unwrap_or_default(),
            plugins.as_deref().unwrap_or_default(),
        );
        if let Err(error) = &system {
            data.record_load_error("system", error);
        }
        if let Err(error) = &integrations {
            data.record_load_error("list", error);
        }
        if let Err(error) = &plugins {
            data.record_load_error("plugin list", error);
        }
        Ok(data)
    }

    /// Inspect one plugin descriptor without executing plugin code.
    ///
    /// # Parameters
    /// - `path`: Filesystem path of the discovered candidate.
    ///
    /// # Returns
    /// The core compatibility result and copied metadata, or a boundary or
    /// decoding error.
    pub fn plugin_inspect(&self, path: &str) -> Result<PluginInspection, FrontendError> {
        let path = CString::new(path)
            .map_err(|_| FrontendError::new(-1, "plugin path contains an interior NUL byte"))?;
        let json = self.call_json(|session, buffer, capacity, length| unsafe {
            ztick_frontend_plugin_inspect(session, path.as_ptr(), buffer, capacity, length)
        })?;
        PluginInspection::from_json(&json).map_err(|error| FrontendError::data(error.message()))
    }

    /// Copy the next queued progress event without discarding the queue.
    ///
    /// The event header is initialized with the layout values required by the
    /// private boundary before the borrowed event is copied.
    ///
    /// # Returns
    /// `Ok(Some(event))` for the oldest queued event, `Ok(None)` when no event
    /// is available, or an owned boundary error.
    pub fn poll_event(&self) -> Result<Option<ZtickFrontendEvent>, FrontendError> {
        let mut event = ZtickFrontendEvent {
            size: size_of::<ZtickFrontendEvent>() as u32,
            version: ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION,
            kind: 0,
            state: 0,
            test_point: 0,
            timing_level: 0,
            isolation_mode: 0,
            message: [0; ZTICK_FRONTEND_EVENT_MESSAGE_SIZE],
        };
        let mut available = 0_i32;
        let status =
            unsafe { ztick_frontend_progress_poll(self.handle, &mut event, &mut available) };
        if status != STATUS_SUCCESS {
            return Err(FrontendError::new(status, self.last_error()));
        }
        Ok(if available == 0 { None } else { Some(event) })
    }

    /// Call one string-returning boundary operation and copy its output.
    ///
    /// A failed CLI command still copies a versioned JSON envelope that carries
    /// explicit availability records. Such an envelope is returned despite the
    /// non-zero status; only a boundary failure with no copied envelope becomes
    /// [`FrontendError`].
    fn call_json<F>(&self, call: F) -> Result<String, FrontendError>
    where
        F: FnOnce(*mut ZtickFrontendSession, *mut c_char, usize, *mut usize) -> i32,
    {
        let mut buffer: Vec<c_char> = vec![0; JSON_CAPACITY];
        let mut length = 0_usize;
        let status = call(self.handle, buffer.as_mut_ptr(), buffer.len(), &mut length);
        if length >= buffer.len() {
            return Err(FrontendError::new(
                status,
                "front-end output exceeded the copy buffer",
            ));
        }
        let text = unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        if text.is_empty() {
            return Err(FrontendError::new(status, self.last_error()));
        }
        Ok(text)
    }

    /// Copy the session's most recent error into an owned Rust string.
    fn last_error(&self) -> String {
        let mut buffer: Vec<c_char> = vec![0; ERROR_CAPACITY];
        let mut length = 0_usize;
        let status = unsafe {
            ztick_frontend_last_error(self.handle, buffer.as_mut_ptr(), buffer.len(), &mut length)
        };
        if status != STATUS_SUCCESS {
            return format!("last-error read failed with status {status}");
        }
        if length >= buffer.len() {
            return "last error exceeded the copy buffer".to_string();
        }
        let message = unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        if message.is_empty() {
            "front-end operation failed".to_string()
        } else {
            message
        }
    }
}

/// Destroy the owned front-end session deterministically.
impl Drop for Session {
    /// Release the owned opaque session handle.
    fn drop(&mut self) {
        unsafe { ztick_frontend_session_destroy(self.handle) };
    }
}

/// Unit tests for the safe front-end wrapper and live envelopes.
#[cfg(test)]
mod tests {
    use std::ffi::CString;

    use super::{FrontendError, Session, Snapshot};
    use crate::{ztick_frontend_config_parse, ztick_frontend_session_destroy};

    /// A complete v1 config used to exercise the successful validation path.
    const CONFIG_JSON: &str = r#"{"config_version":"1.0.0","sdk_version":"1.0.0","integration":{"id":"app-a","version":"1.2.3"},"workload":{"level":"processor","processors":["eq"],"generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,"phase_rad":0.0}},"dimensions":{"sample_rate_hz":48000.0,"block_size":128,"channels":2,"oversampling":1,"instances":1},"execution":{"duration_mode":"iterations","iterations":100,"duration_seconds":0.0,"repetitions":2,"warmup":{"iterations":0},"isolation":"auto"},"profile":{"name":"Quick"},"measurement":{"detail":"aggregate","end_to_end":false,"timing_levels":["per_block","whole_run"]},"validation":{"level":"Basic","tolerances":{},"references":[]},"parallelism":{"workers":"auto","affinity":"auto"}}"#;

    /// Verify error accessors and display carry status and message.
    #[test]
    fn error_accessors_and_display_carry_status_and_message() {
        let error = FrontendError::new(3, "invalid argument");
        assert_eq!(error.status(), 3);
        assert_eq!(error.message(), "invalid argument");
        assert_eq!(
            error.to_string(),
            "front-end call failed with status 3: invalid argument"
        );
    }

    /// Verify data error uses a distinct local status.
    #[test]
    fn data_error_uses_a_distinct_local_status() {
        let error = FrontendError::data("bad envelope");
        assert_ne!(error.status(), 0);
        assert!(error.message().contains("bad envelope"));
    }

    /// Verify snapshot accessors return each envelope.
    #[test]
    fn snapshot_accessors_return_each_envelope() {
        let snapshot = Snapshot::new("system".into(), "list".into(), "plugins".into());
        assert_eq!(snapshot.system(), "system");
        assert_eq!(snapshot.integrations(), "list");
        assert_eq!(snapshot.plugins(), "plugins");
    }

    /// Verify session validate config reports boundary error.
    #[test]
    fn session_validate_config_reports_boundary_error() {
        let session = Session::new().expect("session creation succeeds");
        let error = session
            .validate_config()
            .expect_err("a new session has no retained config");
        assert_ne!(error.status(), 0);
        assert!(!error.message().is_empty());
    }

    /// Verify session validate config accepts a parsed config.
    #[test]
    fn session_validate_config_accepts_a_parsed_config() {
        let session = Session::new().expect("session creation succeeds");
        let config = CString::new(CONFIG_JSON).expect("config contains no interior NUL");
        let status = unsafe {
            ztick_frontend_config_parse(session.handle, config.as_ptr(), config.as_bytes().len())
        };
        assert_eq!(status, 0);
        assert!(session.validate_config().is_ok());
    }

    /// Verify session empty last error uses a visible fallback.
    #[test]
    fn session_empty_last_error_uses_a_visible_fallback() {
        let session = Session::new().expect("session creation succeeds");
        assert_eq!(session.last_error(), "front-end operation failed");
    }

    /// Verify session snapshot reads all dashboard envelopes.
    #[test]
    fn session_snapshot_reads_all_dashboard_envelopes() {
        let session = Session::new().expect("session creation succeeds");
        let snapshot = session.snapshot().expect("dashboard views are available");
        assert!(snapshot.system().contains("data"));
        assert!(snapshot.integrations().contains("data"));
        assert!(snapshot.plugins().contains("data"));
    }

    /// Verify session snapshot reports a destroyed handle error.
    #[test]
    fn session_snapshot_reports_a_destroyed_handle_error() {
        let session = Session::new().expect("session creation succeeds");
        unsafe { ztick_frontend_session_destroy(session.handle) };
        let error = session
            .snapshot()
            .expect_err("a destroyed handle cannot copy envelopes");
        assert_ne!(error.status(), 0);
        assert!(!error.message().is_empty());
    }

    /// Verify session dashboard data decodes live envelopes.
    #[test]
    fn session_dashboard_data_decodes_live_envelopes() {
        let session = Session::new().expect("session creation succeeds");
        let data = session
            .dashboard_data()
            .expect("all three dashboard envelopes decode");
        assert_eq!(data.load_error(), "");
        assert!(!data.system().fields().is_empty());
        assert!(!data.profiles().profiles().is_empty());
        assert!(!data.plugins().directories().is_empty());
    }

    /// Verify session plugin inspect returns core compatibility.
    #[test]
    fn session_plugin_inspect_returns_core_compatibility() {
        let session = Session::new().expect("session creation succeeds");
        let path = concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../build/ci/lib/ztick_test_app_dynamic.so"
        );
        let inspection = session
            .plugin_inspect(path)
            .expect("the built test adapter can be inspected");
        assert_eq!(inspection.path(), path);
        assert_eq!(inspection.compatibility().status(), "success");
        assert!(!inspection.capabilities().is_empty());
    }

    /// Verify session plugin inspect rejects an interior nul path.
    #[test]
    fn session_plugin_inspect_rejects_an_interior_nul_path() {
        let session = Session::new().expect("session creation succeeds");
        let error = session
            .plugin_inspect("bad\0path")
            .expect_err("interior NUL cannot cross the C boundary");
        assert_ne!(error.status(), 0);
    }

    /// Verify progress polling drains boundary events to an empty queue.
    #[test]
    fn session_progress_poll_drains_boundary_events() {
        use crate::ZTICK_FRONTEND_EVENT_STATE_CHANGE;

        let session = Session::new().expect("session creation succeeds");
        assert!(
            session
                .poll_event()
                .expect("a fresh session has an empty queue")
                .is_none()
        );
        let _ = session
            .dashboard_data()
            .expect("dashboard envelopes are copied");
        let first = session
            .poll_event()
            .expect("progress poll succeeds")
            .expect("boundary calls enqueue events");
        assert_eq!(first.kind, ZTICK_FRONTEND_EVENT_STATE_CHANGE);
        let mut count = 1;
        while session
            .poll_event()
            .expect("progress poll succeeds")
            .is_some()
        {
            count += 1;
            assert!(count < 32, "the queue must eventually drain");
        }
    }
}
