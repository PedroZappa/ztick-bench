// SPDX-License-Identifier: MIT
//! Application state shared by front-end event handling and rendering.
//!
//! P5.2 added the three read-only screens. P5.3 adds the editable config
//! screens, typed input state, named-profile selection and the portability gate.
//! Live run monitoring and result workflows remain in later phases; no metric
//! computation belongs here.

use std::path::PathBuf;

use crate::config::EditableField;
use crate::data::{DashboardData, PluginCandidate, PluginInspection, PortabilityReport};
use crate::profile::SavedProfile;
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

/// Screens available after Phase 5.3.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum Screen {
    /// Orientation summary and active run state.
    #[default]
    Dashboard,
    /// Full system inventory with explicit availability states.
    System,
    /// Discovered plugins, trust state and metadata-only inspection.
    Integrations,
    /// Workload level, processors and generator definition.
    Workload,
    /// Dimensions, profile identity and execution duration settings.
    Configuration,
    /// Measurement detail, timing and validation settings.
    Measurement,
    /// Parallelism, affinity, isolation and explicit priority opt-in.
    Scheduling,
    /// Built-in and saved named config profiles.
    Profiles,
}

/// Provide screen titles and cyclic navigation.
impl Screen {
    /// Return the title rendered in the header for this screen.
    pub fn title(self) -> &'static str {
        match self {
            Self::Dashboard => "Dashboard",
            Self::System => "System inventory",
            Self::Integrations => "Integrations / plugins",
            Self::Workload => "Workload",
            Self::Configuration => "Configuration",
            Self::Measurement => "Measurement & validation",
            Self::Scheduling => "Parallelism & scheduling",
            Self::Profiles => "Profiles & settings",
        }
    }

    /// Return all screens in navigation order.
    pub const ALL: [Self; 8] = [
        Self::Dashboard,
        Self::System,
        Self::Integrations,
        Self::Workload,
        Self::Configuration,
        Self::Measurement,
        Self::Scheduling,
        Self::Profiles,
    ];

    /// Return the screen reached by moving forward once.
    pub fn next(self) -> Self {
        let position = Self::ALL
            .iter()
            .position(|screen| *screen == self)
            .unwrap_or(0);
        Self::ALL[(position + 1) % Self::ALL.len()]
    }

    /// Return the screen reached by moving backward once.
    pub fn previous(self) -> Self {
        let position = Self::ALL
            .iter()
            .position(|screen| *screen == self)
            .unwrap_or(0);
        Self::ALL[(position + Self::ALL.len() - 1) % Self::ALL.len()]
    }
}

/// One active text edit started from an editable config field.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TextInput {
    /// The config field that receives the committed value.
    target: EditableField,
    /// The editable text buffer.
    buffer: String,
    /// The prompt shown while the editor is active.
    prompt: String,
}

/// Construct and inspect one active text edit.
impl TextInput {
    /// Start one text edit for a config field.
    ///
    /// # Parameters
    /// - `target`: Config field that receives the committed value.
    /// - `buffer`: Initial editor contents; empty for replacement edits.
    /// - `prompt`: Prompt shown above the editor while it is active.
    pub fn new(
        target: EditableField,
        buffer: impl Into<String>,
        prompt: impl Into<String>,
    ) -> Self {
        Self {
            target,
            buffer: buffer.into(),
            prompt: prompt.into(),
        }
    }

    /// Return the field that receives the committed value.
    pub fn target(&self) -> EditableField {
        self.target
    }

    /// Return the current text buffer.
    pub fn buffer(&self) -> &str {
        &self.buffer
    }

    /// Return the prompt rendered while the editor is active.
    pub fn prompt(&self) -> &str {
        &self.prompt
    }

    /// Append one character to the buffer.
    pub fn push(&mut self, character: char) {
        self.buffer.push(character);
    }

    /// Remove the final character from the buffer, when present.
    pub fn pop(&mut self) {
        self.buffer.pop();
    }
}

