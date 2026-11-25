// SPDX-License-Identifier: MIT
//! Terminal entry point for the optional ztick-bench front end.
//!
//! This binary contains no benchmark logic. It copies read-only envelopes from
//! the private C11 boundary, edits portable configs through the same boundary,
//! renders ratatui screens and processes navigation input until the user quits.
//! Live runs and result workflows land in later Phase 5 tasks.

use std::io;
use std::time::Duration;

use ratatui::crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};

use ztick_tui::app::{App, Screen};
use ztick_tui::config::{DEFAULT_CONFIG_JSON, EditableField};
use ztick_tui::data::{DashboardData, PluginInspection, PortabilityReport};
use ztick_tui::frontend::{FrontendError, Session};
use ztick_tui::profile;
use ztick_tui::ui;

/// Poll interval used when no terminal input is waiting.
const POLL_INTERVAL: Duration = Duration::from_millis(200);

/// Run the terminal application until the user requests exit.
fn main() -> io::Result<()> {
    let session = Session::new().map_err(io::Error::other)?;
    let mut app = App::new();
    match session.parse_config(DEFAULT_CONFIG_JSON) {
        Ok(text) => {
            if let Err(error) = app.set_config_json(&text) {
                app.set_status(format!("default config decode failed: {error}"));
            }
        }
        Err(error) => app.set_status(format!("default config parse failed: {error}")),
    }
    refresh_dashboard(&session, &mut app);
    refresh_profiles(&mut app);

    ratatui::run(|terminal| {
        loop {
            consume_events(&session, &mut app);
            consume_run_result(&session, &mut app);
            terminal.draw(|frame| ui::render(frame, &app))?;
            if !event::poll(POLL_INTERVAL)? {
                continue;
            }
            match event::read()? {
                Event::Key(key) if key.kind == KeyEventKind::Press => {
                    if handle_key(&session, &mut app, key) {
                        break Ok(());
                    }
                }
                Event::Resize(_, _) => {}
                _ => {}
            }
        }
    })
}

/// Apply one key press to the application and return true when it requests quit.
///
/// Text input is handled first so ordinary characters cannot trigger screen
/// shortcuts while an editor is open. Every config edit crosses the private
/// boundary through [`Session::apply_overrides`].
fn handle_key(session: &Session, app: &mut App, key: KeyEvent) -> bool {
    if key.modifiers.contains(KeyModifiers::CONTROL) {
        match key.code {
            KeyCode::Char('c') => return true,
            KeyCode::Char('r') => {
                start_run(session, app);
                return false;
            }
            KeyCode::Char('s') => {
                save_current_profile(session, app);
                return false;
            }
            KeyCode::Char('o') => {
                load_selected_profile(session, app);
                return false;
            }
            _ => {}
        }
    }

    if app.input().is_some() {
        match key.code {
            KeyCode::Esc => {
                app.cancel_input();
                app.set_status("edit cancelled");
            }
            KeyCode::Backspace => app.pop_input_char(),
            KeyCode::Enter => apply_input(session, app),
            KeyCode::Char(character) => app.push_input_char(character),
            _ => {}
        }
        return false;
    }

    match key.code {
        KeyCode::Char('q') | KeyCode::Esc => true,
        KeyCode::Char(character) if character.is_ascii_digit() => {
            let index = character.to_digit(10).expect("digit is valid") as usize;
            if index >= 1 && index <= Screen::ALL.len() {
                app.set_screen(Screen::ALL[index - 1]);
                refresh_screen(session, app);
            }
            false
        }
        KeyCode::Tab => {
            app.next_screen();
            refresh_screen(session, app);
            false
        }
        KeyCode::BackTab => {
            app.previous_screen();
            refresh_screen(session, app);
            false
        }
        KeyCode::Down | KeyCode::Char('j') => {
            select_next(session, app);
            false
        }
        KeyCode::Up | KeyCode::Char('k') => {
            select_previous(session, app);
            false
        }
        KeyCode::Left => {
            cycle_selected_field(session, app, false);
            false
        }
        KeyCode::Right | KeyCode::Char('+') | KeyCode::Char('=') => {
            cycle_selected_field(session, app, true);
            false
        }
        KeyCode::Char('-') => {
            cycle_selected_field(session, app, false);
            false
        }
        KeyCode::Enter => {
            if let Some(field) = app.selected_editable_field()
                && !app.begin_field_edit(field)
            {
                app.set_status("no config is available to edit");
            }
            false
        }
        KeyCode::Char('p') if app.screen() == Screen::Scheduling => {
            confirm_priority_change(session, app);
            false
        }
        KeyCode::Char('r') => {
            refresh_dashboard(session, app);
            refresh_screen(session, app);
            false
        }
        _ => false,
    }
}

