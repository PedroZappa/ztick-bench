// SPDX-License-Identifier: MIT
//! Rust smoke test for every symbol in the private `ztick_frontend` C boundary.
//!
//! The test is registered with CTest by CMake. It proves that Rust can link the
//! C-linkage facade, call every operation, read copied strings and events and
//! destroy the session without crossing a C++ type or exception.

#![deny(missing_docs)]

use std::ffi::{CStr, CString, c_char, c_void};
use std::process;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Duration;

use ztick_tui::*;

/// Number of progress callbacks observed by the smoke test.
static CALLBACK_COUNT: AtomicUsize = AtomicUsize::new(0);

/// A complete, runnable v1 config document used by the parse, save and load checks.
const CONFIG_JSON: &str = r#"{"config_version":"1.0.0","sdk_version":"1.0.0","integration":{"id":"app-a","version":"1.2.3"},"workload":{"level":"processor","processors":["eq"],"generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,"phase_rad":0.0}},"dimensions":{"sample_rate_hz":48000.0,"block_size":128,"channels":2,"oversampling":1,"instances":1},"execution":{"duration_mode":"iterations","iterations":100,"duration_seconds":0.0,"repetitions":2,"warmup":{"iterations":0},"isolation":"auto"},"profile":{"name":"Quick"},"measurement":{"detail":"aggregate","end_to_end":false,"timing_levels":["per_block","whole_run"]},"validation":{"level":"Basic","tolerances":{},"references":[]},"parallelism":{"workers":"auto","affinity":"auto"}}"#;

/// Receive one copied event and count it.
///
/// # Safety
/// The C boundary promises a valid event pointer for the callback duration.
unsafe extern "C" fn count_event(_event: *const ZtickFrontendEvent, _user_data: *mut c_void) {
    CALLBACK_COUNT.fetch_add(1, Ordering::SeqCst);
}

/// Convert a Rust string into a NUL-terminated C string for an FFI call.
fn c_string(value: &str) -> CString {
    CString::new(value).expect("test strings contain no interior NUL")
}

