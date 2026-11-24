// SPDX-License-Identifier: MIT
//! Application state shared by front-end event handling and rendering.
//!
//! P5.2 adds the three read-only screens and their selection state. Detail
//! editing, live run monitoring and result workflows remain in later phases; no
//! metric computation belongs here.

use crate::data::{DashboardData, PluginCandidate, PluginInspection};
use crate::{
    ZTICK_FRONTEND_EVENT_WARNING, ZTICK_FRONTEND_STATE_COMPLETE, ZTICK_FRONTEND_STATE_CONFIGURING,
    ZTICK_FRONTEND_STATE_FAILED, ZTICK_FRONTEND_STATE_IDLE, ZTICK_FRONTEND_STATE_INCOMPLETE,
    ZTICK_FRONTEND_STATE_RUNNING, ZTICK_FRONTEND_STATE_VALIDATING, ZTICK_FRONTEND_STATE_WARNING,
    ZtickFrontendEvent,
};

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

/// Map boundary state codes to stable display labels.
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

/// Read-only screens available in Phase 5.2.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum Screen {
    /// Orientation summary and active run state.
    #[default]
    Dashboard,
    /// Full system inventory with explicit availability states.
    System,
    /// Discovered plugins, trust state and metadata-only inspection.
    Integrations,
}

/// Provide screen titles and cyclic navigation.
impl Screen {
    /// Return the title rendered in the header for this screen.
    pub fn title(self) -> &'static str {
        match self {
            Self::Dashboard => "Dashboard",
            Self::System => "System inventory",
            Self::Integrations => "Integrations / plugins",
        }
    }

    /// Return the screen reached by moving forward once.
    pub fn next(self) -> Self {
        match self {
            Self::Dashboard => Self::System,
            Self::System => Self::Integrations,
            Self::Integrations => Self::Dashboard,
        }
    }

    /// Return the screen reached by moving backward once.
    pub fn previous(self) -> Self {
        match self {
            Self::Dashboard => Self::Integrations,
            Self::System => Self::Dashboard,
            Self::Integrations => Self::System,
        }
    }
}

/// Mutable state for the terminal application.
#[derive(Debug, Clone)]
pub struct App {
    /// The active run state rendered by the dashboard.
    state: RunState,
    /// The currently selected read-only screen.
    screen: Screen,
    /// The read-only envelope data copied from the front end.
    data: DashboardData,
    /// The zero-based index of the selected plugin candidate.
    selected_plugin: usize,
    /// The metadata-only inspection loaded for the selected candidate.
    inspection: Option<PluginInspection>,
    /// The warning messages retained in arrival order.
    warnings: Vec<String>,
    /// The most recent status message shown on the dashboard.
    status: String,
}

/// Provide the idle default application state.
impl Default for App {
    /// Build an idle application with no loaded dashboard data.
    fn default() -> Self {
        Self {
            state: RunState::Idle,
            screen: Screen::Dashboard,
            data: DashboardData::default(),
            selected_plugin: 0,
            inspection: None,
            warnings: Vec::new(),
            status: "Ready".to_string(),
        }
    }
}

/// Mutate and inspect the terminal application state.
impl App {
    /// Construct an idle application with no loaded dashboard data.
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

    /// Return the active screen.
    pub fn screen(&self) -> Screen {
        self.screen
    }

    /// Select one screen explicitly.
    pub fn set_screen(&mut self, screen: Screen) {
        self.screen = screen;
    }

    /// Move to the next screen in dashboard order.
    pub fn next_screen(&mut self) {
        self.screen = self.screen.next();
    }

    /// Move to the previous screen in dashboard order.
    pub fn previous_screen(&mut self) {
        self.screen = self.screen.previous();
    }

    /// Return all copied dashboard inputs.
    pub fn data(&self) -> &DashboardData {
        &self.data
    }

    /// Replace all copied dashboard inputs and keep the current selection in range.
    pub fn set_dashboard_data(&mut self, data: DashboardData) {
        self.selected_plugin = self
            .selected_plugin
            .min(data.plugins().plugins().len().saturating_sub(1));
        self.inspection = None;
        self.data = data;
    }

