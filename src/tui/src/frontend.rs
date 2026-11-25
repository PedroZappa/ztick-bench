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
    ZtickFrontendSession, ZtickFrontendSessionOptions, ztick_frontend_config_load,
    ztick_frontend_config_override, ztick_frontend_config_parse, ztick_frontend_config_save,
    ztick_frontend_config_serialize, ztick_frontend_config_validate, ztick_frontend_last_error,
    ztick_frontend_list, ztick_frontend_plugin_inspect, ztick_frontend_plugin_list,
    ztick_frontend_progress_poll, ztick_frontend_result_read, ztick_frontend_run_result,
    ztick_frontend_run_start, ztick_frontend_session_create, ztick_frontend_session_destroy,
    ztick_frontend_system,
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

    /// Parse, semantically validate and retain one portable config document.
    ///
    /// # Parameters
    /// - `text`: Complete config JSON text.
    ///
    /// # Returns
    /// The serialized config retained by the front end, or the copied error.
    /// A document rejected by the core planner is never retained.
    pub fn parse_config(&self, text: &str) -> Result<String, FrontendError> {
        let config = CString::new(text)
            .map_err(|_| FrontendError::new(-1, "config contains an interior NUL byte"))?;
        let status = unsafe {
            ztick_frontend_config_parse(self.handle, config.as_ptr(), config.as_bytes().len())
        };
        if status != STATUS_SUCCESS {
            return Err(FrontendError::new(status, self.last_error()));
        }
        self.serialize_config()
    }

    /// Load and parse one portable config document from a file.
    ///
    /// # Parameters
    /// - `path`: Filesystem path of a versioned config document.
    ///
    /// # Returns
    /// The serialized config retained by the front end, or the copied error.
    pub fn load_config(&self, path: &str) -> Result<String, FrontendError> {
        let path = CString::new(path)
            .map_err(|_| FrontendError::new(-1, "config path contains an interior NUL byte"))?;
        let status = unsafe { ztick_frontend_config_load(self.handle, path.as_ptr()) };
        if status != STATUS_SUCCESS {
            return Err(FrontendError::new(status, self.last_error()));
        }
        self.serialize_config()
    }

    /// Apply and semantically validate dotted `path=value` assignments.
    ///
    /// # Parameters
    /// - `assignments`: Overrides in application order.
    ///
    /// # Returns
    /// The serialized config after all assignments, or the copied error. The
    /// boundary applies overrides atomically, so a rejected assignment or a
    /// core-invalid result leaves the retained config unchanged.
    pub fn apply_overrides(&self, assignments: &[String]) -> Result<String, FrontendError> {
        let owned: Vec<CString> = assignments
            .iter()
            .map(|assignment| {
                CString::new(assignment.as_str())
                    .map_err(|_| FrontendError::new(-1, "override contains an interior NUL byte"))
            })
            .collect::<Result<_, _>>()?;
        let pointers: Vec<*const c_char> = owned.iter().map(|value| value.as_ptr()).collect();
        self.call_config_string(|session, buffer, capacity, length| unsafe {
            ztick_frontend_config_override(
                session,
                pointers.as_ptr(),
                pointers.len(),
                buffer,
                capacity,
                length,
            )
        })
    }

    /// Copy the retained config as compact JSON.
    ///
    /// # Returns
    /// The serialized config, or the copied boundary error.
    pub fn serialize_config(&self) -> Result<String, FrontendError> {
        self.call_config_string(|session, buffer, capacity, length| unsafe {
            ztick_frontend_config_serialize(session, buffer, capacity, length)
        })
    }

    /// Atomically save the retained config to a file.
    ///
    /// # Parameters
    /// - `path`: Destination config path.
    ///
    /// # Returns
    /// Success, or the copied boundary error.
    pub fn save_config(&self, path: &str) -> Result<(), FrontendError> {
        let path = CString::new(path)
            .map_err(|_| FrontendError::new(-1, "config path contains an interior NUL byte"))?;
        let status = unsafe { ztick_frontend_config_save(self.handle, path.as_ptr()) };
        if status == STATUS_SUCCESS {
            Ok(())
        } else {
            Err(FrontendError::new(status, self.last_error()))
        }
    }

    /// Start one asynchronous run from a saved portable config document.
    ///
    /// # Parameters
    /// - `path`: Filesystem path of a previously saved config document.
    ///
    /// # Returns
    /// Success once the session-owned worker has started, or the copied boundary
    /// error. Core progress is delivered through [`Session::poll_event`].
    pub fn start_run(&self, path: &str) -> Result<(), FrontendError> {
        let path = CString::new(path)
            .map_err(|_| FrontendError::new(-1, "run config path contains an interior NUL byte"))?;
        let status = unsafe { ztick_frontend_run_start(self.handle, path.as_ptr()) };
        if status == STATUS_SUCCESS {
            Ok(())
        } else {
            Err(FrontendError::new(status, self.last_error()))
        }
    }

    /// Copy the result envelope of the most recent asynchronous run.
    ///
    /// # Returns
    /// `Ok(None)` while the worker is active, `Ok(Some(json))` for a completed
    /// run whose JSON envelope is available even when the run failed, or a
    /// boundary error when no envelope was copied.
    pub fn run_result(&self) -> Result<Option<String>, FrontendError> {
        let mut buffer: Vec<c_char> = vec![0; JSON_CAPACITY];
        let mut length = 0_usize;
        let mut available = 0_i32;
        let status = unsafe {
            ztick_frontend_run_result(
                self.handle,
                buffer.as_mut_ptr(),
                buffer.len(),
                &mut length,
                &mut available,
            )
        };
        if available == 0 {
            return Ok(None);
        }
        if length >= buffer.len() {
            return Err(FrontendError::new(
                status,
                "front-end run result exceeded the copy buffer",
            ));
        }
        let text = unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        if text.is_empty() {
            return Err(FrontendError::new(status, self.last_error()));
        }
        Ok(Some(text))
    }

    /// Read one stored result document as compact JSON through the private boundary.
    ///
    /// The copied text is the stored v1 result document, not a CLI envelope.
    /// Decoding it into a typed view is the caller's responsibility.
    ///
    /// # Parameters
    /// - `path`: Filesystem path of a stored result document.
    ///
    /// # Returns
    /// The compact result document, or a boundary or copy error.
    pub fn result_read(&self, path: &str) -> Result<String, FrontendError> {
        let path = CString::new(path)
            .map_err(|_| FrontendError::new(-1, "result path contains an interior NUL byte"))?;
        self.call_config_string(|session, buffer, capacity, length| unsafe {
            ztick_frontend_result_read(session, path.as_ptr(), buffer, capacity, length)
        })
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

    /// Call one config copy operation and require a successful status.
    ///
    /// Config operations return plain JSON rather than a CLI envelope, so a
    /// non-zero status carries no usable payload and is reported immediately.
    fn call_config_string<F>(&self, call: F) -> Result<String, FrontendError>
    where
        F: FnOnce(*mut ZtickFrontendSession, *mut c_char, usize, *mut usize) -> i32,
    {
        let mut buffer: Vec<c_char> = vec![0; JSON_CAPACITY];
        let mut length = 0_usize;
        let status = call(self.handle, buffer.as_mut_ptr(), buffer.len(), &mut length);
        if status != STATUS_SUCCESS {
            return Err(FrontendError::new(status, self.last_error()));
        }
        if length >= buffer.len() {
            return Err(FrontendError::new(
                status,
                "front-end config output exceeded the copy buffer",
            ));
        }
        let text = unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        if text.is_empty() {
            return Err(FrontendError::new(
                status,
                "front-end returned an empty config",
            ));
        }
        Ok(text)
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
    use std::time::Duration;

    use super::{FrontendError, Session, Snapshot};
    use crate::profile;
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
        let path = std::env::var("ZTICK_TUI_TEST_MODULE").unwrap_or_else(|_| {
            format!(
                "{}/../../build/lib/ztick_test_app_dynamic.{}",
                env!("CARGO_MANIFEST_DIR"),
                std::env::consts::DLL_EXTENSION
            )
        });
        let inspection = session
            .plugin_inspect(&path)
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

    /// Verify parse override validate save and load use one retained config.
    #[test]
    fn session_config_operations_round_trip_one_document() {
        use crate::config::DEFAULT_CONFIG_JSON;
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        let parsed = session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("the default config parses");
        assert!(parsed.contains("\"config_version\":\"1.0.0\""));
        session
            .validate_config()
            .expect("the default config validates");

        let updated = session
            .apply_overrides(&["profile.name=\"Saved\"".to_string()])
            .expect("a documented override applies");
        assert!(updated.contains("\"name\":\"Saved\""));
        session
            .validate_config()
            .expect("an overridden config still validates");

        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-config-roundtrip-{nonce}"));
        std::fs::create_dir_all(&directory).expect("temporary directory is created");
        let path = directory.join("config.json");
        let path_text = path.to_string_lossy().to_string();
        session
            .save_config(&path_text)
            .expect("the retained config is saved");
        let reloaded = Session::new().expect("reload session is created");
        let loaded = reloaded
            .load_config(&path_text)
            .expect("the saved config loads");
        assert!(loaded.contains("\"name\":\"Saved\""));
        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }

    /// Verify invalid config operations report errors without panicking.
    #[test]
    fn session_config_operations_reject_invalid_input() {
        use crate::config::DEFAULT_CONFIG_JSON;

        let session = Session::new().expect("session creation succeeds");
        assert!(session.parse_config("bad\0json").is_err());
        assert!(session.load_config("bad\0path").is_err());
        assert!(session.save_config("bad\0path").is_err());
        assert!(
            session
                .apply_overrides(&["bad\0override".to_string()])
                .is_err()
        );
        let semantic_invalid = DEFAULT_CONFIG_JSON.replace("\"channels\":2", "\"channels\":0");
        assert!(
            session.parse_config(&semantic_invalid).is_err(),
            "the core planner rejects zero channels at parse time"
        );
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("a valid config parses after rejected input");
        assert!(
            session
                .apply_overrides(&["missing.path=1".to_string()])
                .is_err()
        );
        let before = session
            .serialize_config()
            .expect("the valid config remains serialized");
        assert!(
            session
                .apply_overrides(&["dimensions.channels=0".to_string()])
                .is_err(),
            "a core-invalid override is rejected"
        );
        assert_eq!(
            session
                .serialize_config()
                .expect("the rejected override left a config"),
            before,
            "a rejected override must not mutate the retained config"
        );
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
        let _ = session
            .parse_config(CONFIG_JSON)
            .expect("config parsing enqueues a configuring event");
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

    /// Verify an invalid async path is rejected before it crosses the boundary.
    #[test]
    fn session_start_run_rejects_an_interior_nul_path() {
        let session = Session::new().expect("session creation succeeds");
        let error = session
            .start_run("bad\0path")
            .expect_err("an interior NUL cannot be passed to C");
        assert!(error.message().contains("interior NUL"));
    }

    /// Verify a fresh session reports no run result before a run is started.
    #[test]
    fn session_run_result_is_pending_before_a_run() {
        let session = Session::new().expect("session creation succeeds");
        assert!(
            session
                .run_result()
                .expect("a pending result is not an error")
                .is_none()
        );
    }

    /// Verify a result read rejects an interior NUL path before crossing the boundary.
    #[test]
    fn session_result_read_rejects_an_interior_nul_path() {
        let session = Session::new().expect("session creation succeeds");
        let error = session
            .result_read("bad\0path")
            .expect_err("an interior NUL cannot be passed to C");
        assert!(error.message().contains("interior NUL"));
    }

    /// Verify an async run delivers copied progress events and a JSON envelope.
    #[test]
    fn session_async_run_delivers_progress_and_result() {
        use crate::{ZTICK_FRONTEND_EVENT_COMPLETION, ZTICK_FRONTEND_STATE_RUNNING};

        let session = Session::new().expect("session creation succeeds");
        let directory = std::env::temp_dir().join(format!(
            "ztick-tui-async-{}-{}",
            std::process::id(),
            line!()
        ));
        std::fs::create_dir_all(&directory).expect("temporary profile directory is created");
        let path = profile::save(&directory, "Quick", CONFIG_JSON)
            .expect("the async config document is saved");
        let path = path.to_str().expect("temporary path is UTF-8");

        session.start_run(path).expect("the async run starts");
        let mut saw_running = false;
        let mut saw_completion = false;
        let mut result = None;
        for _ in 0..500 {
            while let Some(event) = session.poll_event().expect("progress poll succeeds") {
                saw_running |= event.state == ZTICK_FRONTEND_STATE_RUNNING;
                saw_completion |= event.kind == ZTICK_FRONTEND_EVENT_COMPLETION;
            }
            if let Some(json) = session.run_result().expect("result copy succeeds") {
                result = Some(json);
                break;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        assert!(saw_running, "core progress reported a running state");
        assert!(saw_completion, "core progress reported a terminal state");
        let json = result.expect("the async run produced a JSON envelope");
        assert!(json.contains("\"status\":\"success\""));
        assert!(json.contains("\"command\":\"run\""));

        let envelope = crate::json::parse(&json).expect("the envelope is valid JSON");
        let output_path = envelope
            .get("data")
            .and_then(|data| data.get("output_path"))
            .and_then(crate::json::Json::as_str)
            .expect("a successful run publishes an output path");
        let document = session
            .result_read(output_path)
            .expect("the stored result document is read through the safe wrapper");
        assert!(document.contains("\"schema_version\":\"1.0.0\""));
        assert!(document.contains("\"runs\""));
    }
}
