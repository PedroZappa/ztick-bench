// SPDX-License-Identifier: MIT
//! Application state shared by front-end event handling and rendering.
//!
//! P5.1 establishes the state model only. Detail screens and workflow editing
//! are introduced by later Phase 5 tasks; no metric computation belongs here.

use crate::{
    ZTICK_FRONTEND_EVENT_WARNING, ZTICK_FRONTEND_STATE_COMPLETE, ZTICK_FRONTEND_STATE_CONFIGURING,
    ZTICK_FRONTEND_STATE_FAILED, ZTICK_FRONTEND_STATE_IDLE, ZTICK_FRONTEND_STATE_INCOMPLETE,
    ZTICK_FRONTEND_STATE_RUNNING, ZTICK_FRONTEND_STATE_VALIDATING, ZTICK_FRONTEND_STATE_WARNING,
    ZtickFrontendEvent,
};

use crate::frontend::Snapshot;

/// Run states the dashboard can display.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum RunState {
    /// No run is active.
    #[default]
    Idle,
    /// Settings are being edited.
    Configuring,
    /// Capability and config checks are in progress.
    Validating,
    /// Timed execution is active.
    Running,
    /// Instability was detected without rejecting the run.
    Warning,
    /// A workload or infrastructure failure was classified.
    Failed,
    /// The run ended with failures or an interruption.
    Incomplete,
    /// Every configured test finished successfully.
    Complete,
    /// The boundary reported an unrecognised state code.
    Unknown,
}

impl RunState {
    /// Map one private-boundary state code to a display state.
    ///
    /// Unrecognised codes become [`RunState::Unknown`] rather than being silently
    /// mapped to a successful state.
    pub fn from_code(code: i32) -> Self {
        match code {
            ZTICK_FRONTEND_STATE_IDLE => Self::Idle,
            ZTICK_FRONTEND_STATE_CONFIGURING => Self::Configuring,
            ZTICK_FRONTEND_STATE_VALIDATING => Self::Validating,
            ZTICK_FRONTEND_STATE_RUNNING => Self::Running,
            ZTICK_FRONTEND_STATE_WARNING => Self::Warning,
            ZTICK_FRONTEND_STATE_FAILED => Self::Failed,
            ZTICK_FRONTEND_STATE_INCOMPLETE => Self::Incomplete,
            ZTICK_FRONTEND_STATE_COMPLETE => Self::Complete,
            _ => Self::Unknown,
        }
    }

    /// Return the stable short label rendered for this state.
    pub fn label(self) -> &'static str {
        match self {
            Self::Idle => "Idle",
            Self::Configuring => "Configuring",
            Self::Validating => "Validating",
            Self::Running => "Running",
            Self::Warning => "Warning",
            Self::Failed => "Failed",
            Self::Incomplete => "Incomplete",
            Self::Complete => "Complete",
            Self::Unknown => "Unknown",
        }
    }
}

/// Mutable state for the terminal application.
#[derive(Debug, Clone)]
pub struct App {
    state: RunState,
    snapshot: Snapshot,
    warnings: Vec<String>,
    status: String,
}

impl Default for App {
    fn default() -> Self {
        Self {
            state: RunState::Idle,
            snapshot: Snapshot::default(),
            warnings: Vec::new(),
            status: "Ready".to_string(),
        }
    }
}

impl App {
    /// Construct an idle application with no loaded snapshot.
    pub fn new() -> Self {
        Self::default()
    }

    /// Return the currently displayed run state.
    pub fn state(&self) -> RunState {
        self.state
    }

    /// Replace the run state from a private-boundary state code.
    pub fn set_state_code(&mut self, code: i32) {
        self.state = RunState::from_code(code);
    }

    /// Return the copied read-only dashboard data.
    pub fn snapshot(&self) -> &Snapshot {
        &self.snapshot
    }

    /// Replace the copied read-only dashboard data.
    pub fn set_snapshot(&mut self, snapshot: Snapshot) {
        self.snapshot = snapshot;
    }

    /// Return warnings recorded from boundary events.
    pub fn warnings(&self) -> &[String] {
        &self.warnings
    }

    /// Append one warning in arrival order.
    pub fn push_warning(&mut self, warning: impl Into<String>) {
        self.warnings.push(warning.into());
    }

    /// Return the most recent status message.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Replace the most recent status message.
    pub fn set_status(&mut self, status: impl Into<String>) {
        self.status = status.into();
    }

    /// Apply one copied boundary event to the application state.
    ///
    /// State changes update the displayed state, every non-empty event message
    /// updates the status line, and warning events are retained for the dashboard.
    pub fn observe_event(&mut self, event: &ZtickFrontendEvent) {
        self.set_state_code(event.state);
        let message = event_message(event);
        if message.is_empty() {
            if event.kind == ZTICK_FRONTEND_EVENT_WARNING {
                self.push_warning("warning reported without context");
            }
            return;
        }
        self.set_status(message.clone());
        if event.kind == ZTICK_FRONTEND_EVENT_WARNING {
            self.push_warning(message);
        }
    }
}

