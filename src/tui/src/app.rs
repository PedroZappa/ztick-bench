// SPDX-License-Identifier: MIT
//! Application state shared by front-end event handling and rendering.
//!
//! P5.2 added the three read-only screens. P5.3 adds the editable config
//! screens, typed input state, named-profile selection and the portability gate.
//! P5.4 carries live run progress; P5.5 retains the completed run's result path
//! and typed inspection. P5.6 retains comparison, report and bundle evidence
//! without recomputing metrics.

use std::path::PathBuf;

use crate::config::EditableField;
use crate::data::{
    ComparisonInspection, DashboardData, PluginCandidate, PluginInspection, PortabilityReport,
    ReportInspection, ResultInspection,
};
use crate::profile::SavedProfile;
use crate::theme::ThemeKind;
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

/// Timing level reported by a copied core progress event.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum TimingLevel {
    /// No timing level was reported.
    #[default]
    Unknown,
    /// One raw duration per processed block.
    PerBlock,
    /// One raw duration spanning the primary DSP interval.
    WholeRun,
    /// One raw duration spanning generation through the primary interval.
    EndToEnd,
}

/// Map private-boundary timing codes to stable display values.
impl TimingLevel {
    /// Map one private-boundary timing code to a display level.
    pub fn from_code(code: i32) -> Self {
        match code {
            crate::ZTICK_FRONTEND_TIMING_PER_BLOCK => Self::PerBlock,
            crate::ZTICK_FRONTEND_TIMING_WHOLE_RUN => Self::WholeRun,
            crate::ZTICK_FRONTEND_TIMING_END_TO_END => Self::EndToEnd,
            _ => Self::Unknown,
        }
    }

    /// Return the stable short label rendered for this timing level.
    pub fn label(self) -> &'static str {
        match self {
            Self::Unknown => "unsupported",
            Self::PerBlock => "per_block",
            Self::WholeRun => "whole_run",
            Self::EndToEnd => "end_to_end",
        }
    }
}

/// Isolation mode reported by a copied core progress event.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum IsolationMode {
    /// No isolation mode was reported.
    #[default]
    Unknown,
    /// The integration runs in the host process.
    InProcess,
    /// The integration runs in a child process.
    OutOfProcess,
}

/// Map private-boundary isolation codes to stable display values.
impl IsolationMode {
    /// Map one private-boundary isolation code to a display mode.
    pub fn from_code(code: i32) -> Self {
        match code {
            crate::ZTICK_FRONTEND_ISOLATION_IN_PROCESS => Self::InProcess,
            crate::ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS => Self::OutOfProcess,
            _ => Self::Unknown,
        }
    }

    /// Return the stable short label rendered for this isolation mode.
    pub fn label(self) -> &'static str {
        match self {
            Self::Unknown => "unsupported",
            Self::InProcess => "in_process",
            Self::OutOfProcess => "out_of_process",
        }
    }
}

/// Live execution context copied from core progress events.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ExecutionProgress {
    /// Filesystem path of the config started by the current run.
    pub config_path: String,
    /// One-based test point reported by the latest progress event.
    pub test_point: u32,
    /// Most recent human-readable progress message.
    pub message: String,
    /// Timing level reported by the latest event with a known code.
    pub timing_level: TimingLevel,
    /// Isolation mode reported by the latest event with a known code.
    pub isolation_mode: IsolationMode,
    /// Copied result envelope status and destination path, when available.
    pub result_status: String,
}

/// Screens available after Phase 5.6.
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
    /// Live run state and context copied from core progress events.
    Execution,
    /// Built-in and saved named config profiles.
    Profiles,
    /// Detailed inspection of one stored result document.
    Results,
    /// Core comparison records with threshold and significance evidence.
    Compare,
    /// Report envelopes, CSV projections and session bundle operations.
    Report,
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
            Self::Execution => "Execution monitoring",
            Self::Profiles => "Profiles & settings",
            Self::Results => "Results inspection",
            Self::Compare => "Compare",
            Self::Report => "Report / Export",
        }
    }

    /// Return all screens in navigation order.
    pub const ALL: [Self; 12] = [
        Self::Dashboard,
        Self::System,
        Self::Integrations,
        Self::Workload,
        Self::Configuration,
        Self::Measurement,
        Self::Scheduling,
        Self::Execution,
        Self::Profiles,
        Self::Results,
        Self::Compare,
        Self::Report,
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

/// Destination for one active text edit.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InputTarget {
    /// A portable config field receiving a dotted override.
    Config(EditableField),
    /// Baseline result document path for a comparison.
    BaselinePath,
    /// Candidate result document path for a comparison.
    CandidatePath,
    /// Comparison threshold percentage as CLI text.
    ThresholdPercent,
    /// Comparison significance level as CLI text.
    Alpha,
    /// Stored result document path for the report screen.
    ReportPath,
    /// Destination path for the copied report JSON envelope.
    JsonExportPath,
    /// Destination path for the copied CSV projection.
    CsvExportPath,
    /// Destination path for a session bundle.
    BundlePath,
    /// Directory used when importing a session bundle.
    BundleTargetDir,
}