/// Move the active screen's selection forward.
fn select_next(session: &Session, app: &mut App) {
    match app.screen() {
        Screen::Integrations => {
            app.select_next_plugin();
            refresh_inspection(session, app);
            refresh_portability(app);
        }
        Screen::Profiles => app.select_next_profile(),
        _ => app.select_next_field(),
    }
}

/// Move the active screen's selection backward.
fn select_previous(session: &Session, app: &mut App) {
    match app.screen() {
        Screen::Integrations => {
            app.select_previous_plugin();
            refresh_inspection(session, app);
            refresh_portability(app);
        }
        Screen::Profiles => app.select_previous_profile(),
        _ => app.select_previous_field(),
    }
}

/// Cycle the selected editable field and apply the generated override.
fn cycle_selected_field(session: &Session, app: &mut App, forward: bool) {
    let Some(config) = app.config() else {
        app.set_status("no config is available to edit");
        return;
    };
    let Some(field) = app.selected_editable_field() else {
        return;
    };
    let options = ztick_tui::config::EditOptions {
        integration_ids: app
            .data()
            .plugins()
            .plugins()
            .iter()
            .filter(|plugin| plugin.has_metadata())
            .map(|plugin| plugin.identifier().to_string())
            .collect(),
    };
    let Some(assignments) = field.cycle_assignments(config, forward, &options) else {
        if matches!(
            field,
            EditableField::PriorityOptIn | EditableField::PriorityValue
        ) {
            app.set_status("higher priority requires p and a second confirmation");
        } else {
            app.set_status(format!("{} has no discovered alternative", field.label()));
        }
        return;
    };
    apply_assignments(session, app, &assignments);
}

/// Commit the active text editor through the private boundary.
fn apply_input(session: &Session, app: &mut App) {
    let Some(input) = app.take_input() else {
        return;
    };
    let Some(config) = app.config() else {
        app.set_status("no config is available to edit");
        return;
    };
    let assignments = match input
        .target()
        .assignment_batch_from_input(input.buffer(), config)
    {
        Ok(assignments) => assignments,
        Err(error) => {
            app.set_status(format!("invalid {}: {}", input.target().label(), error));
            return;
        }
    };
    apply_assignments(session, app, &assignments);
}

/// Apply one semantically validated override for tests and single-value flows.
fn apply_assignment(session: &Session, app: &mut App, assignment: &str) {
    apply_assignments(session, app, &[assignment.to_string()]);
}

/// Apply one override batch, refresh the typed config and recheck portability.
fn apply_assignments(session: &Session, app: &mut App, assignments: &[String]) {
    match session.apply_overrides(assignments) {
        Ok(text) => {
            if let Err(error) = app.set_config_json(&text) {
                app.set_status(format!("config decode failed: {error}"));
                return;
            }
            app.set_validation_note("validated by core planner");
            refresh_portability(app);
            let summary = assignments.join(", ");
            if app.portability().is_allowed() {
                app.set_status(format!("applied {summary}"));
            } else {
                app.set_status(format!(
                    "applied {summary}; config is blocked by portability findings"
                ));
            }
        }
        Err(error) => {
            app.set_validation_note("last edit rejected; current config unchanged");
            app.set_status(format!("override rejected: {error}"));
        }
    }
}