    /// Return the index of the selected plugin candidate.
    pub fn selected_plugin_index(&self) -> usize {
        self.selected_plugin
    }

    /// Return the selected plugin candidate, if any are available.
    pub fn selected_plugin(&self) -> Option<&PluginCandidate> {
        self.data.plugins().plugins().get(self.selected_plugin)
    }

    /// Select the next plugin candidate with wrap-around.
    pub fn select_next_plugin(&mut self) {
        let count = self.data.plugins().plugins().len();
        if count == 0 {
            return;
        }
        self.selected_plugin = (self.selected_plugin + 1) % count;
        self.inspection = None;
    }

    /// Select the previous plugin candidate with wrap-around.
    pub fn select_previous_plugin(&mut self) {
        let count = self.data.plugins().plugins().len();
        if count == 0 {
            return;
        }
        self.selected_plugin = (self.selected_plugin + count - 1) % count;
        self.inspection = None;
    }

    /// Return the inspection loaded for the selected candidate.
    pub fn inspection(&self) -> Option<&PluginInspection> {
        self.inspection.as_ref()
    }

    /// Replace the inspection loaded for the selected candidate.
    pub fn set_inspection(&mut self, inspection: Option<PluginInspection>) {
        self.inspection = inspection;
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

/// Unit tests for run-state mapping and application state transitions.
#[cfg(test)]
mod tests {
    use std::ffi::c_char;

    use super::{App, RunState, Screen, event_message};
    use crate::data::{DashboardData, PluginInspection};
    use crate::{
        ZTICK_FRONTEND_EVENT_COMPLETION, ZTICK_FRONTEND_EVENT_MESSAGE_SIZE,
        ZTICK_FRONTEND_EVENT_STATE_CHANGE, ZTICK_FRONTEND_EVENT_WARNING,
        ZTICK_FRONTEND_STATE_COMPLETE, ZTICK_FRONTEND_STATE_CONFIGURING,
        ZTICK_FRONTEND_STATE_FAILED, ZTICK_FRONTEND_STATE_IDLE, ZTICK_FRONTEND_STATE_INCOMPLETE,
        ZTICK_FRONTEND_STATE_RUNNING, ZTICK_FRONTEND_STATE_VALIDATING,
        ZTICK_FRONTEND_STATE_WARNING, ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION, ZtickFrontendEvent,
    };

    /// Build one boundary event with a copied context message.
    fn event(kind: i32, state: i32, message: &str) -> ZtickFrontendEvent {
        let mut bytes = [0 as c_char; ZTICK_FRONTEND_EVENT_MESSAGE_SIZE];
        for (slot, value) in bytes.iter_mut().zip(message.as_bytes()) {
            *slot = *value as c_char;
        }
        ZtickFrontendEvent {
            size: size_of::<ZtickFrontendEvent>() as u32,
            version: ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION,
            kind,
            state,
            test_point: 0,
            timing_level: 0,
            isolation_mode: 0,
            message: bytes,
        }
    }

    /// Build dashboard data with the requested number of plugin candidates.
    fn data_with_plugins(count: usize) -> DashboardData {
        let plugins = format!(
            r#"{{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{{"directories":[],"trust":{{"status":"available","reason":"","entries":[]}},"plugins":[{}]}},"error":""}}"#,
            (0..count)
                .map(|index| {
                    format!(
                        r#"{{"path":"/app{index}.so","source":"explicit_path","status":"success","reason":"","identifier":"app-{index}","name":"App {index}","version":"1.0.0","sdk_version":"1.0.0","capabilities":"0x7c","trust":"untrusted"}}"#
                    )
                })
                .collect::<Vec<_>>()
                .join(",")
        );
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[]},"error":""}"#;
        let system = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{}},"error":""}"#;
        DashboardData::from_json(system, list, &plugins)
    }

    /// Verify run state maps all known codes and rejects unknown codes.
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

    /// Verify run state labels are distinct.
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

    /// Verify screen cycles forward and backward.
    #[test]
    fn screen_cycles_forward_and_backward() {
        assert_eq!(Screen::Dashboard.next(), Screen::System);
        assert_eq!(Screen::System.next(), Screen::Integrations);
        assert_eq!(Screen::Integrations.next(), Screen::Dashboard);
        assert_eq!(Screen::Dashboard.previous(), Screen::Integrations);
        assert_eq!(Screen::Integrations.previous(), Screen::System);
        assert_eq!(Screen::System.title(), "System inventory");
    }

    /// Verify app accessors return initial and replaced values.
    #[test]
    fn app_accessors_return_initial_and_replaced_values() {
        let mut app = App::new();
        assert_eq!(app.state(), RunState::Idle);
        assert_eq!(app.screen(), Screen::Dashboard);
        assert_eq!(app.status(), "Ready");
        assert!(app.warnings().is_empty());
        assert_eq!(app.data(), &DashboardData::default());

        app.set_state_code(ZTICK_FRONTEND_STATE_VALIDATING);
        app.set_screen(Screen::System);
        app.set_status("checking capabilities");
        app.push_warning("unstable timing");
        let data = data_with_plugins(1);
        app.set_dashboard_data(data.clone());

        assert_eq!(app.state(), RunState::Validating);
        assert_eq!(app.screen(), Screen::System);
        assert_eq!(app.status(), "checking capabilities");
        assert_eq!(app.warnings(), ["unstable timing"]);
        assert_eq!(app.data(), &data);
    }

    /// Verify screen navigation helpers move without losing data.
    #[test]
    fn screen_navigation_helpers_move_without_losing_data() {
        let mut app = App::new();
        app.set_dashboard_data(data_with_plugins(2));
        app.next_screen();
        assert_eq!(app.screen(), Screen::System);
        app.next_screen();
        assert_eq!(app.screen(), Screen::Integrations);
        app.previous_screen();
        assert_eq!(app.screen(), Screen::System);
        assert_eq!(app.data().plugins().plugins().len(), 2);
    }

    /// Verify plugin selection wraps and clears stale inspection.
    #[test]
    fn plugin_selection_wraps_and_clears_stale_inspection() {
        let mut app = App::new();
        app.set_dashboard_data(data_with_plugins(2));
        let inspection = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app0.so","status":"success","reason":"","metadata":{"identifier":"app-0","name":"App 0","version":"1.0.0","sdk_version":"1.0.0","capabilities":"0x7c","metadata":""},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_inspection(Some(inspection));
        assert!(app.inspection().is_some());

        app.select_next_plugin();
        assert_eq!(app.selected_plugin_index(), 1);
        assert_eq!(
            app.selected_plugin().map(|plugin| plugin.path()),
            Some("/app1.so")
        );
        assert!(app.inspection().is_none());

        app.select_previous_plugin();
        assert_eq!(app.selected_plugin_index(), 0);
    }

    /// Verify plugin selection without candidates is a no op.
    #[test]
    fn plugin_selection_without_candidates_is_a_no_op() {
        let mut app = App::new();
        app.select_next_plugin();
        app.select_previous_plugin();
        assert_eq!(app.selected_plugin_index(), 0);
        assert!(app.selected_plugin().is_none());
    }

    /// Verify replacing data clamps the selection.
    #[test]
    fn replacing_data_clamps_the_selection() {
        let mut app = App::new();
        app.set_dashboard_data(data_with_plugins(3));
        app.select_next_plugin();
        app.select_next_plugin();
        assert_eq!(app.selected_plugin_index(), 2);
        app.set_dashboard_data(data_with_plugins(1));
        assert_eq!(app.selected_plugin_index(), 0);
    }

    /// Verify event message stops at first nul.
    #[test]
    fn event_message_stops_at_first_nul() {
        let value = event(
            ZTICK_FRONTEND_EVENT_COMPLETION,
            ZTICK_FRONTEND_STATE_IDLE,
            "visible",
        );
        assert_eq!(event_message(&value), "visible");
    }

    /// Verify observe event updates state status and warnings.
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

    /// Verify observe event records empty warning with placeholder.
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