/// Explicit first-run trust confirmation requested by the Integrations screen.
///
/// The prompt carries copied metadata only. No plugin code is executed while the
/// prompt is visible, and no execution is allowed until an explicit grant is
/// recorded through the private front-end boundary.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TrustConsentPrompt {
    /// Filesystem path of the discovered plugin candidate.
    path: String,
    /// Stable plugin identifier that receives the consent decision.
    identifier: String,
    /// Human-readable plugin name.
    name: String,
    /// Declared adapter version.
    version: String,
    /// Declared SDK version.
    sdk_version: String,
    /// Declared capability mask.
    capabilities: String,
    /// Core compatibility status token.
    compatibility_status: String,
    /// Core compatibility explanation.
    compatibility_reason: String,
    /// Current trust state token for the selected plugin.
    trust_state: String,
}

impl TrustConsentPrompt {
    /// Construct a consent prompt from copied plugin metadata.
    ///
    /// # Parameters
    /// - `path`: Discovered plugin descriptor path.
    /// - `identifier`: Stable plugin identifier.
    /// - `name`: Human-readable plugin name.
    /// - `version`: Declared adapter version.
    /// - `sdk_version`: Declared SDK version.
    /// - `capabilities`: Declared capability mask.
    /// - `compatibility_status`: Core compatibility status token.
    /// - `compatibility_reason`: Core compatibility explanation.
    /// - `trust_state`: Current trust state token.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        path: impl Into<String>,
        identifier: impl Into<String>,
        name: impl Into<String>,
        version: impl Into<String>,
        sdk_version: impl Into<String>,
        capabilities: impl Into<String>,
        compatibility_status: impl Into<String>,
        compatibility_reason: impl Into<String>,
        trust_state: impl Into<String>,
    ) -> Self {
        Self {
            path: path.into(),
            identifier: identifier.into(),
            name: name.into(),
            version: version.into(),
            sdk_version: sdk_version.into(),
            capabilities: capabilities.into(),
            compatibility_status: compatibility_status.into(),
            compatibility_reason: compatibility_reason.into(),
            trust_state: trust_state.into(),
        }
    }

    /// Return the discovered plugin descriptor path.
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Return the stable plugin identifier.
    pub fn identifier(&self) -> &str {
        &self.identifier
    }

    /// Return the human-readable plugin name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the declared adapter version.
    pub fn version(&self) -> &str {
        &self.version
    }

    /// Return the declared SDK version.
    pub fn sdk_version(&self) -> &str {
        &self.sdk_version
    }

    /// Return the declared capability mask.
    pub fn capabilities(&self) -> &str {
        &self.capabilities
    }

    /// Return the core compatibility status token.
    pub fn compatibility_status(&self) -> &str {
        &self.compatibility_status
    }

    /// Return the core compatibility explanation.
    pub fn compatibility_reason(&self) -> &str {
        &self.compatibility_reason
    }

    /// Return the current trust state token.
    pub fn trust_state(&self) -> &str {
        &self.trust_state
    }
}

/// One active text edit started from a config field or a P5.6 operation field.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TextInput {
    /// The destination that receives the committed value.
    target: InputTarget,
    /// The editable text buffer.
    buffer: String,
    /// The prompt shown while the editor is active.
    prompt: String,
}

/// Construct and inspect one active text edit.
impl TextInput {
    /// Start one text edit for a config field or P5.6 operation field.
    ///
    /// # Parameters
    /// - `target`: Destination that receives the committed value.
    /// - `buffer`: Initial editor contents; empty for replacement edits.
    /// - `prompt`: Prompt shown above the editor while it is active.
    pub fn new(target: InputTarget, buffer: impl Into<String>, prompt: impl Into<String>) -> Self {
        Self {
            target,
            buffer: buffer.into(),
            prompt: prompt.into(),
        }
    }