/// Call a string-returning boundary function and copy its output into Rust.
///
/// # Safety
/// The caller supplies a boundary function that writes at most `capacity` bytes
/// into the provided buffer and NUL-terminates it.
unsafe fn collect<F>(call: F) -> (i32, String)
where
    F: FnOnce(*mut c_char, usize, *mut usize) -> i32,
{
    let mut buffer = vec![0_i8; 131072];
    let mut length = 0_usize;
    let status = call(buffer.as_mut_ptr(), buffer.len(), &mut length);
    let text = unsafe { CStr::from_ptr(buffer.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    (status, text)
}

/// Read the session's recorded error as a Rust string.
///
/// # Safety
/// The session handle must be live and owned by this test.
unsafe fn last_error(session: *mut ZtickFrontendSession) -> String {
    unsafe {
        collect(|buffer, capacity, out_length| {
            ztick_frontend_last_error(session, buffer, capacity, out_length)
        })
        .1
    }
}

/// Assert that a borrowed C string matches its expected token.
///
/// # Safety
/// The pointer must come from a stable boundary string.
unsafe fn expect_token(pointer: *const c_char, expected: &str) {
    assert_eq!(
        unsafe { CStr::from_ptr(pointer) }.to_string_lossy(),
        expected
    );
}

/// Build a unique temporary path for one smoke-test artifact.
fn temp_path(label: &str) -> String {
    format!("/tmp/ztick_frontend_rust_{}_{}", process::id(), label)
}

/// Exercise every private front-end symbol through Rust FFI.
#[test]
fn rust_ffi_calls_every_private_boundary_symbol() {
    CALLBACK_COUNT.store(0, Ordering::SeqCst);
    let config_path = temp_path("config.json");
    let result_path = temp_path("config.result.json");
    let trust_path = temp_path("trust.list");
    let missing_path = temp_path("missing.json");

    unsafe {
        let mut version = ZtickFrontendVersion {
            size: size_of::<ZtickFrontendVersion>() as u32,
            version: 1,
            major: 0,
            minor: 0,
            patch: 0,
        };
        assert_eq!(ztick_frontend_get_version(&mut version), 0);
        assert_eq!((version.major, version.minor, version.patch), (0, 2, 0));
        expect_token(ztick_frontend_status_message(3), "invalid argument");

        let trust = c_string(&trust_path);
        let options = ZtickFrontendSessionOptions {
            size: size_of::<ZtickFrontendSessionOptions>() as u32,
            version: 1,
            trust_store_path: trust.as_ptr(),
        };
        let session = ztick_frontend_session_create(&options);
        assert!(!session.is_null());
        assert_eq!(last_error(session), "");
        assert_eq!(
            ztick_frontend_progress_subscribe(session, Some(count_event), std::ptr::null_mut()),
            0
        );

        let config = c_string(CONFIG_JSON);
        assert_eq!(
            ztick_frontend_config_parse(session, config.as_ptr(), config.as_bytes().len()),
            0
        );
        assert_eq!(ztick_frontend_config_validate(session), 0);
        let override_value = c_string("dimensions.channels=4");
        let assignments = [override_value.as_ptr()];
        let (override_status, overridden) = collect(|buffer, capacity, out_length| {
            ztick_frontend_config_override(
                session,
                assignments.as_ptr(),
                1,
                buffer,
                capacity,
                out_length,
            )
        });
        assert_eq!(override_status, 0);
        assert!(overridden.contains("\"channels\":4"));
        let (serialize_status, serialized) = collect(|buffer, capacity, out_length| {
            ztick_frontend_config_serialize(session, buffer, capacity, out_length)
        });
        assert_eq!(serialize_status, 0);
        assert!(serialized.contains("config_version"));

        let config_file = c_string(&config_path);
        assert_eq!(ztick_frontend_config_save(session, config_file.as_ptr()), 0);
        assert_eq!(ztick_frontend_config_load(session, config_file.as_ptr()), 0);

        let (list_status, list_json) = collect(|buffer, capacity, out_length| {
            ztick_frontend_list(session, buffer, capacity, out_length)
        });
        let (system_status, _system_json) = collect(|buffer, capacity, out_length| {
            ztick_frontend_system(session, buffer, capacity, out_length)
        });
        let (plugin_list_status, _plugin_json) = collect(|buffer, capacity, out_length| {
            ztick_frontend_plugin_list(session, buffer, capacity, out_length)
        });
        assert_eq!(list_status, 0);
        assert_eq!(system_status, 0);
        assert_eq!(plugin_list_status, 0);
        assert!(list_json.contains("data"));

        let missing = c_string(&missing_path);
        let (run_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_run(session, missing.as_ptr(), buffer, capacity, out_length)
        });
        let (report_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_report(session, missing.as_ptr(), buffer, capacity, out_length)
        });
        let (compare_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_compare(
                session,
                missing.as_ptr(),
                missing.as_ptr(),
                buffer,
                capacity,
                out_length,
            )
        });
        let (result_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_result_read(session, missing.as_ptr(), buffer, capacity, out_length)
        });
        let (inspect_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_plugin_inspect(session, missing.as_ptr(), buffer, capacity, out_length)
        });
        assert_ne!(run_status, 0);
        assert_ne!(report_status, 0);
        assert_ne!(compare_status, 0);
        assert_ne!(result_status, 0);
        assert_ne!(inspect_status, 0);

        let result_file = c_string(&result_path);
        let (good_run_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_run(session, config_file.as_ptr(), buffer, capacity, out_length)
        });
        let (good_report_status, report_json) = collect(|buffer, capacity, out_length| {
            ztick_frontend_report(session, result_file.as_ptr(), buffer, capacity, out_length)
        });
        let (good_compare_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_compare(
                session,
                result_file.as_ptr(),
                result_file.as_ptr(),
                buffer,
                capacity,
                out_length,
            )
        });
        let (good_result_status, result_json) = collect(|buffer, capacity, out_length| {
            ztick_frontend_result_read(session, result_file.as_ptr(), buffer, capacity, out_length)
        });
        let (csv_status, csv_text) = collect(|buffer, capacity, out_length| {
            ztick_frontend_result_export_csv(
                session,
                result_file.as_ptr(),
                c"per_test_point".as_ptr(),
                buffer,
                capacity,
                out_length,
            )
        });
        let unknown_mode = c_string("unknown-mode");
        let (bad_mode_status, _) = collect(|buffer, capacity, out_length| {
            ztick_frontend_result_export_csv(
                session,
                result_file.as_ptr(),
                unknown_mode.as_ptr(),
                buffer,
                capacity,
                out_length,
            )
        });
        assert_eq!(good_run_status, 0);
        assert_eq!(good_report_status, 0);
        assert!(report_json.contains("data"));
        assert_eq!(good_compare_status, 0);
        assert_eq!(good_result_status, 0);
        assert!(result_json.contains("config_version"));
        assert_eq!(csv_status, 0);
        assert!(csv_text.contains("session_id"));
        assert_eq!(bad_mode_status, 3);

        let identifier = c_string("rust.ffi.smoke.plugin");
        let mut trusted = 1_i32;
        assert_eq!(
            ztick_frontend_trust_query(session, identifier.as_ptr(), &mut trusted),
            0
        );
        assert_eq!(trusted, 0);
        assert_eq!(
            ztick_frontend_trust_consent(session, identifier.as_ptr(), 1),
            0
        );
        assert_eq!(
            ztick_frontend_trust_query(session, identifier.as_ptr(), &mut trusted),
            0
        );
        assert_eq!(trusted, 1);
        assert_eq!(
            ztick_frontend_trust_consent(session, identifier.as_ptr(), 0),
            0
        );

        let mut event = ZtickFrontendEvent {
            size: size_of::<ZtickFrontendEvent>() as u32,
            version: 1,
            kind: 0,
            state: 0,
            test_point: 0,
            timing_level: 0,
            isolation_mode: 0,
            message: [0; ZTICK_FRONTEND_EVENT_MESSAGE_SIZE],
        };
        let mut available = 0_i32;
        assert_eq!(
            ztick_frontend_progress_poll(session, &mut event, &mut available),
            0
        );
        assert_eq!(available, 1);
        assert!(event.kind >= ZTICK_FRONTEND_EVENT_STATE_CHANGE);
        assert_eq!(event.timing_level, ZTICK_FRONTEND_TIMING_UNKNOWN);
        assert_eq!(event.isolation_mode, ZTICK_FRONTEND_ISOLATION_UNKNOWN);
        let mut drained = 0_i32;
        assert_eq!(
            ztick_frontend_progress_drain(
                session,
                Some(count_event),
                std::ptr::null_mut(),
                &mut drained
            ),
            0
        );
        assert!(drained > 0);
        let mut after_drain = 0_i32;
        assert_eq!(
            ztick_frontend_progress_poll(session, &mut event, &mut after_drain),
            0
        );
        assert_eq!(after_drain, 0);
        assert!(CALLBACK_COUNT.load(Ordering::SeqCst) > 0);

        assert_eq!(
            ztick_frontend_run_start(session, std::ptr::null()),
            ZTICK_STATUS_INVALID_ARGUMENT
        );
        assert_eq!(ztick_frontend_run_start(session, config_file.as_ptr()), 0);
        let mut result_buffer = vec![0_i8; 131072];
        let mut result_length = 0_usize;
        let mut run_available = 0_i32;
        for _ in 0..500 {
            let status = ztick_frontend_run_result(
                session,
                result_buffer.as_mut_ptr(),
                result_buffer.len(),
                &mut result_length,
                &mut run_available,
            );
            assert_eq!(status, 0);
            if run_available == 1 {
                break;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(run_available, 1);
        let async_json = CStr::from_ptr(result_buffer.as_ptr())
            .to_string_lossy()
            .into_owned();
        assert!(async_json.contains("\"command\":\"run\""));
        assert!(async_json.contains("\"status\":\"success\""));

        ztick_frontend_session_destroy(session);
        assert_eq!(ztick_frontend_config_validate(session), 3);
        ztick_frontend_session_destroy(session);

        let replacement = ztick_frontend_session_create(&options);
        assert!(!replacement.is_null());
        assert_ne!(replacement, session);
        assert_eq!(ztick_frontend_config_validate(session), 3);
        assert_eq!(ztick_frontend_config_validate(replacement), 3);
        ztick_frontend_session_destroy(replacement);
    }

    let _ = std::fs::remove_file(config_path);
    let _ = std::fs::remove_file(result_path);
    let _ = std::fs::remove_file(trust_path);
}