/// Require a second identical key press before changing priority opt-in.
fn confirm_priority_change(session: &Session, app: &mut App) {
    let Some(config) = app.config() else {
        app.set_status("no config is available to edit");
        return;
    };
    let desired = !config.priority_opt_in();
    if !app.request_priority_change(desired) {
        app.set_status(format!(
            "priority opt-in {desired} is pending; press p again to confirm"
        ));
        return;
    }
    apply_assignment(
        session,
        app,
        &format!("parallelism.priority_opt_in={desired}"),
    );
}

/// Validate and save the active named profile only when portability allows it.
fn save_current_profile(session: &Session, app: &mut App) {
    if !app.portability().is_allowed() {
        let detail = app
            .portability()
            .blockers()
            .first()
            .map(|finding| finding.message())
            .unwrap_or("portability findings are present");
        app.set_status(format!("save BLOCKED: {detail}"));
        return;
    }
    if let Err(error) = session.validate_config() {
        app.set_validation_note("invalid");
        app.set_status(format!("save blocked by validation: {error}"));
        return;
    }
    app.set_validation_note("valid");
    let Some(config) = app.config() else {
        app.set_status("save blocked: no config is retained");
        return;
    };
    let name = config.profile_name().to_string();
    let text = config.text().to_string();
    match profile::save(app.profile_directory(), &name, &text) {
        Ok(path) => {
            refresh_profiles(app);
            app.select_saved_profile_path(&path);
            app.set_status(format!(
                "saved named profile '{name}' to {}",
                path.display()
            ));
        }
        Err(error) => app.set_status(format!("profile save failed: {error}")),
    }
}

/// Load the selected named profile without adapting unsupported capabilities.
fn load_selected_profile(session: &Session, app: &mut App) {
    let Some(path) = app
        .selected_saved_profile()
        .map(|value| value.path().to_path_buf())
    else {
        app.set_status("no saved profile is selected");
        return;
    };
    let path_text = path.to_string_lossy().to_string();
    match session.load_config(&path_text) {
        Ok(text) => {
            if let Err(error) = app.set_config_json(&text) {
                app.set_status(format!("profile decode failed: {error}"));
                return;
            }
            app.set_validation_note("loaded; validated by core planner");
            refresh_portability(app);
            if app.portability().is_allowed() {
                app.set_status(format!("loaded profile from {}", path.display()));
            } else {
                app.set_status(format!(
                    "loaded profile from {} but it is BLOCKED by portability findings",
                    path.display()
                ));
            }
        }
        Err(error) => app.set_status(format!("profile load failed: {error}")),
    }
}

/// Start one asynchronous run from the currently retained portable config.
///
/// A blocked portability report refuses execution instead of adapting an
/// unsupported capability. The config is saved under its profile name so the
/// private boundary receives a stable document path.
fn start_run(session: &Session, app: &mut App) {
    if !app.portability().is_allowed() {
        let detail = app
            .portability()
            .blockers()
            .first()
            .map(|finding| finding.message())
            .unwrap_or("portability findings are present");
        app.set_status(format!("run BLOCKED: {detail}"));
        return;
    }
    let Some(config) = app.config() else {
        app.set_status("run blocked: no config is retained");
        return;
    };
    let name = config.profile_name().to_string();
    let text = config.text().to_string();
    let path = match profile::save(app.profile_directory(), &name, &text) {
        Ok(path) => path,
        Err(error) => {
            app.set_status(format!("run config save failed: {error}"));
            return;
        }
    };
    let path_text = path.to_string_lossy().to_string();
    match session.start_run(&path_text) {
        Ok(()) => {
            app.begin_run(&path_text);
            app.set_screen(Screen::Execution);
            app.set_status(format!("run started from {}", path.display()));
        }
        Err(error) => app.set_status(format!("run start failed: {error}")),
    }
}

