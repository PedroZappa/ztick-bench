// SPDX-License-Identifier: MIT
//! Safe ownership and string conversion around the private C11 front end.
//!
//! The raw declarations in the crate root remain the only FFI surface. This
//! module keeps the opaque handle private, destroys it deterministically and
//! turns non-zero `ztick_status` values into owned Rust errors.

use std::error::Error;
use std::ffi::{CStr, c_char};
use std::fmt;
use std::ptr;

use crate::{
    ZtickFrontendSession, ZtickFrontendSessionOptions, ztick_frontend_config_validate,
    ztick_frontend_last_error, ztick_frontend_list, ztick_frontend_plugin_list,
    ztick_frontend_session_create, ztick_frontend_session_destroy, ztick_frontend_system,
};

/// Status returned by every successful private-boundary operation.
const STATUS_SUCCESS: i32 = 0;
/// Capacity used for compact JSON envelopes returned by the front end.
const JSON_CAPACITY: usize = 64 * 1024;
/// Capacity used for copied session error messages.
const ERROR_CAPACITY: usize = 1024;

/// An owned error produced by one private-boundary call.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FrontendError {
    status: i32,
    message: String,
}

impl FrontendError {
    /// Construct an error from a boundary status and an owned explanation.
    ///
    /// @param status Raw `ztick_status` value returned by the boundary.
    /// @param message Human-readable explanation copied from the session or
    ///        produced by the wrapper.
    pub fn new(status: i32, message: impl Into<String>) -> Self {
        Self {
            status,
            message: message.into(),
        }
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

impl fmt::Display for FrontendError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "front-end call failed with status {}: {}",
            self.status, self.message
        )
    }
}

impl Error for FrontendError {}

/// Read-only dashboard data copied from the front end.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Snapshot {
    system: String,
    integrations: String,
    plugins: String,
}

impl Snapshot {
    /// Construct a snapshot from copied JSON envelopes.
    ///
    /// @param system JSON returned by the `system` operation.
    /// @param integrations JSON returned by the `list` operation.
    /// @param plugins JSON returned by the `plugin list` operation.
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
    handle: *mut ZtickFrontendSession,
}

impl Session {
    /// Create one front-end session using platform defaults.
    ///
    /// @return The owned session, or an error when construction fails.
    pub fn new() -> Result<Self, FrontendError> {
        let options = ZtickFrontendSessionOptions {
            size: size_of::<ZtickFrontendSessionOptions>() as u32,
            version: 1,
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
    /// @return Success when a valid config is retained, otherwise the copied
    ///         session error.
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
    /// @return A snapshot containing the `system`, `list` and `plugin list` JSON.
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

    /// Call one string-returning boundary operation and copy its output.
    fn call_json<F>(&self, call: F) -> Result<String, FrontendError>
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
                "front-end output exceeded the copy buffer",
            ));
        }
        Ok(unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned())
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

impl Drop for Session {
    fn drop(&mut self) {
        unsafe { ztick_frontend_session_destroy(self.handle) };
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::CString;

    use super::{FrontendError, Session, Snapshot};
    use crate::{ztick_frontend_config_parse, ztick_frontend_session_destroy};

    /// A complete v1 config used to exercise the successful validation path.
    const CONFIG_JSON: &str = r#"{"config_version":"1.0.0","sdk_version":"1.0.0","integration":{"id":"app-a","version":"1.2.3"},"workload":{"level":"processor","processors":["eq"],"generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,"phase_rad":0.0}},"dimensions":{"sample_rate_hz":48000.0,"block_size":128,"channels":2,"oversampling":1,"instances":1},"execution":{"duration_mode":"iterations","iterations":100,"duration_seconds":0.0,"repetitions":2,"warmup":{"iterations":0},"isolation":"auto"},"profile":{"name":"Quick"},"measurement":{"detail":"aggregate","end_to_end":false,"timing_levels":["per_block","whole_run"]},"validation":{"level":"Basic","tolerances":{},"references":[]},"parallelism":{"workers":"auto","affinity":"auto"}}"#;

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

    #[test]
    fn snapshot_accessors_return_each_envelope() {
        let snapshot = Snapshot::new("system".into(), "list".into(), "plugins".into());
        assert_eq!(snapshot.system(), "system");
        assert_eq!(snapshot.integrations(), "list");
        assert_eq!(snapshot.plugins(), "plugins");
    }

    #[test]
    fn session_validate_config_reports_boundary_error() {
        let session = Session::new().expect("session creation succeeds");
        let error = session
            .validate_config()
            .expect_err("a new session has no retained config");
        assert_ne!(error.status(), 0);
        assert!(!error.message().is_empty());
    }

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

    #[test]
    fn session_empty_last_error_uses_a_visible_fallback() {
        let session = Session::new().expect("session creation succeeds");
        assert_eq!(session.last_error(), "front-end operation failed");
    }

    #[test]
    fn session_snapshot_reads_all_dashboard_envelopes() {
        let session = Session::new().expect("session creation succeeds");
        let snapshot = session.snapshot().expect("dashboard views are available");
        assert!(snapshot.system().contains("data"));
        assert!(snapshot.integrations().contains("data"));
        assert!(snapshot.plugins().contains("data"));
    }

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
}