/// Mutable state for the terminal application.
#[derive(Debug, Clone)]
pub struct App {
    /// The active run state rendered by the dashboard.
    state: RunState,
    /// The currently selected screen.
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
    /// The decoded portable config retained by the front-end session.
    config: Option<crate::data::PortableConfig>,
    /// The most recent config decode error.
    config_error: String,
    /// The most recent validation result.
    validation_note: String,
    /// The zero-based index of the selected editable field.
    selected_field: usize,
    /// The active text editor, when one is open.
    input: Option<TextInput>,
    /// The desired priority opt-in awaiting a second explicit confirmation.
    pending_priority: Option<bool>,
    /// The portability findings for the retained config.
    portability: PortabilityReport,
    /// The directory used for named profile documents.
    profile_directory: PathBuf,
    /// Saved profiles discovered on disk.
    saved_profiles: Vec<SavedProfile>,
    /// The zero-based index of the selected saved profile.
    selected_profile: usize,
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
            config: None,
            config_error: String::new(),
            validation_note: "not validated".to_string(),
            selected_field: 0,
            input: None,
            pending_priority: None,
            portability: PortabilityReport::default(),
            profile_directory: crate::profile::default_directory(),
            saved_profiles: Vec::new(),
            selected_profile: 0,
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

    /// Select one screen explicitly and reset transient field input state.
    pub fn set_screen(&mut self, screen: Screen) {
        self.screen = screen;
        self.selected_field = 0;
        self.input = None;
        self.pending_priority = None;
    }

    /// Move to the next screen in dashboard order.
    pub fn next_screen(&mut self) {
        self.set_screen(self.screen.next());
    }

    /// Move to the previous screen in dashboard order.
    pub fn previous_screen(&mut self) {
        self.set_screen(self.screen.previous());
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

    /// Replace the decoded portable config and record any decode error.
    ///
    /// A rejected document is never retained, so stale edits cannot appear as
    /// the effective config after a failed reload.
    pub fn set_config_json(&mut self, text: &str) -> Result<(), crate::data::DataError> {
        match crate::data::PortableConfig::from_json(text) {
            Ok(config) => {
                self.config = Some(config);
                self.config_error.clear();
                Ok(())
            }
            Err(error) => {
                self.config = None;
                self.config_error = error.message().to_string();
                Err(error)
            }
        }
    }

    /// Return the decoded portable config, once one has been loaded.
    pub fn config(&self) -> Option<&crate::data::PortableConfig> {
        self.config.as_ref()
    }

    /// Return the most recent config decode error.
    pub fn config_error(&self) -> &str {
        &self.config_error
    }

    /// Return the most recent validation result text.
    pub fn validation_note(&self) -> &str {
        &self.validation_note
    }

    /// Replace the most recent validation result text.
    pub fn set_validation_note(&mut self, note: impl Into<String>) {
        self.validation_note = note.into();
    }

    /// Replace the portability findings for the retained config.
    pub fn set_portability(&mut self, report: PortabilityReport) {
        self.portability = report;
    }

    /// Return the portability findings for the retained config.
    pub fn portability(&self) -> &PortabilityReport {
        &self.portability
    }

    /// Return the editable fields shown on the active screen.
    pub fn editable_fields(&self) -> &'static [EditableField] {
        EditableField::fields_for(self.screen)
    }

    /// Return the zero-based index of the selected editable field.
    pub fn selected_field_index(&self) -> usize {
        self.selected_field
    }

    /// Return the selected editable field, when the active screen has one.
    pub fn selected_editable_field(&self) -> Option<EditableField> {
        self.editable_fields().get(self.selected_field).copied()
    }

    /// Select the next editable field with wrap-around.
    pub fn select_next_field(&mut self) {
        let count = self.editable_fields().len();
        if count == 0 {
            return;
        }
        self.selected_field = (self.selected_field + 1) % count;
        self.input = None;
    }

    /// Select the previous editable field with wrap-around.
    pub fn select_previous_field(&mut self) {
        let count = self.editable_fields().len();
        if count == 0 {
            return;
        }
        self.selected_field = (self.selected_field + count - 1) % count;
        self.input = None;
    }

    /// Open an empty replacement editor for one field and show its current value.
    ///
    /// # Returns
    /// `true` when a config was loaded and the editor opened.
    pub fn begin_field_edit(&mut self, field: EditableField) -> bool {
        let Some(config) = self.config.as_ref() else {
            return false;
        };
        let current = field.current_value(config);
        let prompt = format!("{} [{}] = ", field.label(), current);
        self.input = Some(TextInput::new(field, "", prompt));
        true
    }