/// Copy one completed asynchronous result envelope into the execution view.
fn consume_run_result(session: &Session, app: &mut App) {
    if !app.progress().result_status.is_empty() {
        return;
    }
    match session.run_result() {
        Ok(Some(json)) => app.set_run_result(&json),
        Ok(None) => {}
        Err(error) => app.set_status(format!("run result read failed: {error}")),
    }
}

/// Apply all queued boundary events to the displayed run state and status.
///
/// Queue reads stop at the first boundary error so the error remains visible
/// and no later event silently replaces it.
fn consume_events(session: &Session, app: &mut App) {
    loop {
        match session.poll_event() {
            Ok(Some(event)) => app.observe_event(&event),
            Ok(None) => break,
            Err(error) => {
                app.set_status(format!("progress event read failed: {error}"));
                break;
            }
        }
    }
}

/// Apply one dashboard load result without discarding already-loaded data.
fn apply_dashboard_result(result: Result<DashboardData, FrontendError>, app: &mut App) {
    match result {
        Ok(data) => {
            if !data.load_error().is_empty() {
                app.set_status(format!("dashboard load warnings: {}", data.load_error()));
            }
            app.set_dashboard_data(data);
        }
        Err(error) => app.set_status(format!("dashboard refresh failed: {error}")),
    }
}

/// Reload all dashboard envelopes and record decode failures visibly.
fn refresh_dashboard(session: &Session, app: &mut App) {
    apply_dashboard_result(session.dashboard_data(), app);
    refresh_portability(app);
}

/// Load detail for the active screen when it needs boundary data.
fn refresh_screen(session: &Session, app: &mut App) {
    match app.screen() {
        Screen::Integrations => refresh_inspection(session, app),
        Screen::Profiles => refresh_profiles(app),
        _ => {}
    }
}

/// Reload the saved profile list from the configured profile directory.
fn refresh_profiles(app: &mut App) {
    app.set_saved_profiles(profile::list(app.profile_directory()));
}

/// Recompute portability findings from the retained config and inspection.
///
/// A missing config is itself a blocker; no missing or unsupported capability
/// is adapted or hidden.
fn refresh_portability(app: &mut App) {
    let Some(config) = app.config() else {
        app.set_portability(PortabilityReport::blocked("no config is retained"));
        return;
    };
    let report = PortabilityReport::evaluate(
        config,
        app.selected_plugin(),
        app.inspection(),
        app.data().system(),
    );
    app.set_portability(report);
}

/// Apply one plugin inspection result to the selected candidate.
fn apply_inspection_result(result: Result<PluginInspection, FrontendError>, app: &mut App) {
    match result {
        Ok(inspection) => app.set_inspection(Some(inspection)),
        Err(error) => {
            app.set_status(format!("plugin inspection failed: {error}"));
            app.set_inspection(None);
        }
    }
}

/// Load metadata-only inspection for the selected plugin, if any.
fn refresh_inspection(session: &Session, app: &mut App) {
    let Some(path) = app
        .selected_plugin()
        .map(|plugin| plugin.path().to_string())
    else {
        app.set_inspection(None);
        return;
    };
    apply_inspection_result(session.plugin_inspect(&path), app);
    refresh_portability(app);
}

/// Unit tests for terminal event handling and refresh helpers.
#[cfg(test)]
mod tests {
    use std::time::Duration;

    use ratatui::crossterm::event::{KeyCode, KeyEvent, KeyModifiers};

    use super::{
        apply_assignment, apply_dashboard_result, apply_inspection_result, consume_events,
        consume_run_result, handle_key, refresh_dashboard, refresh_portability, refresh_profiles,
        refresh_screen, save_current_profile,
    };
    use ztick_tui::app::{App, RunState, Screen};
    use ztick_tui::config::{DEFAULT_CONFIG_JSON, EditableField};
    use ztick_tui::data::{DashboardData, PluginInspection, PortabilityReport};
    use ztick_tui::frontend::{FrontendError, Session};
    use ztick_tui::profile::{self, SavedProfile};

