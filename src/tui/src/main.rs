// SPDX-License-Identifier: MIT
//! Terminal entry point for the optional ztick-bench front end.
//!
//! This binary contains no benchmark logic. It copies read-only envelopes from
//! the private C11 boundary, renders them with ratatui and processes navigation
//! input until the user quits. Editing, live runs and result workflows land in
//! later Phase 5 tasks.

use std::io;
use std::time::Duration;

use ratatui::crossterm::event::{self, Event, KeyCode, KeyEventKind, KeyModifiers};

use ztick_tui::app::{App, Screen};
use ztick_tui::data::{DashboardData, PluginInspection};
use ztick_tui::frontend::{FrontendError, Session};
use ztick_tui::ui;

/// Poll interval used when no terminal input is waiting.
const POLL_INTERVAL: Duration = Duration::from_millis(200);

/// Run the terminal application until the user requests exit.
fn main() -> io::Result<()> {
    let session = Session::new().map_err(io::Error::other)?;
    let mut app = App::new();
    refresh_dashboard(&session, &mut app);

    ratatui::run(|terminal| {
        loop {
            consume_events(&session, &mut app);
            terminal.draw(|frame| ui::render(frame, &app))?;
            if !event::poll(POLL_INTERVAL)? {
                continue;
            }
            match event::read()? {
                Event::Key(key) if key.kind == KeyEventKind::Press => match key.code {
                    KeyCode::Char('q') | KeyCode::Esc => break Ok(()),
                    KeyCode::Char('c') if key.modifiers.contains(KeyModifiers::CONTROL) => {
                        break Ok(());
                    }
                    KeyCode::Char('1') => app.set_screen(Screen::Dashboard),
                    KeyCode::Char('2') => app.set_screen(Screen::System),
                    KeyCode::Char('3') => {
                        app.set_screen(Screen::Integrations);
                        refresh_inspection(&session, &mut app);
                    }
                    KeyCode::Tab => {
                        app.next_screen();
                        refresh_screen(&session, &mut app);
                    }
                    KeyCode::BackTab => {
                        app.previous_screen();
                        refresh_screen(&session, &mut app);
                    }
                    KeyCode::Down | KeyCode::Char('j') if app.screen() == Screen::Integrations => {
                        app.select_next_plugin();
                        refresh_inspection(&session, &mut app);
                    }
                    KeyCode::Up | KeyCode::Char('k') if app.screen() == Screen::Integrations => {
                        app.select_previous_plugin();
                        refresh_inspection(&session, &mut app);
                    }
                    KeyCode::Char('r') => {
                        refresh_dashboard(&session, &mut app);
                        refresh_screen(&session, &mut app);
                    }
                    _ => {}
                },
                Event::Resize(_, _) => {}
                _ => {}
            }
        }
    })
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
}

/// Load detail for the active screen when it needs boundary data.
fn refresh_screen(session: &Session, app: &mut App) {
    if app.screen() == Screen::Integrations {
        refresh_inspection(session, app);
    }
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
}

/// Unit tests for terminal event handling and refresh helpers.
#[cfg(test)]
mod tests {
    use super::{
        apply_dashboard_result, apply_inspection_result, consume_events, refresh_dashboard,
        refresh_screen,
    };
    use ztick_tui::app::{App, RunState, Screen};
    use ztick_tui::data::DashboardData;
    use ztick_tui::frontend::{FrontendError, Session};

    /// Build dashboard data with one plugin from valid envelopes.
    fn test_data() -> DashboardData {
        let system = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{"cpu_model":{"status":"value","value":"Test CPU","reason":""}}},"error":""}"#;
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[]},"error":""}"#;
        let plugins = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[],"trust":{"status":"available","reason":"","entries":[]},"plugins":[{"path":"/app.so","source":"explicit_path","status":"success","reason":"","identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7c","trust":"untrusted"}]},"error":""}"#;
        DashboardData::from_json(system, list, plugins)
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
            .dashboard_data()
            .expect("dashboard envelopes are copied");
        consume_events(&session, &mut app);
        assert_eq!(app.state(), RunState::Complete);
        assert!(session.poll_event().expect("queue is drained").is_none());
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
}