    /// Return the destination that receives the committed value.
    pub fn target(&self) -> InputTarget {
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
    /// Active first-run trust confirmation prompt, when one is open.
    trust_prompt: Option<TrustConsentPrompt>,
    /// The warning messages retained in arrival order.
    warnings: Vec<String>,
    /// Live execution context copied from core progress events.
    progress: ExecutionProgress,
    /// Stored result document path copied from a completed run envelope.
    result_path: Option<PathBuf>,
    /// Typed inspection of the stored result document, when one was loaded.
    result_inspection: Option<ResultInspection>,
    /// Baseline result path used by the comparison screen.
    baseline_path: Option<PathBuf>,
    /// Candidate result path used by the comparison screen.
    candidate_path: Option<PathBuf>,
    /// Comparison policy threshold as CLI text.
    threshold_percent: String,
    /// Comparison policy significance level as CLI text.
    alpha: String,
    /// Whether comparison significance uses the one-sided tail.
    one_sided: bool,
    /// Whether a classified regression maps to the CLI regression exit code.
    fail_on_regression: bool,
    /// Typed core comparison record copied from the private boundary.
    comparison: Option<ComparisonInspection>,
    /// Stored result path used by the report screen.
    report_path: Option<PathBuf>,
    /// Typed report envelope copied from the private boundary.
    report: Option<ReportInspection>,
    /// Core CSV row mode selected for projection.
    csv_row_mode: String,
    /// Copied CSV projection text for the report screen.
    csv_export: String,
    /// Destination path for copied report JSON text.
    json_export_path: Option<PathBuf>,
    /// Destination path for copied CSV text.
    csv_export_path: Option<PathBuf>,
    /// Destination path for session bundle export/import.
    bundle_path: Option<PathBuf>,
    /// Target directory used when reconstructing a session bundle.
    bundle_target_dir: PathBuf,
    /// Visible status of the most recent bundle operation.
    bundle_status: String,
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
    /// Active color theme.
    theme: ThemeKind,
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
            trust_prompt: None,
            warnings: Vec::new(),
            progress: ExecutionProgress::default(),
            result_path: None,
            result_inspection: None,
            baseline_path: None,
            candidate_path: None,
            threshold_percent: "1.0".to_string(),
            alpha: "0.05".to_string(),
            one_sided: false,
            fail_on_regression: false,
            comparison: None,
            report_path: None,
            report: None,
            csv_row_mode: "per_test_point".to_string(),
            csv_export: String::new(),
            json_export_path: None,
            csv_export_path: None,
            bundle_path: None,
            bundle_target_dir: PathBuf::from("."),
            bundle_status: "no bundle operation".to_string(),
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
            theme: ThemeKind::default(),
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
        self.trust_prompt = None;
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

    /// Replace copied dashboard inputs while preserving inspection for the same plugin.
    ///
    /// The selection is clamped to the new plugin list. Inspection detail is
    /// retained only when it still describes the selected candidate, so an
    /// unrelated dashboard refresh cannot silently remove a portability blocker.
    pub fn set_dashboard_data(&mut self, data: DashboardData) {
        let selected_path = self
            .selected_plugin()
            .map(|plugin| plugin.path().to_string());
        self.selected_plugin = self
            .selected_plugin
            .min(data.plugins().plugins().len().saturating_sub(1));
        let keep_inspection = match (
            selected_path.as_deref(),
            self.selected_plugin(),
            self.inspection(),
        ) {
            (Some(path), Some(plugin), Some(inspection)) => {
                inspection.path() == path && plugin.path() == path
            }
            _ => false,
        };
        self.data = data;
        if !keep_inspection {
            self.inspection = None;
        }
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
        self.trust_prompt = None;
    }

    /// Select the previous plugin candidate with wrap-around.
    pub fn select_previous_plugin(&mut self) {
        let count = self.data.plugins().plugins().len();
        if count == 0 {
            return;
        }
        self.selected_plugin = (self.selected_plugin + count - 1) % count;
        self.inspection = None;
        self.trust_prompt = None;
    }

    /// Return the inspection loaded for the selected candidate.
    pub fn inspection(&self) -> Option<&PluginInspection> {
        self.inspection.as_ref()
    }

    /// Replace the inspection loaded for the selected candidate.
    pub fn set_inspection(&mut self, inspection: Option<PluginInspection>) {
        let keep_prompt = self
            .trust_prompt
            .as_ref()
            .zip(inspection.as_ref())
            .is_some_and(|(prompt, value)| prompt.path() == value.path());
        self.inspection = inspection;
        if !keep_prompt {
            self.trust_prompt = None;
        }
    }

    /// Return the active first-run trust confirmation prompt.
    pub fn trust_prompt(&self) -> Option<&TrustConsentPrompt> {
        self.trust_prompt.as_ref()
    }

    /// Open the first-run trust confirmation prompt for the selected plugin.
    ///
    /// # Returns
    /// `true` when a prompt was opened for the selected plugin.
    pub fn open_trust_prompt(&mut self) -> bool {
        let Some(prompt) = self.selected_trust_prompt() else {
            return false;
        };
        self.trust_prompt = Some(prompt);
        true
    }

    /// Close the active trust confirmation prompt without recording a decision.
    pub fn cancel_trust_prompt(&mut self) {
        self.trust_prompt = None;
    }

    /// Take the active trust confirmation prompt for an explicit decision.
    pub fn take_trust_prompt(&mut self) -> Option<TrustConsentPrompt> {
        self.trust_prompt.take()
    }

    /// Build the trust confirmation prompt for the currently selected plugin.
    ///
    /// The prompt is only available when metadata-only inspection has produced a
    /// stable identifier, compatibility status and capability report. Missing
    /// identity is treated as unavailable rather than guessed.
    pub fn selected_trust_prompt(&self) -> Option<TrustConsentPrompt> {
        let plugin = self.selected_plugin()?;
        let inspection = self
            .inspection
            .as_ref()
            .filter(|value| value.path() == plugin.path());
        let trust_state = inspection
            .map(|value| value.trust().state().to_string())
            .filter(|value| !value.is_empty())
            .unwrap_or_else(|| {
                if plugin.has_metadata() {
                    self.data
                        .plugins()
                        .trust()
                        .state_for(plugin.identifier())
                        .to_string()
                } else {
                    plugin.trust().to_string()
                }
            });
        let (
            identifier,
            name,
            version,
            sdk_version,
            capabilities,
            compatibility_status,
            compatibility_reason,
        ) = if let Some(inspection) = inspection.filter(|value| !value.identifier().is_empty()) {
            (
                inspection.identifier().to_string(),
                inspection.name().to_string(),
                inspection.version().to_string(),
                inspection.sdk_version().to_string(),
                inspection.capabilities().to_string(),
                inspection.compatibility().status().to_string(),
                inspection.compatibility().reason().to_string(),
            )
        } else if plugin.has_metadata() {
            (
                plugin.identifier().to_string(),
                plugin.name().to_string(),
                plugin.version().to_string(),
                plugin.sdk_version().to_string(),
                plugin.capabilities().to_string(),
                "uninspected".to_string(),
                "metadata-only plugin inspection has not been loaded".to_string(),
            )
        } else {
            return None;
        };

        Some(TrustConsentPrompt::new(
            plugin.path().to_string(),
            identifier,
            name,
            version,
            sdk_version,
            capabilities,
            compatibility_status,
            compatibility_reason,
            trust_state,
        ))
    }

    /// Return warnings recorded from boundary events.
    pub fn warnings(&self) -> &[String] {
        &self.warnings
    }

    /// Append one warning in arrival order.
    pub fn push_warning(&mut self, warning: impl Into<String>) {
        self.warnings.push(warning.into());
    }

    /// Return the live execution context copied from progress events.
    pub fn progress(&self) -> &ExecutionProgress {
        &self.progress
    }

    /// Reset execution state when a new asynchronous run is started.
    ///
    /// Stale result state is discarded so a previous run can never appear as the
    /// result of the new run.
    ///
    /// # Parameters
    /// - `config_path`: Config document path passed to the private boundary.
    pub fn begin_run(&mut self, config_path: impl Into<String>) {
        self.progress = ExecutionProgress {
            config_path: config_path.into(),
            ..ExecutionProgress::default()
        };
        self.warnings.clear();
        self.result_path = None;
        self.result_inspection = None;
        self.baseline_path = None;
        self.candidate_path = None;
        self.comparison = None;
        self.report_path = None;
        self.report = None;
        self.csv_export.clear();
        self.bundle_status = "no bundle operation".to_string();
        self.set_state_code(crate::ZTICK_FRONTEND_STATE_VALIDATING);
        self.set_status("run started");
    }

    /// Record the copied result envelope for the active run.
    ///
    /// A usable `output_path` is retained for the results and report screens and
    /// seeded as the comparison baseline. The candidate stays unset so an
    /// accidental self-comparison cannot be presented as a two-result decision.
    /// Unknown or malformed envelopes are reported explicitly rather than being
    /// presented as a successful completed result, and any earlier inspection is
    /// discarded.
    pub fn set_run_result(&mut self, json: &str) {
        self.result_path = None;
        self.result_inspection = None;
        self.baseline_path = None;
        self.candidate_path = None;
        self.comparison = None;
        self.report_path = None;
        self.report = None;
        self.csv_export.clear();
        let parsed = crate::json::parse(json);
        self.progress.result_status = match parsed {
            Ok(envelope) => {
                let status = envelope
                    .get("status")
                    .and_then(crate::json::Json::as_str)
                    .unwrap_or("unknown");
                let output = envelope
                    .get("data")
                    .and_then(|data| data.get("output_path"))
                    .and_then(crate::json::Json::as_str)
                    .unwrap_or("unavailable");
                if !output.is_empty() && output != "unavailable" {
                    let path = PathBuf::from(output);
                    self.result_path = Some(path.clone());
                    self.baseline_path = Some(path.clone());
                    self.report_path = Some(path);
                }
                format!("{status}; output={output}")
            }
            Err(_) => "undecodable result envelope".to_string(),
        };
    }

    /// Return the stored result path copied from the latest run envelope.
    pub fn result_path(&self) -> Option<&std::path::Path> {
        self.result_path.as_deref()
    }

    /// Return the typed inspection loaded for the stored result document.
    pub fn result_inspection(&self) -> Option<&ResultInspection> {
        self.result_inspection.as_ref()
    }

    /// Replace the typed inspection for the stored result document.
    pub fn set_result_inspection(&mut self, inspection: Option<ResultInspection>) {
        self.result_inspection = inspection;
    }

    /// Return the selected comparison baseline path.
    pub fn baseline_path(&self) -> Option<&std::path::Path> {
        self.baseline_path.as_deref()
    }

    /// Replace the selected comparison baseline path.
    pub fn set_baseline_path(&mut self, path: Option<PathBuf>) {
        self.baseline_path = path;
    }

    /// Return the selected comparison candidate path.
    pub fn candidate_path(&self) -> Option<&std::path::Path> {
        self.candidate_path.as_deref()
    }

    /// Replace the selected comparison candidate path.
    pub fn set_candidate_path(&mut self, path: Option<PathBuf>) {
        self.candidate_path = path;
    }

    /// Return the comparison threshold percentage as CLI text.
    pub fn threshold_percent(&self) -> &str {
        &self.threshold_percent
    }

    /// Replace the comparison threshold percentage after local validation.
    pub fn set_threshold_percent(&mut self, value: impl Into<String>) {
        self.threshold_percent = value.into();
    }

    /// Return the comparison significance level as CLI text.
    pub fn alpha(&self) -> &str {
        &self.alpha
    }

    /// Replace the comparison significance level after local validation.
    pub fn set_alpha(&mut self, value: impl Into<String>) {
        self.alpha = value.into();
    }

    /// Return whether comparison significance uses the one-sided tail.
    pub fn one_sided(&self) -> bool {
        self.one_sided
    }

    /// Toggle the comparison sidedness and return the new value.
    pub fn toggle_one_sided(&mut self) -> bool {
        self.one_sided = !self.one_sided;
        self.one_sided
    }

    /// Return whether a regression maps to the CLI regression exit code.
    pub fn fail_on_regression(&self) -> bool {
        self.fail_on_regression
    }

    /// Toggle regression exit-code mapping and return the new value.
    pub fn toggle_fail_on_regression(&mut self) -> bool {
        self.fail_on_regression = !self.fail_on_regression;
        self.fail_on_regression
    }

    /// Return the copied comparison inspection, when one was loaded.
    pub fn comparison(&self) -> Option<&ComparisonInspection> {
        self.comparison.as_ref()
    }

    /// Replace the copied comparison inspection.
    pub fn set_comparison(&mut self, inspection: Option<ComparisonInspection>) {
        self.comparison = inspection;
    }

    /// Return the selected report result path.
    pub fn report_path(&self) -> Option<&std::path::Path> {
        self.report_path.as_deref()
    }

    /// Replace the selected report result path.
    pub fn set_report_path(&mut self, path: Option<PathBuf>) {
        self.report_path = path;
    }

    /// Return the copied report inspection, when one was loaded.
    pub fn report(&self) -> Option<&ReportInspection> {
        self.report.as_ref()
    }

    /// Replace the copied report inspection.
    pub fn set_report(&mut self, inspection: Option<ReportInspection>) {
        self.report = inspection;
    }

    /// Return the selected core CSV row mode.
    pub fn csv_row_mode(&self) -> &str {
        &self.csv_row_mode
    }

    /// Cycle the CSV row mode between the two documented projections.
    pub fn cycle_csv_row_mode(&mut self) -> &str {
        self.csv_row_mode = if self.csv_row_mode == "per_test_point" {
            "per_metric_sample".to_string()
        } else {
            "per_test_point".to_string()
        };
        &self.csv_row_mode
    }

    /// Return the copied CSV projection text.
    pub fn csv_export(&self) -> &str {
        &self.csv_export
    }

    /// Replace the copied CSV projection text.
    pub fn set_csv_export(&mut self, text: impl Into<String>) {
        self.csv_export = text.into();
    }

    /// Return the destination path for copied report JSON text.
    pub fn json_export_path(&self) -> Option<&std::path::Path> {
        self.json_export_path.as_deref()
    }

    /// Replace the destination path for copied report JSON text.
    pub fn set_json_export_path(&mut self, path: Option<PathBuf>) {
        self.json_export_path = path;
    }

    /// Return the destination path for copied CSV text.
    pub fn csv_export_path(&self) -> Option<&std::path::Path> {
        self.csv_export_path.as_deref()
    }

    /// Replace the destination path for copied CSV text.
    pub fn set_csv_export_path(&mut self, path: Option<PathBuf>) {
        self.csv_export_path = path;
    }

    /// Return the destination path for session bundle operations.
    pub fn bundle_path(&self) -> Option<&std::path::Path> {
        self.bundle_path.as_deref()
    }

    /// Replace the destination path for session bundle operations.
    pub fn set_bundle_path(&mut self, path: Option<PathBuf>) {
        self.bundle_path = path;
    }

    /// Return the target directory used when importing a session bundle.
    pub fn bundle_target_dir(&self) -> &std::path::Path {
        &self.bundle_target_dir
    }

    /// Replace the target directory used when importing a session bundle.
    pub fn set_bundle_target_dir(&mut self, path: PathBuf) {
        self.bundle_target_dir = path;
    }

    /// Return the visible status of the most recent bundle operation.
    pub fn bundle_status(&self) -> &str {
        &self.bundle_status
    }

    /// Replace the visible status of the most recent bundle operation.
    pub fn set_bundle_status(&mut self, status: impl Into<String>) {
        self.bundle_status = status.into();
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
    /// the effective config after a failed reload. A pending priority decision
    /// belongs to the previous document and is always discarded.
    pub fn set_config_json(&mut self, text: &str) -> Result<(), crate::data::DataError> {
        self.pending_priority = None;
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

    /// Return the active color theme.
    pub fn theme(&self) -> ThemeKind {
        self.theme
    }

    /// Cycle the active color theme and return its name.
    pub fn cycle_theme(&mut self) -> &'static str {
        self.theme = self.theme.next();
        self.theme.name()
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
        self.input = Some(TextInput::new(InputTarget::Config(field), "", prompt));
        true
    }

    /// Open a replacement editor for one P5.6 operation field.
    ///
    /// # Parameters
    /// - `target`: Destination that receives the committed value.
    /// - `current`: Value shown as the editor's initial prompt context.
    /// - `prompt`: Label shown above the editor while it is active.
    pub fn begin_input(
        &mut self,
        target: InputTarget,
        current: impl Into<String>,
        prompt: impl Into<String>,
    ) {
        let current = current.into();
        let label = prompt.into();
        let prompt = if current.is_empty() {
            format!("{label} = ")
        } else {
            format!("{label} [{current}] = ")
        };
        self.input = Some(TextInput::new(target, "", prompt));
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
    /// State changes update the displayed state. Known timing, isolation and
    /// test-point values are retained for the execution screen; unknown codes
    /// never overwrite a value already reported by the core. Every non-empty
    /// message updates the status line, and warning events are also retained in
    /// arrival order.
    pub fn observe_event(&mut self, event: &ZtickFrontendEvent) {
        self.set_state_code(event.state);
        let timing = TimingLevel::from_code(event.timing_level);
        if timing != TimingLevel::Unknown {
            self.progress.timing_level = timing;
        }
        let isolation = IsolationMode::from_code(event.isolation_mode);
        if isolation != IsolationMode::Unknown {
            self.progress.isolation_mode = isolation;
        }
        if event.test_point > 0u32 {
            self.progress.test_point = event.test_point;
        }
        let message = event_message(event);
        if message.is_empty() {
            if event.kind == ZTICK_FRONTEND_EVENT_WARNING {
                self.push_warning("warning reported without context");
            }
            return;
        }
        self.progress.message = message.clone();
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

    use super::{App, InputTarget, IsolationMode, RunState, Screen, TimingLevel, event_message};
    use crate::config::DEFAULT_CONFIG_JSON;
    use crate::data::{DashboardData, PluginInspection, PortabilityReport};
    use crate::profile::SavedProfile;
    use crate::{
        ZTICK_FRONTEND_EVENT_COMPLETION, ZTICK_FRONTEND_EVENT_MESSAGE_SIZE,
        ZTICK_FRONTEND_EVENT_PROGRESS, ZTICK_FRONTEND_EVENT_STATE_CHANGE,
        ZTICK_FRONTEND_EVENT_WARNING, ZTICK_FRONTEND_ISOLATION_IN_PROCESS,
        ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS, ZTICK_FRONTEND_ISOLATION_UNKNOWN,
        ZTICK_FRONTEND_STATE_COMPLETE, ZTICK_FRONTEND_STATE_CONFIGURING,
        ZTICK_FRONTEND_STATE_FAILED, ZTICK_FRONTEND_STATE_IDLE, ZTICK_FRONTEND_STATE_INCOMPLETE,
        ZTICK_FRONTEND_STATE_RUNNING, ZTICK_FRONTEND_STATE_VALIDATING,
        ZTICK_FRONTEND_STATE_WARNING, ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION,
        ZTICK_FRONTEND_TIMING_END_TO_END, ZTICK_FRONTEND_TIMING_PER_BLOCK,
        ZTICK_FRONTEND_TIMING_UNKNOWN, ZTICK_FRONTEND_TIMING_WHOLE_RUN, ZtickFrontendEvent,
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

    /// Build one boundary event with copied execution context fields.
    fn event_with_context(
        kind: i32,
        state: i32,
        test_point: u32,
        timing_level: i32,
        isolation_mode: i32,
        message: &str,
    ) -> ZtickFrontendEvent {
        let mut built = event(kind, state, message);
        built.test_point = test_point;
        built.timing_level = timing_level;
        built.isolation_mode = isolation_mode;
        built
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

    /// Verify trust confirmation prompt captures selected plugin identity and capabilities.
    #[test]
    fn trust_prompt_carries_selected_plugin_identity_and_capabilities() {
        let mut app = App::new();
        app.set_dashboard_data(data_with_plugins(1));
        let inspection = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app0.so","status":"success","reason":"","metadata":{"identifier":"app-0","name":"App 0","version":"1.0.0","sdk_version":"1.0.0","capabilities":"0x7c","metadata":"demo"},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_inspection(Some(inspection));
        assert!(app.open_trust_prompt());
        let prompt = app.trust_prompt().expect("prompt is open");
        assert_eq!(prompt.identifier(), "app-0");
        assert_eq!(prompt.name(), "App 0");
        assert_eq!(prompt.version(), "1.0.0");
        assert_eq!(prompt.sdk_version(), "1.0.0");
        assert_eq!(prompt.capabilities(), "0x7c");
        assert_eq!(prompt.compatibility_status(), "success");
        assert_eq!(prompt.trust_state(), "untrusted");
        app.cancel_trust_prompt();
        assert!(app.trust_prompt().is_none());
    }

    /// Verify screen cycles forward and backward through every registered screen.
    #[test]
    fn screen_cycles_forward_and_backward() {
        assert_eq!(Screen::ALL.len(), 12);
        assert_eq!(Screen::Results.title(), "Results inspection");
        assert_eq!(Screen::Compare.title(), "Compare");
        assert_eq!(Screen::Report.title(), "Report / Export");
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

    /// Verify replacing data keeps inspection for an unchanged plugin path.
    #[test]
    fn replacing_data_keeps_matching_inspection() {
        let mut app = App::new();
        app.set_dashboard_data(data_with_plugins(1));
        let inspection = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app0.so","status":"success","reason":"","metadata":{},"compatibility":{"status":"failure","reason":"unsupported on this host"},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_inspection(Some(inspection));

        app.set_dashboard_data(data_with_plugins(1));
        let retained = app.inspection().expect("matching inspection is retained");
        assert_eq!(retained.path(), "/app0.so");
        assert_eq!(retained.compatibility().status(), "failure");
        assert_eq!(
            retained.compatibility().reason(),
            "unsupported on this host"
        );

        let second = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app1.so","status":"success","reason":"","metadata":{},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_dashboard_data(data_with_plugins(2));
        app.select_next_plugin();
        app.set_inspection(Some(second));
        app.set_dashboard_data(data_with_plugins(1));
        assert_eq!(app.selected_plugin_index(), 0);
        assert!(app.inspection().is_none());
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

    /// Verify timing and isolation codes map to distinct explicit labels.
    #[test]
    fn execution_context_codes_map_to_explicit_labels() {
        assert_eq!(
            TimingLevel::from_code(ZTICK_FRONTEND_TIMING_PER_BLOCK),
            TimingLevel::PerBlock
        );
        assert_eq!(
            TimingLevel::from_code(ZTICK_FRONTEND_TIMING_WHOLE_RUN),
            TimingLevel::WholeRun
        );
        assert_eq!(
            TimingLevel::from_code(ZTICK_FRONTEND_TIMING_END_TO_END),
            TimingLevel::EndToEnd
        );
        assert_eq!(
            TimingLevel::from_code(ZTICK_FRONTEND_TIMING_UNKNOWN),
            TimingLevel::Unknown
        );
        assert_eq!(
            IsolationMode::from_code(ZTICK_FRONTEND_ISOLATION_IN_PROCESS),
            IsolationMode::InProcess
        );
        assert_eq!(
            IsolationMode::from_code(ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS),
            IsolationMode::OutOfProcess
        );
        assert_eq!(
            IsolationMode::from_code(ZTICK_FRONTEND_ISOLATION_UNKNOWN),
            IsolationMode::Unknown
        );
        let timing_labels = [
            TimingLevel::Unknown.label(),
            TimingLevel::PerBlock.label(),
            TimingLevel::WholeRun.label(),
            TimingLevel::EndToEnd.label(),
        ];
        let isolation_labels = [
            IsolationMode::Unknown.label(),
            IsolationMode::InProcess.label(),
            IsolationMode::OutOfProcess.label(),
        ];
        let unique_timing: std::collections::HashSet<_> = timing_labels.iter().collect();
        let unique_isolation: std::collections::HashSet<_> = isolation_labels.iter().collect();
        assert_eq!(unique_timing.len(), timing_labels.len());
        assert_eq!(unique_isolation.len(), isolation_labels.len());
    }

    /// Verify copied events retain progress context across unrelated events.
    #[test]
    fn observe_event_retains_copied_execution_context() {
        let mut app = App::new();
        app.begin_run("/tmp/ztick-run.json");
        assert_eq!(app.state(), RunState::Validating);
        app.observe_event(&event_with_context(
            ZTICK_FRONTEND_EVENT_PROGRESS,
            ZTICK_FRONTEND_STATE_RUNNING,
            1,
            ZTICK_FRONTEND_TIMING_WHOLE_RUN,
            ZTICK_FRONTEND_ISOLATION_IN_PROCESS,
            "repetition 1/2",
        ));
        assert_eq!(app.progress().config_path, "/tmp/ztick-run.json");
        assert_eq!(app.progress().test_point, 1);
        assert_eq!(app.progress().timing_level, TimingLevel::WholeRun);
        assert_eq!(app.progress().isolation_mode, IsolationMode::InProcess);
        assert_eq!(app.progress().message, "repetition 1/2");

        app.observe_event(&event_with_context(
            ZTICK_FRONTEND_EVENT_WARNING,
            ZTICK_FRONTEND_STATE_WARNING,
            1,
            ZTICK_FRONTEND_TIMING_PER_BLOCK,
            ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS,
            "timing drift",
        ));
        assert_eq!(app.progress().timing_level, TimingLevel::PerBlock);
        assert_eq!(app.progress().isolation_mode, IsolationMode::OutOfProcess);
        assert_eq!(app.warnings(), ["timing drift"]);

        app.observe_event(&event_with_context(
            ZTICK_FRONTEND_EVENT_COMPLETION,
            ZTICK_FRONTEND_STATE_COMPLETE,
            0,
            ZTICK_FRONTEND_TIMING_UNKNOWN,
            ZTICK_FRONTEND_ISOLATION_UNKNOWN,
            "run completed",
        ));
        assert_eq!(app.progress().timing_level, TimingLevel::PerBlock);
        assert_eq!(app.progress().isolation_mode, IsolationMode::OutOfProcess);
        assert_eq!(app.progress().test_point, 1);
    }

    /// Verify a new run clears stale warnings and copied result status.
    #[test]
    fn begin_run_resets_stale_execution_state() {
        let mut app = App::new();
        app.push_warning("stale");
        app.set_run_result(r#"{"status":"success"}"#);
        let result = crate::data::ResultInspection::from_json(crate::data::fixtures::RESULT_JSON)
            .expect("result fixture decodes");
        app.set_result_inspection(Some(result));
        app.set_comparison(None);
        app.set_report(None);
        app.set_csv_export("stale csv");
        app.set_bundle_status("stale bundle");
        app.begin_run("/tmp/next.json");
        assert!(app.warnings().is_empty());
        assert!(app.progress().result_status.is_empty());
        assert!(app.result_path().is_none());
        assert!(app.result_inspection().is_none());
        assert!(app.baseline_path().is_none());
        assert!(app.candidate_path().is_none());
        assert!(app.comparison().is_none());
        assert!(app.report_path().is_none());
        assert!(app.report().is_none());
        assert!(app.csv_export().is_empty());
        assert_eq!(app.bundle_status(), "no bundle operation");
        assert_eq!(app.progress().test_point, 0);
        assert_eq!(app.progress().timing_level, TimingLevel::Unknown);
        assert_eq!(app.progress().isolation_mode, IsolationMode::Unknown);
    }

    /// Verify result envelopes expose status and destination without recomputing metrics.
    #[test]
    fn set_run_result_exposes_envelope_status_and_output_path() {
        let mut app = App::new();
        app.set_run_result(
            r#"{"schema_version":1,"command":"run","status":"success","exit_code":0,"data":{"output_path":"/tmp/result.json"},"error":""}"#,
        );
        assert_eq!(
            app.progress().result_status,
            "success; output=/tmp/result.json"
        );
        assert_eq!(
            app.result_path().map(|path| path.display().to_string()),
            Some("/tmp/result.json".to_string())
        );
        assert_eq!(
            app.baseline_path().map(|path| path.display().to_string()),
            Some("/tmp/result.json".to_string())
        );
        assert!(app.candidate_path().is_none());
        assert_eq!(
            app.report_path().map(|path| path.display().to_string()),
            Some("/tmp/result.json".to_string())
        );
        let inspection =
            crate::data::ResultInspection::from_json(crate::data::fixtures::RESULT_JSON)
                .expect("result fixture decodes");
        app.set_result_inspection(Some(inspection));
        app.set_run_result("not json");
        assert_eq!(app.progress().result_status, "undecodable result envelope");
        assert!(app.result_path().is_none());
        assert!(app.result_inspection().is_none());
        assert!(app.baseline_path().is_none());
        assert!(app.comparison().is_none());
        assert!(app.report_path().is_none());
        assert!(app.report().is_none());
        assert!(app.csv_export().is_empty());
    }

    /// Verify comparison policy defaults and toggles stay visible and explicit.
    #[test]
    fn comparison_policy_state_is_explicit() {
        let mut app = App::new();
        assert_eq!(app.threshold_percent(), "1.0");
        assert_eq!(app.alpha(), "0.05");
        assert!(!app.one_sided());
        assert!(!app.fail_on_regression());
        app.set_threshold_percent("2.5");
        app.set_alpha("0.01");
        assert!(app.toggle_one_sided());
        assert!(app.toggle_fail_on_regression());
        assert_eq!(app.threshold_percent(), "2.5");
        assert_eq!(app.alpha(), "0.01");
        assert!(app.one_sided());
        assert!(app.fail_on_regression());
    }

    /// Verify an operation-field editor carries its destination and prompt.
    #[test]
    fn begin_input_opens_an_operation_field_editor() {
        let mut app = App::new();
        app.begin_input(InputTarget::BaselinePath, "old.json", "baseline");
        let input = app.input().expect("operation editor is open");
        assert_eq!(input.target(), InputTarget::BaselinePath);
        assert_eq!(input.prompt(), "baseline [old.json] = ");
        assert!(input.buffer().is_empty());
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
        assert_eq!(
            taken.target(),
            crate::app::InputTarget::Config(crate::config::EditableField::ProfileName)
        );
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

    /// Verify replacing the config discards a pending priority decision.
    #[test]
    fn config_replacement_discards_pending_priority() {
        let mut app = App::new();
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        assert!(!app.request_priority_change(true));
        assert_eq!(app.pending_priority(), Some(true));

        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        assert_eq!(app.pending_priority(), None);

        assert!(!app.request_priority_change(true));
        assert!(app.set_config_json("not json").is_err());
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