    /// Return the active text editor, when one is open.
    pub fn input(&self) -> Option<&TextInput> {
        self.input.as_ref()
    }

    /// Append one character to the active text editor.
    pub fn push_input_char(&mut self, character: char) {
        if let Some(input) = self.input.as_mut() {
            input.push(character);
        }
    }

    /// Remove the final character from the active text editor.
    pub fn pop_input_char(&mut self) {
        if let Some(input) = self.input.as_mut() {
            input.pop();
        }
    }

    /// Close the active text editor without applying its buffer.
    pub fn cancel_input(&mut self) {
        self.input = None;
    }

    /// Close and return the active text editor for application.
    pub fn take_input(&mut self) -> Option<TextInput> {
        self.input.take()
    }

    /// Request or confirm one scheduling-priority opt-in change.
    ///
    /// # Returns
    /// `true` only on the second identical request, which is the explicit
    /// confirmation. The first request records a pending decision and returns
    /// `false`.
    pub fn request_priority_change(&mut self, desired: bool) -> bool {
        if self.pending_priority == Some(desired) {
            self.pending_priority = None;
            true
        } else {
            self.pending_priority = Some(desired);
            false
        }
    }

    /// Return the pending scheduling-priority decision, when one is awaiting confirmation.
    pub fn pending_priority(&self) -> Option<bool> {
        self.pending_priority
    }

    /// Cancel a pending scheduling-priority decision.
    pub fn clear_pending_priority(&mut self) {
        self.pending_priority = None;
    }

    /// Return the directory used for named profile documents.
    pub fn profile_directory(&self) -> &PathBuf {
        &self.profile_directory
    }

    /// Replace the directory used for named profile documents.
    pub fn set_profile_directory(&mut self, directory: impl Into<PathBuf>) {
        self.profile_directory = directory.into();
    }

    /// Replace saved profiles and keep the current selection by path.
    ///
    /// A missing path falls back to the first profile. This prevents profile
    /// saves from shifting the selection when the sorted list changes.
    pub fn set_saved_profiles(&mut self, profiles: Vec<SavedProfile>) {
        let fallback = self
            .selected_profile
            .min(self.saved_profiles.len().saturating_sub(1));
        let selected_path = self
            .selected_saved_profile()
            .map(|profile| profile.path().to_path_buf());
        self.saved_profiles = profiles;
        self.selected_profile = selected_path
            .as_deref()
            .and_then(|path| {
                self.saved_profiles
                    .iter()
                    .position(|profile| profile.path() == path)
            })
            .unwrap_or_else(|| fallback.min(self.saved_profiles.len().saturating_sub(1)));
    }

    /// Select the saved profile with the given path, if it exists.
    pub fn select_saved_profile_path(&mut self, path: &std::path::Path) -> bool {
        if let Some(index) = self
            .saved_profiles
            .iter()
            .position(|profile| profile.path() == path)
        {
            self.selected_profile = index;
            true
        } else {
            false
        }
    }

    /// Return every discovered saved profile.
    pub fn saved_profiles(&self) -> &[SavedProfile] {
        &self.saved_profiles
    }

    /// Return the zero-based index of the selected saved profile.
    pub fn selected_profile_index(&self) -> usize {
        self.selected_profile
    }

    /// Return the selected saved profile, when one exists.
    pub fn selected_saved_profile(&self) -> Option<&SavedProfile> {
        self.saved_profiles.get(self.selected_profile)
    }

    /// Select the next saved profile with wrap-around.
    pub fn select_next_profile(&mut self) {
        let count = self.saved_profiles.len();
        if count == 0 {
            return;
        }
        self.selected_profile = (self.selected_profile + 1) % count;
    }