/// Copy the NUL-terminated context carried by one boundary event.
fn event_message(event: &ZtickFrontendEvent) -> String {
    let end = event
        .message
        .iter()
        .position(|value| *value == 0)
        .unwrap_or(event.message.len());
    let bytes: Vec<u8> = event.message[..end]
        .iter()
        .map(|value| *value as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

#[cfg(test)]
mod tests {
    use std::ffi::c_char;

    use super::{App, RunState, event_message};
    use crate::frontend::Snapshot;
    use crate::{
        ZTICK_FRONTEND_EVENT_COMPLETION, ZTICK_FRONTEND_EVENT_MESSAGE_SIZE,
        ZTICK_FRONTEND_EVENT_STATE_CHANGE, ZTICK_FRONTEND_EVENT_WARNING,
        ZTICK_FRONTEND_STATE_COMPLETE, ZTICK_FRONTEND_STATE_CONFIGURING,
        ZTICK_FRONTEND_STATE_FAILED, ZTICK_FRONTEND_STATE_IDLE, ZTICK_FRONTEND_STATE_INCOMPLETE,
        ZTICK_FRONTEND_STATE_RUNNING, ZTICK_FRONTEND_STATE_VALIDATING,
        ZTICK_FRONTEND_STATE_WARNING, ZtickFrontendEvent,
    };

    /// Build one boundary event with a copied context message.
    fn event(kind: i32, state: i32, message: &str) -> ZtickFrontendEvent {
        let mut bytes = [0 as c_char; ZTICK_FRONTEND_EVENT_MESSAGE_SIZE];
        for (slot, value) in bytes.iter_mut().zip(message.as_bytes()) {
            *slot = *value as c_char;
        }
        ZtickFrontendEvent {
            size: size_of::<ZtickFrontendEvent>() as u32,
            version: 1,
            kind,
            state,
            test_point: 0,
            timing_level: 0,
            isolation_mode: 0,
            message: bytes,
        }
    }

    #[test]
    fn run_state_maps_all_known_codes_and_rejects_unknown_codes() {
        let cases = [
            (ZTICK_FRONTEND_STATE_IDLE, RunState::Idle),
            (ZTICK_FRONTEND_STATE_CONFIGURING, RunState::Configuring),
            (ZTICK_FRONTEND_STATE_VALIDATING, RunState::Validating),
            (ZTICK_FRONTEND_STATE_RUNNING, RunState::Running),
            (ZTICK_FRONTEND_STATE_WARNING, RunState::Warning),
            (ZTICK_FRONTEND_STATE_FAILED, RunState::Failed),
            (ZTICK_FRONTEND_STATE_INCOMPLETE, RunState::Incomplete),
            (ZTICK_FRONTEND_STATE_COMPLETE, RunState::Complete),
            (-1, RunState::Unknown),
            (99, RunState::Unknown),
        ];
        for (code, expected) in cases {
            assert_eq!(RunState::from_code(code), expected);
        }
    }

    #[test]
    fn run_state_labels_are_distinct() {
        let labels = [
            RunState::Idle.label(),
            RunState::Configuring.label(),
            RunState::Validating.label(),
            RunState::Running.label(),
            RunState::Warning.label(),
            RunState::Failed.label(),
            RunState::Incomplete.label(),
            RunState::Complete.label(),
            RunState::Unknown.label(),
        ];
        let unique: std::collections::HashSet<_> = labels.iter().collect();
        assert_eq!(unique.len(), labels.len());
    }

    #[test]
    fn app_accessors_return_initial_and_replaced_values() {
        let mut app = App::new();
        assert_eq!(app.state(), RunState::Idle);
        assert_eq!(app.status(), "Ready");
        assert!(app.warnings().is_empty());
        assert_eq!(app.snapshot(), &Snapshot::default());

        app.set_state_code(ZTICK_FRONTEND_STATE_VALIDATING);
        app.set_status("checking capabilities");
        app.push_warning("unstable timing");
        let snapshot = Snapshot::new("s".into(), "l".into(), "p".into());
        app.set_snapshot(snapshot.clone());

        assert_eq!(app.state(), RunState::Validating);
        assert_eq!(app.status(), "checking capabilities");
        assert_eq!(app.warnings(), ["unstable timing"]);
        assert_eq!(app.snapshot(), &snapshot);
    }

    #[test]
    fn event_message_stops_at_first_nul() {
        let value = event(
            ZTICK_FRONTEND_EVENT_COMPLETION,
            ZTICK_FRONTEND_STATE_IDLE,
            "visible",
        );
        assert_eq!(event_message(&value), "visible");
    }

    #[test]
    fn observe_event_updates_state_status_and_warnings() {
        let mut app = App::new();
        app.observe_event(&event(
            ZTICK_FRONTEND_EVENT_STATE_CHANGE,
            ZTICK_FRONTEND_STATE_RUNNING,
            "test point 2",
        ));
        assert_eq!(app.state(), RunState::Running);
        assert_eq!(app.status(), "test point 2");
        assert!(app.warnings().is_empty());

        app.observe_event(&event(
            ZTICK_FRONTEND_EVENT_WARNING,
            ZTICK_FRONTEND_STATE_WARNING,
            "instability detected",
        ));
        assert_eq!(app.state(), RunState::Warning);
        assert_eq!(app.status(), "instability detected");
        assert_eq!(app.warnings(), ["instability detected"]);
    }

    #[test]
    fn observe_event_records_empty_warning_with_placeholder() {
        let mut app = App::new();
        app.observe_event(&event(
            ZTICK_FRONTEND_EVENT_WARNING,
            ZTICK_FRONTEND_STATE_WARNING,
            "",
        ));
        assert_eq!(app.warnings(), ["warning reported without context"]);
        assert_eq!(app.status(), "Ready");
    }
}