    /// Build dashboard data with one plugin from valid envelopes.
    fn test_data() -> DashboardData {
        let system = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{"cpu_model":{"status":"value","value":"Test CPU","reason":""}}},"error":""}"#;
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[]},"error":""}"#;
        let plugins = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[],"trust":{"status":"available","reason":"","entries":[]},"plugins":[{"path":"/app.so","source":"explicit_path","status":"success","reason":"","identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7c","trust":"untrusted"}]},"error":""}"#;
        DashboardData::from_json(system, list, plugins)
    }

    /// Build an application with the default portable config retained.
    fn configured_app() -> App {
        let mut app = App::new();
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        app
    }

    /// Build one key press with no modifiers.
    fn key(code: KeyCode) -> KeyEvent {
        KeyEvent::new(code, KeyModifiers::NONE)
    }

    /// Verify dashboard load errors never erase already-loaded data.
    #[test]
    fn apply_dashboard_result_keeps_existing_data_on_error() {
        let mut app = App::new();
        let data = test_data();
        app.set_dashboard_data(data.clone());
        apply_dashboard_result(Err(FrontendError::new(3, "boundary failed")), &mut app);
        assert_eq!(app.data(), &data);
        assert!(app.status().contains("dashboard refresh failed"));
        assert!(app.status().contains("boundary failed"));
    }

    /// Verify partial dashboard failures stay visible beside good sections.
    #[test]
    fn apply_dashboard_result_records_partial_load_errors() {
        let failed_system = r#"{"schema_version":1,"command":"system","status":"failure","exit_code":1,"data":{"fields":{"system_inventory":{"status":"unavailable","value":"","reason":"collector denied"}}},"error":"collector denied"}"#;
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[]},"error":""}"#;
        let plugins = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[],"trust":{"status":"available","reason":"","entries":[]},"plugins":[]},"error":""}"#;
        let data = DashboardData::from_json(failed_system, list, plugins);
        let mut app = App::new();
        apply_dashboard_result(Ok(data), &mut app);
        assert!(app.status().contains("dashboard load warnings"));
        assert!(app.status().contains("system reported failure"));
        assert!(app.data().system().field("system_inventory").is_some());
    }

    /// Verify inspection errors clear stale detail and record a status.
    #[test]
    fn apply_inspection_result_reports_error() {
        let mut app = App::new();
        let inspection = ztick_tui::data::PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"","metadata":{},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_inspection(Some(inspection));
        apply_inspection_result(Err(FrontendError::new(4, "not found")), &mut app);
        assert!(app.inspection().is_none());
        assert!(app.status().contains("plugin inspection failed"));
    }

    /// Verify queued boundary events update the displayed run state.
    #[test]
    fn consume_events_applies_queued_events() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = App::new();
        assert_eq!(app.state(), RunState::Idle);
        let _ = session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("config parsing enqueues a configuring event");
        consume_events(&session, &mut app);
        assert_eq!(app.state(), RunState::Configuring);
        assert!(session.poll_event().expect("queue is drained").is_none());
    }

    /// Verify read-only dashboard envelopes do not overwrite an active run state.
    #[test]
    fn consume_events_ignores_read_only_envelopes() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = App::new();
        app.begin_run("/tmp/ztick-run.json");
        let _ = session
            .dashboard_data()
            .expect("dashboard envelopes are copied");
        consume_events(&session, &mut app);
        assert_eq!(app.state(), RunState::Validating);
    }

    /// Verify live dashboard refresh populates the application data.
    #[test]
    fn refresh_dashboard_loads_live_envelopes() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = App::new();
        refresh_dashboard(&session, &mut app);
        assert!(!app.data().system().fields().is_empty());
        assert!(!app.data().plugins().directories().is_empty());
    }

    /// Verify non-integration screens do not trigger plugin inspection.
    #[test]
    fn refresh_screen_ignores_non_integration_screens() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = App::new();
        app.set_screen(Screen::Dashboard);
        refresh_screen(&session, &mut app);
        assert!(app.inspection().is_none());
    }

    /// Verify a dashboard refresh preserves an inspection-backed blocker.
    #[test]
    fn dashboard_refresh_keeps_inspection_backed_blocker() {
        let mut app = configured_app();
        let data = test_data();
        app.set_dashboard_data(data.clone());
        let inspection = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"","metadata":{},"compatibility":{"status":"failure","reason":"unsupported on this host"},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_inspection(Some(inspection));
        refresh_portability(&mut app);
        assert!(
            app.portability()
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("unsupported on this host"))
        );

        apply_dashboard_result(Ok(data), &mut app);
        refresh_portability(&mut app);
        assert!(app.inspection().is_some());
        assert!(
            app.portability()
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("unsupported on this host"))
        );
    }

    /// Verify number keys select registered screens without quitting.
    #[test]
    fn number_keys_select_registered_screens() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        for (index, expected) in Screen::ALL.iter().enumerate() {
            let digit = char::from_digit((index + 1) as u32, 10).expect("index is a digit");
            assert!(!handle_key(&session, &mut app, key(KeyCode::Char(digit))));
            assert_eq!(app.screen(), *expected);
        }
        assert!(handle_key(&session, &mut app, key(KeyCode::Char('q'))));
    }

    /// Verify right arrow applies a documented field override through the boundary.
    #[test]
    fn right_arrow_applies_a_field_override() {
        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        app.set_screen(Screen::Configuration);
        assert!(!handle_key(&session, &mut app, key(KeyCode::Right)));
        assert_eq!(
            app.config().expect("config remains").sample_rate_hz(),
            Some(96000.0)
        );
    }

    /// Verify typed editor input commits a quoted portable profile name.
    #[test]
    fn typed_editor_input_commits_a_quoted_profile_name() {
        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        app.set_screen(Screen::Profiles);
        assert!(app.begin_field_edit(EditableField::ProfileName));
        for character in "Rename Me".chars() {
            app.push_input_char(character);
        }
        assert!(!handle_key(&session, &mut app, key(KeyCode::Enter)));
        assert_eq!(
            app.config().expect("config remains").profile_name(),
            "Rename Me"
        );
        assert!(app.input().is_none());
    }

    /// Verify higher priority needs a second explicit confirmation.
    #[test]
    fn higher_priority_requires_a_second_explicit_confirmation() {
        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        app.set_screen(Screen::Scheduling);
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('p'))));
        assert_eq!(app.pending_priority(), Some(true));
        assert!(!app.config().expect("config remains").priority_opt_in());
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('p'))));
        assert_eq!(app.pending_priority(), None);
        assert!(app.config().expect("config remains").priority_opt_in());
        assert!(
            app.portability()
                .warnings()
                .iter()
                .any(|finding| finding.message().contains("explicitly accepted"))
        );
    }

    /// Verify a pending priority confirmation does not survive a profile load.
    #[test]
    fn profile_load_discards_pending_priority_confirmation() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        app.set_screen(Screen::Scheduling);
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-priority-load-{nonce}"));
        let path = profile::save(&directory, "Other", DEFAULT_CONFIG_JSON)
            .expect("named profile is saved");
        app.set_profile_directory(&directory);
        app.set_saved_profiles(vec![SavedProfile::new("Other", path)]);

        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('p'))));
        assert_eq!(app.pending_priority(), Some(true));
        assert!(!handle_key(
            &session,
            &mut app,
            KeyEvent::new(KeyCode::Char('o'), KeyModifiers::CONTROL)
        ));
        assert_eq!(app.pending_priority(), None);
        assert!(!app.config().expect("config remains").priority_opt_in());

        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('p'))));
        assert_eq!(app.pending_priority(), Some(true));
        assert!(!app.config().expect("config remains").priority_opt_in());

        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }

    /// Verify core-invalid overrides are rejected without mutating the session config.
    #[test]
    fn core_invalid_override_is_rejected_without_mutating_config() {
        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        apply_assignment(&session, &mut app, "dimensions.channels=0");
        assert_eq!(app.config().expect("config remains").channels(), Some(2));
        assert_eq!(
            app.validation_note(),
            "last edit rejected; current config unchanged"
        );
        assert!(app.status().contains("override rejected"));
    }

    /// Verify typed priority entries cannot bypass the explicit opt-in flow.
    #[test]
    fn typed_priority_cannot_bypass_explicit_opt_in() {
        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        app.set_screen(Screen::Scheduling);
        assert!(app.begin_field_edit(EditableField::PriorityValue));
        app.push_input_char('5');
        assert!(!handle_key(&session, &mut app, key(KeyCode::Enter)));
        assert_eq!(app.config().expect("config remains").priority(), Some(0));
        assert!(app.status().contains("higher priority requires p"));
    }

    /// Verify a blocked config cannot be saved as a named profile.
    #[test]
    fn blocked_config_cannot_be_saved() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-blocked-save-{nonce}"));
        app.set_profile_directory(&directory);
        refresh_dashboard(&session, &mut app);
        assert!(!app.portability().is_allowed());

        save_current_profile(&session, &mut app);
        assert!(app.status().contains("save BLOCKED"));
        assert!(!directory.exists());
    }

    /// Verify an allowed config saves a named portable profile document.
    #[test]
    fn allowed_config_saves_a_named_portable_profile() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-allowed-save-{nonce}"));
        app.set_profile_directory(&directory);
        app.set_portability(PortabilityReport::default());
        apply_assignment(&session, &mut app, "profile.name=\"Saved\"");
        app.set_portability(PortabilityReport::default());

        save_current_profile(&session, &mut app);
        let path = directory.join("Saved.json");
        let text = std::fs::read_to_string(&path).expect("named profile is saved");
        assert!(text.contains("\"config_version\":\"1.0.0\""));
        assert!(text.contains("\"name\":\"Saved\""));
        assert_eq!(app.validation_note(), "valid");
        refresh_profiles(&mut app);
        assert_eq!(app.saved_profiles()[0].name(), "Saved");

        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }

    /// Verify Ctrl+R refuses to start a run without a retained config.
    #[test]
    fn control_r_blocks_a_run_without_a_config() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = App::new();
        app.set_portability(PortabilityReport::default());
        assert!(!handle_key(
            &session,
            &mut app,
            KeyEvent::new(KeyCode::Char('r'), KeyModifiers::CONTROL)
        ));
        assert_eq!(app.screen(), Screen::Dashboard);
        assert!(app.status().contains("run blocked: no config"));
    }

    /// Verify Ctrl+R starts an allowed run and the result reaches the execution view.
    #[test]
    fn control_r_starts_an_allowed_async_run() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-ctrl-r-{nonce}"));
        app.set_profile_directory(&directory);
        app.set_portability(PortabilityReport::default());

        assert!(!handle_key(
            &session,
            &mut app,
            KeyEvent::new(KeyCode::Char('r'), KeyModifiers::CONTROL)
        ));
        assert_eq!(app.screen(), Screen::Execution);
        assert_eq!(app.state(), RunState::Validating);
        assert!(app.status().contains("run started from"));
        assert!(app.progress().config_path.contains("Quick.json"));

        for _ in 0..500 {
            consume_events(&session, &mut app);
            consume_run_result(&session, &mut app);
            if !app.progress().result_status.is_empty() {
                break;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        assert!(
            app.progress().result_status.contains("success"),
            "the copied run envelope reaches the execution screen"
        );
        assert_eq!(app.state(), RunState::Complete);
        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }
}