    /// Select the previous saved profile with wrap-around.
    pub fn select_previous_profile(&mut self) {
        let count = self.saved_profiles.len();
        if count == 0 {
            return;
        }
        self.selected_profile = (self.selected_profile + count - 1) % count;
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
    use crate::config::DEFAULT_CONFIG_JSON;
    use crate::data::{DashboardData, PluginInspection, PortabilityReport};
    use crate::profile::SavedProfile;
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

    /// Verify screen cycles forward and backward through every registered screen.
    #[test]
    fn screen_cycles_forward_and_backward() {
        assert_eq!(Screen::ALL.len(), 8);
        for (index, screen) in Screen::ALL.iter().enumerate() {
            let next = Screen::ALL[(index + 1) % Screen::ALL.len()];
            let previous = Screen::ALL[(index + Screen::ALL.len() - 1) % Screen::ALL.len()];
            assert_eq!(screen.next(), next);
            assert_eq!(screen.previous(), previous);
        }
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

    /// Verify config replacement exposes typed values and rejects malformed text.
    #[test]
    fn config_replacement_exposes_values_and_rejects_malformed_text() {
        let mut app = App::new();
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        assert_eq!(
            app.config().expect("config is retained").profile_name(),
            "Quick"
        );
        assert!(app.config_error().is_empty());

        assert!(app.set_config_json("not json").is_err());
        assert!(app.config().is_none());
        assert!(!app.config_error().is_empty());
    }

    /// Verify field selection wraps and screen changes reset transient input.
    #[test]
    fn field_selection_wraps_and_screen_changes_reset_transient_input() {
        let mut app = App::new();
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        app.set_screen(Screen::Workload);
        let count = app.editable_fields().len();
        assert_eq!(app.selected_field_index(), 0);
        app.select_previous_field();
        assert_eq!(app.selected_field_index(), count - 1);
        app.select_next_field();
        assert_eq!(app.selected_field_index(), 0);

        let field = app.selected_editable_field().expect("field exists");
        assert!(app.begin_field_edit(field));
        app.push_input_char('x');
        app.request_priority_change(true);
        app.set_screen(Screen::Configuration);
        assert!(app.input().is_none());
        assert_eq!(app.pending_priority(), None);
        assert_eq!(app.selected_field_index(), 0);
    }

    /// Verify text input edits buffer characters and can be cancelled.
    #[test]
    fn text_input_buffers_characters_and_can_be_cancelled() {
        let mut app = App::new();
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        assert!(app.begin_field_edit(crate::config::EditableField::ProfileName));
        app.push_input_char('A');
        app.push_input_char('B');
        assert_eq!(app.input().expect("input is open").buffer(), "AB");
        assert!(
            app.input()
                .expect("input is open")
                .prompt()
                .contains("[Quick]")
        );
        app.pop_input_char();
        assert_eq!(app.input().expect("input is open").buffer(), "A");
        let taken = app.take_input().expect("input is returned");
        assert_eq!(taken.target(), crate::config::EditableField::ProfileName);
        assert!(app.input().is_none());
    }

    /// Verify priority confirmation needs an identical second request.
    #[test]
    fn priority_confirmation_needs_an_identical_second_request() {
        let mut app = App::new();
        assert!(!app.request_priority_change(true));
        assert_eq!(app.pending_priority(), Some(true));
        assert!(!app.request_priority_change(false));
        assert_eq!(app.pending_priority(), Some(false));
        assert!(app.request_priority_change(false));
        assert_eq!(app.pending_priority(), None);
        app.request_priority_change(true);
        app.clear_pending_priority();
        assert_eq!(app.pending_priority(), None);
    }

    /// Verify saved profile selection wraps and clamps after replacement.
    #[test]
    fn saved_profile_selection_wraps_and_clamps() {
        let mut app = App::new();
        app.set_saved_profiles(vec![
            SavedProfile::new("A", "/p/A.json"),
            SavedProfile::new("B", "/p/B.json"),
        ]);
        app.select_previous_profile();
        assert_eq!(app.selected_profile_index(), 1);
        app.select_next_profile();
        assert_eq!(app.selected_profile_index(), 0);
        assert_eq!(
            app.selected_saved_profile().expect("profile exists").name(),
            "A"
        );
        app.set_saved_profiles(vec![SavedProfile::new("C", "/p/C.json")]);
        assert_eq!(app.selected_profile_index(), 0);
    }

    /// Verify validation and portability accessors retain explicit values.
    #[test]
    fn validation_and_portability_accessors_retain_explicit_values() {
        let mut app = App::new();
        assert_eq!(app.validation_note(), "not validated");
        assert!(app.portability().is_allowed());
        app.set_validation_note("valid");
        app.set_portability(PortabilityReport::blocked("unsupported capability"));
        assert_eq!(app.validation_note(), "valid");
        assert!(!app.portability().is_allowed());
        assert_eq!(app.portability().blockers()[0].severity(), "blocker");
    }
}
