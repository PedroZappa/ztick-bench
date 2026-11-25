// SPDX-License-Identifier: MIT
//! Terminal entry point for the optional ztick-bench front end.
//!
//! This binary contains no benchmark logic. It copies read-only envelopes from
//! the private C11 boundary, edits portable configs through the same boundary,
//! renders ratatui screens and processes navigation input until the user quits.
//! P5.4 carries live runs; P5.5 reads stored results; P5.6 drives comparison,
//! report, export and session-bundle workflows without recomputing metrics.

use std::io;
use std::path::{Path, PathBuf};
use std::time::Duration;

use ratatui::crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};

use ztick_tui::app::{App, InputTarget, Screen};
use ztick_tui::config::{DEFAULT_CONFIG_JSON, EditableField};
use ztick_tui::data::{
    ComparisonInspection, DashboardData, PluginInspection, PortabilityReport, ReportInspection,
    ResultInspection, envelope_failure_note,
};
use ztick_tui::frontend::{FrontendError, Session};
use ztick_tui::profile;
use ztick_tui::session_bundle::{SessionBundle, write_atomic};
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
    if app.trust_prompt().is_some() {
        match key.code {
            KeyCode::Char('y') | KeyCode::Char('Y') | KeyCode::Enter => {
                resolve_trust_prompt(session, app, true);
            }
            KeyCode::Char('n') | KeyCode::Char('N') | KeyCode::Esc => {
                resolve_trust_prompt(session, app, false);
            }
            KeyCode::Char('q') => return true,
            _ => {}
        }
        return false;
    }
    if app.input().is_some() {
        match key.code {
            KeyCode::Esc => {
                app.cancel_input();
                app.set_status("edit cancelled");
            }
            KeyCode::Backspace => app.pop_input_char(),
            KeyCode::Enter => apply_input(session, app),
            KeyCode::Char(character) if !key.modifiers.contains(KeyModifiers::CONTROL) => {
                app.push_input_char(character);
            }
            _ => {}
        }
        return false;
    }

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
            KeyCode::Char('e') => {
                execute_screen_action(session, app);
                return false;
            }
            KeyCode::Char('b') => {
                export_session_bundle(session, app);
                return false;
            }
            KeyCode::Char('i') => {
                import_session_bundle(app);
                return false;
            }
            _ => {}
        }
    }

    match key.code {
        KeyCode::Char('q') | KeyCode::Esc => true,
        KeyCode::Char(character) if character.is_ascii_digit() => {
            let digit = character.to_digit(10).expect("digit is valid") as usize;
            let screen = if digit == 0 {
                Some(Screen::Results)
            } else if digit <= Screen::ALL.len() {
                Some(Screen::ALL[digit - 1])
            } else {
                None
            };
            if let Some(screen) = screen {
                app.set_screen(screen);
                refresh_screen(session, app);
            }
            false
        }
        KeyCode::Char('c') => {
            app.set_screen(Screen::Compare);
            refresh_screen(session, app);
            false
        }
        KeyCode::Char('e') => {
            app.set_screen(Screen::Report);
            refresh_screen(session, app);
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
        KeyCode::Char('b') if app.screen() == Screen::Compare => {
            let current = display_path(app.baseline_path());
            app.begin_input(InputTarget::BaselinePath, current, "baseline result");
            false
        }
        KeyCode::Char('n') if app.screen() == Screen::Compare => {
            let current = display_path(app.candidate_path());
            app.begin_input(InputTarget::CandidatePath, current, "candidate result");
            false
        }
        KeyCode::Char('t') if app.screen() == Screen::Compare => {
            let current = app.threshold_percent().to_string();
            app.begin_input(InputTarget::ThresholdPercent, current, "threshold percent");
            false
        }
        KeyCode::Char('a') if app.screen() == Screen::Compare => {
            let current = app.alpha().to_string();
            app.begin_input(InputTarget::Alpha, current, "alpha");
            false
        }
        KeyCode::Char('s') if app.screen() == Screen::Compare => {
            let sided = app.toggle_one_sided();
            app.set_comparison(None);
            app.set_status(format!(
                "comparison sidedness: {}",
                if sided { "one-sided" } else { "two-sided" }
            ));
            refresh_comparison(session, app);
            false
        }
        KeyCode::Char('f') if app.screen() == Screen::Compare => {
            let enabled = app.toggle_fail_on_regression();
            app.set_comparison(None);
            app.set_status(format!(
                "regression exit code mapping: {}",
                if enabled { "enabled" } else { "disabled" }
            ));
            refresh_comparison(session, app);
            false
        }
        KeyCode::Char('f') if app.screen() == Screen::Report => {
            let current = display_path(app.report_path());
            app.begin_input(InputTarget::ReportPath, current, "report result");
            false
        }
        KeyCode::Char('m') if app.screen() == Screen::Report => {
            let mode = app.cycle_csv_row_mode().to_string();
            app.set_csv_export(String::new());
            app.set_status(format!("CSV row mode: {mode}"));
            refresh_report(session, app);
            false
        }
        KeyCode::Char('g') if app.screen() == Screen::Report => {
            let current = display_path(app.json_export_path());
            app.begin_input(InputTarget::JsonExportPath, current, "JSON export path");
            false
        }
        KeyCode::Char('y') if app.screen() == Screen::Report => {
            let current = display_path(app.csv_export_path());
            app.begin_input(InputTarget::CsvExportPath, current, "CSV export path");
            false
        }
        KeyCode::Char('u') if app.screen() == Screen::Report => {
            let current = display_path(app.bundle_path());
            app.begin_input(InputTarget::BundlePath, current, "session bundle path");
            false
        }
        KeyCode::Char('d') if app.screen() == Screen::Report => {
            let current = app.bundle_target_dir().display().to_string();
            app.begin_input(
                InputTarget::BundleTargetDir,
                current,
                "bundle import directory",
            );
            false
        }
        KeyCode::Char('p') if app.screen() == Screen::Scheduling => {
            confirm_priority_change(session, app);
            false
        }
        KeyCode::Char('t') if app.screen() == Screen::Integrations => {
            open_trust_prompt(session, app);
            false
        }
        KeyCode::Char('r') => {
            refresh_dashboard(session, app);
            refresh_screen(session, app);
            false
        }
        KeyCode::Char('T') => {
            let name = app.cycle_theme();
            app.set_status(format!("theme: {name}"));
            false
        }
        _ => false,
    }
}

/// Render one optional path as editable text.
fn display_path(path: Option<&Path>) -> String {
    path.map(|value| value.display().to_string())
        .unwrap_or_default()
}

/// Convert one optional path to a boundary-safe UTF-8 string.
fn path_argument(path: Option<&Path>) -> Result<&str, String> {
    path.ok_or_else(|| "no path is selected".to_string())?
        .to_str()
        .ok_or_else(|| "selected path is not valid UTF-8".to_string())
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

/// Commit the active text editor to its config or P5.6 operation field.
fn apply_input(session: &Session, app: &mut App) {
    let Some(input) = app.take_input() else {
        return;
    };
    let value = input.buffer().to_string();
    match input.target() {
        InputTarget::Config(field) => {
            let Some(config) = app.config() else {
                app.set_status("no config is available to edit");
                return;
            };
            let assignments = match field.assignment_batch_from_input(&value, config) {
                Ok(assignments) => assignments,
                Err(error) => {
                    app.set_status(format!("invalid {}: {}", field.label(), error));
                    return;
                }
            };
            apply_assignments(session, app, &assignments);
        }
        InputTarget::BaselinePath => {
            if value.trim().is_empty() {
                app.set_status("baseline result path is empty");
                return;
            }
            app.set_baseline_path(Some(PathBuf::from(value)));
            app.set_comparison(None);
            app.set_status("baseline result selected");
            refresh_comparison(session, app);
        }
        InputTarget::CandidatePath => {
            if value.trim().is_empty() {
                app.set_status("candidate result path is empty");
                return;
            }
            app.set_candidate_path(Some(PathBuf::from(value)));
            app.set_comparison(None);
            app.set_status("candidate result selected");
            refresh_comparison(session, app);
        }
        InputTarget::ThresholdPercent => {
            if value.trim().is_empty() {
                app.set_status("threshold percent is empty");
                return;
            }
            app.set_threshold_percent(value);
            app.set_comparison(None);
            app.set_status("comparison threshold updated");
            refresh_comparison(session, app);
        }
        InputTarget::Alpha => {
            if value.trim().is_empty() {
                app.set_status("alpha is empty");
                return;
            }
            app.set_alpha(value);
            app.set_comparison(None);
            app.set_status("comparison alpha updated");
            refresh_comparison(session, app);
        }
        InputTarget::ReportPath => {
            if value.trim().is_empty() {
                app.set_status("report result path is empty");
                return;
            }
            app.set_report_path(Some(PathBuf::from(value)));
            app.set_status("report result selected");
            refresh_report(session, app);
        }
        InputTarget::JsonExportPath => {
            if value.trim().is_empty() {
                app.set_status("JSON export path is empty");
                return;
            }
            app.set_json_export_path(Some(PathBuf::from(value)));
            app.set_status("JSON export path selected");
        }
        InputTarget::CsvExportPath => {
            if value.trim().is_empty() {
                app.set_status("CSV export path is empty");
                return;
            }
            app.set_csv_export_path(Some(PathBuf::from(value)));
            app.set_status("CSV export path selected");
        }
        InputTarget::BundlePath => {
            if value.trim().is_empty() {
                app.set_status("session bundle path is empty");
                return;
            }
            app.set_bundle_path(Some(PathBuf::from(value)));
            app.set_status("session bundle path selected");
        }
        InputTarget::BundleTargetDir => {
            if value.trim().is_empty() {
                app.set_status("bundle import directory is empty");
                return;
            }
            app.set_bundle_target_dir(PathBuf::from(value));
            app.set_status("bundle import directory selected");
        }
    }
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
        Ok(Some(json)) => {
            app.set_run_result(&json);
            refresh_result(session, app);
        }
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
        Screen::Results => refresh_result(session, app),
        Screen::Compare => refresh_comparison(session, app),
        Screen::Report => refresh_report(session, app),
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

/// Open the first-run trust confirmation prompt for the selected plugin.
///
/// The prompt is only offered after metadata-only inspection has provided a
/// stable identity, declared capabilities and core compatibility. No plugin code
/// is loaded or executed by this interaction.
fn open_trust_prompt(session: &Session, app: &mut App) {
    if app.screen() != Screen::Integrations {
        app.set_status("trust confirmation is available on Integrations only");
        return;
    }
    let Some(plugin) = app.selected_plugin() else {
        app.set_status("trust confirmation needs a selected plugin");
        return;
    };
    let path = plugin.path().to_string();
    if app
        .inspection()
        .is_none_or(|inspection| inspection.path() != path)
    {
        refresh_inspection(session, app);
    }
    if !app.open_trust_prompt() {
        app.set_status(
            "trust confirmation unavailable: load a plugin inspection with a stable identifier first",
        );
        return;
    }
    let Some(prompt) = app.trust_prompt() else {
        return;
    };
    app.set_status(format!(
        "first-run consent requested for {} ({}); y/Enter grant, n/Esc deny",
        prompt.name(),
        prompt.identifier()
    ));
}

/// Apply an explicit trust decision through the private front-end boundary.
///
/// A denial never authorizes execution and never writes trust. A grant is only
/// treated as success when the boundary records it; dashboard data is refreshed
/// so the rendered trust state reflects the persisted store.
fn resolve_trust_prompt(session: &Session, app: &mut App, granted: bool) {
    let Some(prompt) = app.take_trust_prompt() else {
        return;
    };
    let identifier = prompt.identifier().to_string();
    let name = prompt.name().to_string();
    match session.trust_consent(&identifier, granted) {
        Ok(()) => {
            refresh_dashboard(session, app);
            if granted {
                let trusted = session.trust_query(&identifier).ok();
                if trusted == Some(true) {
                    app.set_status(format!(
                        "trust granted for {name} ({identifier}); execution remains subject to normal run checks"
                    ));
                } else {
                    app.set_status(format!(
                        "trust decision recorded for {name} ({identifier}), but trust state could not be confirmed"
                    ));
                }
            } else {
                app.set_status(format!(
                    "trust denied for {name} ({identifier}); execution remains refused until explicit consent is recorded"
                ));
            }
        }
        Err(error) => {
            app.set_status(format!(
                "trust decision failed for {name} ({identifier}): {error}"
            ));
        }
    }
    refresh_portability(app);
}

/// Apply one result-document read and keep decode failures visible.
fn apply_result_result(result: Result<String, FrontendError>, app: &mut App) {
    match result {
        Ok(text) => match ResultInspection::from_json(&text) {
            Ok(inspection) => app.set_result_inspection(Some(inspection)),
            Err(error) => {
                app.set_result_inspection(None);
                app.set_status(format!("result document decode failed: {error}"));
            }
        },
        Err(error) => {
            app.set_result_inspection(None);
            app.set_status(format!("result document read failed: {error}"));
        }
    }
}

/// Load the typed inspection for the completed run's stored result document.
///
/// The path comes only from a published result envelope. A missing or unreadable
/// document is reported through the app status instead of being shown as a
/// completed result.
fn refresh_result(session: &Session, app: &mut App) {
    if app.result_inspection().is_some() {
        return;
    }
    let Some(path) = app
        .result_path()
        .map(|path| path.to_string_lossy().into_owned())
    else {
        return;
    };
    apply_result_result(session.result_read(&path), app);
}

/// Apply one copied comparison envelope without recomputing any metric.
fn apply_comparison_result(result: Result<String, FrontendError>, app: &mut App) {
    match result {
        Ok(text) => match ComparisonInspection::from_envelope(&text) {
            Ok(inspection) => {
                let status = inspection.envelope_status().to_string();
                app.set_comparison(Some(inspection));
                app.set_status(format!("comparison loaded: {status}"));
            }
            Err(error) => {
                app.set_comparison(None);
                let detail = envelope_failure_note(&text, "compare")
                    .unwrap_or_else(|| format!("comparison decode failed: {error}"));
                app.set_status(detail);
            }
        },
        Err(error) => {
            app.set_comparison(None);
            app.set_status(format!("compare failed: {error}"));
        }
    }
}

/// Ask the core for a policy-driven comparison of the selected stored results.
fn refresh_comparison(session: &Session, app: &mut App) {
    if app.comparison().is_some() {
        return;
    }
    let baseline = match path_argument(app.baseline_path()) {
        Ok(path) => path.to_string(),
        Err(error) => {
            app.set_status(format!("compare needs a baseline: {error}"));
            return;
        }
    };
    let candidate = match path_argument(app.candidate_path()) {
        Ok(path) => path.to_string(),
        Err(error) => {
            app.set_status(format!("compare needs a candidate: {error}"));
            return;
        }
    };
    let result = session.compare_with_policy(
        &baseline,
        &candidate,
        app.threshold_percent(),
        app.alpha(),
        app.one_sided(),
        app.fail_on_regression(),
    );
    apply_comparison_result(result, app);
}

/// Apply one copied report envelope without recomputing its stored statuses.
fn apply_report_envelope(text: &str, app: &mut App) -> bool {
    match ReportInspection::from_envelope(text) {
        Ok(inspection) => {
            let status = inspection.envelope_status().to_string();
            app.set_report(Some(inspection));
            app.set_status(format!("report loaded: {status}"));
            true
        }
        Err(error) => {
            app.set_report(None);
            let detail = envelope_failure_note(text, "report")
                .unwrap_or_else(|| format!("report decode failed: {error}"));
            app.set_status(detail);
            false
        }
    }
}

/// Apply one copied report envelope and its core CSV projection.
fn refresh_report(session: &Session, app: &mut App) {
    let path = match path_argument(app.report_path()) {
        Ok(path) => path.to_string(),
        Err(error) => {
            app.set_status(format!("report needs a result path: {error}"));
            return;
        }
    };
    match session.report(&path) {
        Ok(text) => {
            if !apply_report_envelope(&text, app) {
                return;
            }
        }
        Err(error) => {
            app.set_report(None);
            app.set_status(format!("report read failed: {error}"));
            return;
        }
    }
    match session.result_export_csv(&path, app.csv_row_mode()) {
        Ok(csv) => app.set_csv_export(csv),
        Err(error) => {
            app.set_csv_export(String::new());
            app.set_status(format!("CSV projection failed: {error}"));
        }
    }
}

/// Run the primary action for the active comparison/report screen.
fn execute_screen_action(session: &Session, app: &mut App) {
    match app.screen() {
        Screen::Compare => {
            app.set_comparison(None);
            refresh_comparison(session, app);
        }
        Screen::Report => export_report(session, app),
        _ => app.set_status("Ctrl-E is available on Compare and Report only"),
    }
}

/// Write copied JSON and CSV text to the configured export paths.
fn export_report(session: &Session, app: &mut App) {
    let Some(report) = app.report() else {
        app.set_status("report export needs a loaded report");
        return;
    };
    let envelope = report.envelope_json_text();
    let csv = if app.csv_export().is_empty() {
        let path = match path_argument(app.report_path()) {
            Ok(path) => path.to_string(),
            Err(error) => {
                app.set_status(format!("report export needs a result path: {error}"));
                return;
            }
        };
        match session.result_export_csv(&path, app.csv_row_mode()) {
            Ok(text) => text,
            Err(error) => {
                app.set_status(format!("CSV projection failed: {error}"));
                return;
            }
        }
    } else {
        app.csv_export().to_string()
    };
    let json_path = match app.json_export_path() {
        Some(path) => path.to_path_buf(),
        None => {
            app.set_status("set a JSON export path with g");
            return;
        }
    };
    let csv_path = match app.csv_export_path() {
        Some(path) => path.to_path_buf(),
        None => {
            app.set_status("set a CSV export path with y");
            return;
        }
    };
    if let Err(error) = write_export(&json_path, &envelope, "report JSON") {
        app.set_status(format!("report JSON export failed: {error}"));
        return;
    }
    if let Err(error) = write_export(&csv_path, &csv, "CSV") {
        app.set_status(format!("CSV export failed: {error}"));
        return;
    }
    app.set_status(format!(
        "report exported to {} and {}",
        json_path.display(),
        csv_path.display()
    ));
}

/// Write one copied export to disk through an atomic replace.
fn write_export(path: &Path, text: &str, label: &str) -> Result<(), String> {
    write_atomic(path, text).map_err(|error| format!("{label} write failed: {error}"))
}

/// Persist selected stored documents and the core comparison record verbatim.
fn export_session_bundle(session: &Session, app: &mut App) {
    let bundle_path = match app.bundle_path() {
        Some(path) => path.to_path_buf(),
        None => {
            app.set_status("set a session bundle path with u");
            return;
        }
    };
    let mut paths: Vec<PathBuf> = Vec::new();
    for path in [app.baseline_path(), app.candidate_path(), app.report_path()]
        .into_iter()
        .flatten()
    {
        if !paths.iter().any(|existing| existing == path) {
            paths.push(path.to_path_buf());
        }
    }
    if paths.is_empty() {
        app.set_status("bundle export needs at least one stored result");
        return;
    }
    let mut results = Vec::new();
    let mut used_names: Vec<String> = vec!["config".to_string()];
    for path in paths {
        let text = path
            .to_str()
            .ok_or_else(|| "non-UTF-8 result path".to_string());
        let text =
            match text.and_then(|value| session.result_read(value).map_err(|e| e.to_string())) {
                Ok(text) => text,
                Err(error) => {
                    app.set_status(format!("bundle result read failed: {error}"));
                    return;
                }
            };
        let stem = path
            .file_stem()
            .map(|name| name.to_string_lossy().into_owned())
            .filter(|name| !name.is_empty())
            .unwrap_or_else(|| "result".to_string());
        let mut name = stem.clone();
        let mut suffix = 2_usize;
        while used_names.contains(&name) {
            name = format!("{stem}-{suffix}");
            suffix += 1;
        }
        used_names.push(name.clone());
        results.push((name, text));
    }
    let comparisons: Vec<String> = app
        .comparison()
        .map(|comparison| vec![comparison.record_json_text()])
        .unwrap_or_default();
    let config = match session.serialize_config() {
        Ok(text) => text,
        Err(error) => {
            app.set_status(format!("bundle config snapshot failed: {error}"));
            return;
        }
    };
    let configs = vec![("config".to_string(), config)];
    let bundle = match SessionBundle::from_texts(&results, &comparisons, &configs) {
        Ok(bundle) => bundle,
        Err(error) => {
            app.set_status(format!("bundle encode failed: {error}"));
            return;
        }
    };
    match bundle.export_to(&bundle_path) {
        Ok(()) => app.set_status(format!(
            "session bundle exported to {}",
            bundle_path.display()
        )),
        Err(error) => app.set_status(format!("session bundle export failed: {error}")),
    }
}

/// Reconstruct stored documents and comparison records from a session bundle.
fn import_session_bundle(app: &mut App) {
    let path = match app.bundle_path() {
        Some(path) => path.to_path_buf(),
        None => {
            app.set_bundle_status("set a session bundle path with u");
            app.set_status(app.bundle_status().to_string());
            return;
        }
    };
    let target = app.bundle_target_dir().to_path_buf();
    match SessionBundle::import_from(&path, &target) {
        Ok(bundle) => {
            app.set_bundle_status(format!(
                "imported {} results, {} comparisons, {} configs into {}",
                bundle.results().len(),
                bundle.comparisons().len(),
                bundle.configs().len(),
                target.display()
            ));
            app.set_status(app.bundle_status().to_string());
        }
        Err(error) => {
            app.set_bundle_status(format!("import failed: {error}"));
            app.set_status(app.bundle_status().to_string());
        }
    }
}

/// Unit tests for terminal event handling and refresh helpers.
#[cfg(test)]
mod tests {
    use std::time::Duration;

    use ratatui::crossterm::event::{KeyCode, KeyEvent, KeyModifiers};

    use super::{
        apply_assignment, apply_comparison_result, apply_dashboard_result, apply_inspection_result,
        apply_report_envelope, apply_result_result, consume_events, consume_run_result,
        execute_screen_action, export_session_bundle, handle_key, import_session_bundle,
        open_trust_prompt, refresh_dashboard, refresh_portability, refresh_profiles,
        refresh_screen, resolve_trust_prompt, save_current_profile, start_run, write_export,
    };
    use ztick_tui::app::{App, InputTarget, RunState, Screen};
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

    /// Build one key press carrying the CONTROL modifier.
    fn control_key(code: KeyCode) -> KeyEvent {
        KeyEvent::new(code, KeyModifiers::CONTROL)
    }

    /// Build one valid stored result document with one MET-01 sample.
    fn result_document(session_id: &str, value: f64) -> String {
        format!(
            r#"{{"schema_version":"1.0.0","sdk_version":"1.0.0","session":{{"id":"{session_id}"}},"config_snapshot":{DEFAULT_CONFIG_JSON},"build":{{}},"machine":{{}},"environment":{{}},"runs":[{{"id":"run-1","status":"complete","failures":[],"metrics":[{{"id":"MET-01","name":"Execution time","value":{value},"unit":"s","status":"measured","source":"instrumented_dsp","method":"nearest_rank","reason":"","context":{{}}}}]}}],"comparisons":[],"warnings":[],"failures":[],"status":"complete"}}"#
        )
    }

    /// Build one unique temporary directory for a P5.6 flow test.
    fn flow_dir(label: &str) -> std::path::PathBuf {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("ztick-p56-{label}-{}-{nonce}", std::process::id()))
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
        for (index, expected) in Screen::ALL.iter().take(10).enumerate() {
            let digit = if *expected == Screen::Results {
                '0'
            } else {
                char::from_digit((index + 1) as u32, 10).expect("index is a digit")
            };
            assert!(!handle_key(&session, &mut app, key(KeyCode::Char(digit))));
            assert_eq!(app.screen(), *expected);
        }
        assert!(handle_key(&session, &mut app, key(KeyCode::Char('q'))));
    }

    /// Verify letter keys select the P5.6 comparison and report screens.
    #[test]
    fn letter_keys_select_comparison_and_report_screens() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('c'))));
        assert_eq!(app.screen(), Screen::Compare);
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('e'))));
        assert_eq!(app.screen(), Screen::Report);
    }

    /// Verify an operation editor commits a comparison baseline path.
    #[test]
    fn operation_editor_commits_a_baseline_path() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_screen(Screen::Compare);
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('b'))));
        assert_eq!(
            app.input().expect("editor is open").target(),
            InputTarget::BaselinePath
        );
        for character in "baseline.json".chars() {
            app.push_input_char(character);
        }
        assert!(!handle_key(&session, &mut app, key(KeyCode::Enter)));
        assert_eq!(
            app.baseline_path().map(|path| path.display().to_string()),
            Some("baseline.json".to_string())
        );
        assert!(app.comparison().is_none());
        assert!(app.status().contains("compare needs a candidate"));
    }

    /// Verify missing P5.6 inputs are reported without inventing comparisons or reports.
    #[test]
    fn missing_comparison_and_report_inputs_are_reported() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_screen(Screen::Compare);
        refresh_screen(&session, &mut app);
        assert!(app.comparison().is_none());
        assert!(app.status().contains("compare needs a baseline"));
        app.set_screen(Screen::Report);
        refresh_screen(&session, &mut app);
        assert!(app.report().is_none());
        assert!(app.status().contains("report needs a result path"));
    }

    /// Verify key flows load a core comparison, CSV projection and report exports.
    #[test]
    fn comparison_and_report_key_flows_copy_core_output_to_export_files() {
        use std::fs;

        let session = Session::new().expect("session creation succeeds");
        let directory = flow_dir("key-flows");
        fs::create_dir_all(&directory).expect("flow directory is created");
        let baseline = directory.join("baseline.json");
        let candidate = directory.join("candidate.json");
        fs::write(&baseline, result_document("s-baseline", 0.5)).expect("baseline is written");
        fs::write(&candidate, result_document("s-candidate", 0.6)).expect("candidate is written");

        let mut app = configured_app();
        app.set_baseline_path(Some(baseline.clone()));
        app.set_candidate_path(Some(candidate));
        app.set_report_path(Some(baseline));
        let json_export = directory.join("exports/report.json");
        let csv_export = directory.join("exports/report.csv");
        app.set_json_export_path(Some(json_export.clone()));
        app.set_csv_export_path(Some(csv_export.clone()));

        app.set_screen(Screen::Compare);
        refresh_screen(&session, &mut app);
        assert!(app.comparison().is_some());
        assert!(app.status().contains("comparison loaded"));
        app.set_comparison(None);
        assert!(!handle_key(
            &session,
            &mut app,
            control_key(KeyCode::Char('e'))
        ));
        assert!(app.comparison().is_some());

        app.set_screen(Screen::Report);
        refresh_screen(&session, &mut app);
        assert!(app.report().is_some());
        assert!(!app.csv_export().is_empty());
        assert!(!handle_key(
            &session,
            &mut app,
            control_key(KeyCode::Char('e'))
        ));
        assert!(app.status().contains("report exported"));
        assert!(
            fs::read_to_string(&json_export)
                .expect("JSON export exists")
                .contains("\"command\":\"report\"")
        );
        assert!(
            fs::read_to_string(&csv_export)
                .expect("CSV export exists")
                .contains("session_id")
        );

        fs::remove_dir_all(&directory).expect("flow directory is removed");
    }

    /// Verify the comparison screen exposes an explicit regression-failure toggle.
    #[test]
    fn comparison_key_toggles_fail_on_regression() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_screen(Screen::Compare);
        assert!(!app.fail_on_regression());
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('f'))));
        assert!(app.fail_on_regression());
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('f'))));
        assert!(!app.fail_on_regression());
    }

    /// Verify control shortcuts never act while a text editor is open.
    #[test]
    fn control_shortcuts_are_inert_while_editing() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_screen(Screen::Report);
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('f'))));
        for character in "keep.json".chars() {
            app.push_input_char(character);
        }
        for code in [
            KeyCode::Char('c'),
            KeyCode::Char('e'),
            KeyCode::Char('b'),
            KeyCode::Char('i'),
            KeyCode::Char('r'),
            KeyCode::Char('s'),
            KeyCode::Char('o'),
        ] {
            assert!(!handle_key(&session, &mut app, control_key(code)));
            assert_eq!(
                app.input().expect("editor remains open").buffer(),
                "keep.json"
            );
        }
        assert!(!handle_key(&session, &mut app, key(KeyCode::Esc)));
        assert!(app.input().is_none());
    }

    /// Verify failed envelopes keep their explicit CLI error in the status line.
    #[test]
    fn failed_envelopes_keep_their_cli_error() {
        let compare = r#"{"schema_version":1,"command":"compare","status":"failure","exit_code":1,"data":null,"error":"cannot read baseline result"}"#;
        let mut app = App::new();
        apply_comparison_result(Ok(compare.to_string()), &mut app);
        assert!(app.comparison().is_none());
        assert!(app.status().contains("cannot read baseline result"));

        let report = r#"{"schema_version":1,"command":"report","status":"failure","exit_code":1,"data":null,"error":"cannot read report result"}"#;
        let mut app = App::new();
        assert!(!apply_report_envelope(report, &mut app));
        assert!(app.report().is_none());
        assert!(app.status().contains("cannot read report result"));
    }

    /// Verify bundle export keeps duplicate and config stems distinct on import.
    #[test]
    fn bundle_round_trip_uses_unique_stems_and_restores_config() {
        use std::fs;

        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("the default config parses");
        let directory = flow_dir("bundle");
        let first = directory.join("one");
        let second = directory.join("two");
        let third = directory.join("three");
        let restored = directory.join("restored");
        fs::create_dir_all(&first).expect("first source directory is created");
        fs::create_dir_all(&second).expect("second source directory is created");
        fs::create_dir_all(&third).expect("third source directory is created");
        let first_path = first.join("result.json");
        let second_path = second.join("result.json");
        let third_path = third.join("config.json");
        fs::write(&first_path, result_document("s-first", 0.5)).expect("first result is written");
        fs::write(&second_path, result_document("s-second", 0.7))
            .expect("second result is written");
        fs::write(&third_path, result_document("s-third", 0.9)).expect("third result is written");
        let bundle_path = directory.join("session.bundle.json");

        let mut app = configured_app();
        app.set_baseline_path(Some(first_path));
        app.set_candidate_path(Some(second_path));
        app.set_report_path(Some(third_path));
        app.set_bundle_path(Some(bundle_path.clone()));
        app.set_bundle_target_dir(restored.clone());
        export_session_bundle(&session, &mut app);
        assert!(app.status().contains("session bundle exported"));

        import_session_bundle(&mut app);
        assert!(app.status().contains("imported 3 results"));
        let first_text =
            fs::read_to_string(restored.join("result.json")).expect("first is restored");
        let second_text =
            fs::read_to_string(restored.join("result-2.json")).expect("second is restored");
        let third_text =
            fs::read_to_string(restored.join("config-2.json")).expect("third is restored");
        assert!(first_text.contains("s-first"));
        assert!(second_text.contains("s-second"));
        assert!(third_text.contains("s-third"));
        assert!(
            fs::read_to_string(restored.join("config.json"))
                .expect("config is restored")
                .contains("config_version")
        );

        fs::remove_dir_all(&directory).expect("bundle directory is removed");
    }

    /// Verify copied export text is written through its configured path.
    #[test]
    fn write_export_creates_a_parent_directory() {
        let root =
            std::env::temp_dir().join(format!("ztick-export-{}-{}", std::process::id(), line!()));
        let target = root.join("nested").join("report.json");
        write_export(&target, "{\"status\":\"success\"}", "report JSON")
            .expect("export text is written");
        assert_eq!(
            std::fs::read_to_string(&target).expect("export file is readable"),
            "{\"status\":\"success\"}"
        );
        std::fs::remove_dir_all(&root).expect("temporary export directory is removed");
    }

    /// Verify bundle actions report missing paths instead of failing silently.
    #[test]
    fn bundle_actions_require_a_configured_path() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        export_session_bundle(&session, &mut app);
        assert!(app.status().contains("session bundle path"));
        import_session_bundle(&mut app);
        assert!(app.bundle_status().contains("set a session bundle path"));
        execute_screen_action(&session, &mut app);
        assert!(app.status().contains("Ctrl-E is available"));
    }

    /// Verify the zero key opens results inspection and loads retained result data.
    #[test]
    fn zero_key_opens_results_and_loads_stored_document() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-results-key-{nonce}"));
        std::fs::create_dir_all(&directory).expect("temporary profile directory is created");
        let config_path = profile::save(&directory, "Quick", DEFAULT_CONFIG_JSON)
            .expect("the run config is saved");
        let config_text = config_path.to_string_lossy().to_string();
        session
            .start_run(&config_text)
            .expect("the async run starts");

        let mut envelope = None;
        for _ in 0..500 {
            while session
                .poll_event()
                .expect("progress poll succeeds")
                .is_some()
            {}
            if let Some(json) = session.run_result().expect("result copy succeeds") {
                envelope = Some(json);
                break;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        let envelope = envelope.expect("the async run published a result envelope");
        assert!(envelope.contains("\"status\":\"success\""));

        let mut app = configured_app();
        app.set_run_result(&envelope);
        assert!(!handle_key(&session, &mut app, key(KeyCode::Char('0'))));
        assert_eq!(app.screen(), Screen::Results);
        let inspection = app
            .result_inspection()
            .expect("the stored result was decoded");
        assert!(inspection.is_complete());
        assert!(!inspection.runs().is_empty());
        assert!(app.status().is_empty() || !app.status().contains("failed"));

        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }

    /// Verify an unreadable result path is reported instead of appearing complete.
    #[test]
    fn unreadable_result_document_is_reported() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_run_result(
            r#"{"status":"success","data":{"output_path":"/missing/ztick-result.json"}}"#,
        );
        app.set_screen(Screen::Results);
        refresh_screen(&session, &mut app);
        assert!(app.result_inspection().is_none());
        assert!(app.status().contains("result document read failed"));
        apply_result_result(Err(FrontendError::new(7, "decode rejected")), &mut app);
        assert!(app.status().contains("result document read failed"));
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

    /// Verify trust confirmation refuses execution until an explicit grant is recorded.
    #[test]
    fn trust_confirmation_refuses_execution_until_explicit_grant() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-trust-ui-{nonce}"));
        let trust_path = directory.join("trusted.json");
        std::fs::create_dir_all(&directory).expect("temporary trust directory is created");
        let trust_path_text = trust_path.to_string_lossy().to_string();

        let session = Session::with_trust_store_path(Some(&trust_path_text))
            .expect("session creation with trust store succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");

        let mut app = configured_app();
        let plugins = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[],"trust":{"status":"available","reason":"","entries":[]},"plugins":[{"path":"/app.so","source":"explicit_path","status":"success","reason":"","identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7f","trust":"untrusted"}]},"error":""}"#;
        let system = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{"cpu_model":{"status":"value","value":"Test CPU","reason":""}}},"error":""}"#;
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[]},"error":""}"#;
        app.set_dashboard_data(DashboardData::from_json(system, list, plugins));
        let inspection = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"","metadata":{"identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7f","metadata":"demo"},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes");
        app.set_inspection(Some(inspection));
        refresh_portability(&mut app);

        assert!(!app.portability().is_allowed());
        assert!(app.portability().blockers().iter().any(|finding| {
            finding
                .message()
                .contains("explicit first-run consent is required")
        }));

        start_run(&session, &mut app);
        let status = app.status().to_ascii_lowercase();
        assert!(
            status.contains("run blocked"),
            "status was {}",
            app.status()
        );
        assert!(
            session
                .run_result()
                .expect("run result query succeeds")
                .is_none()
        );

        app.set_screen(Screen::Integrations);
        open_trust_prompt(&session, &mut app);
        assert!(app.trust_prompt().is_some());
        let prompt = app.trust_prompt().expect("prompt open");
        assert_eq!(prompt.identifier(), "app-a");
        assert_eq!(prompt.capabilities(), "0x7f");
        assert_eq!(prompt.trust_state(), "untrusted");

        resolve_trust_prompt(&session, &mut app, false);
        assert!(!trust_path.exists(), "denial must not create trust store");
        assert!(app.status().contains("execution remains refused"));
        app.set_dashboard_data(DashboardData::from_json(system, list, plugins));
        app.set_inspection(Some(PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"","metadata":{"identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7f","metadata":"demo"},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        ).expect("inspection decodes")));
        refresh_portability(&mut app);
        assert!(!app.portability().is_allowed());

        open_trust_prompt(&session, &mut app);
        resolve_trust_prompt(&session, &mut app, true);
        assert!(
            trust_path.exists(),
            "grant must persist trust; status={}; trust_query={:?}",
            app.status(),
            session.trust_query("app-a")
        );
        assert!(
            session.trust_query("app-a").expect("trust query succeeds"),
            "trusted plugin should be reported as trusted"
        );
        let trusted_inspection = PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"","metadata":{"identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7f","metadata":"demo"},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[{"identifier":"app-a","name":"app-a","version":"","sdk_version":"","capabilities":""}],"state":"trusted"}},"error":""}"#,
        )
        .expect("trusted inspection decodes");
        app.set_inspection(Some(trusted_inspection));
        refresh_portability(&mut app);
        assert!(
            !app.portability()
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("untrusted")),
            "grant should clear untrusted blocker: {:?}",
            app.portability().blockers()
        );

        std::fs::remove_dir_all(&directory).expect("temporary trust directory is removed");
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

    /// P5.8 principal workflow: dashboard orientation shows system, integrations and warnings.
    #[test]
    fn principal_workflow_orientation() {
        let session = Session::new().expect("session creation succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        refresh_dashboard(&session, &mut app);
        consume_events(&session, &mut app);
        assert_eq!(app.screen(), Screen::Dashboard);
        assert!(!app.data().system().fields().is_empty());
        assert!(!app.data().plugins().directories().is_empty());
        assert_eq!(app.state(), RunState::Configuring);
    }

    /// P5.8 principal workflow: configuration edits are validated through the boundary.
    #[test]
    fn principal_workflow_configuration() {
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
        apply_assignment(&session, &mut app, "dimensions.channels=0");
        assert_eq!(app.config().expect("config remains").channels(), Some(2));
        assert!(app.status().contains("override rejected"));
    }

    /// P5.8 principal workflow: execution monitoring reports progress and completes.
    #[test]
    fn principal_workflow_execution_monitoring() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-p58-monitor-{nonce}"));
        app.set_profile_directory(&directory);
        app.set_portability(PortabilityReport::default());
        start_run(&session, &mut app);
        assert_eq!(app.screen(), Screen::Execution);
        for _ in 0..500 {
            consume_events(&session, &mut app);
            consume_run_result(&session, &mut app);
            if !app.progress().result_status.is_empty() {
                break;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        assert!(app.progress().result_status.contains("success"));
        assert_eq!(app.state(), RunState::Complete);
        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }

    /// P5.8 principal workflow: results inspection shows stored statuses and warnings.
    #[test]
    fn principal_workflow_results_inspection() {
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_run_result(r#"{"status":"success","data":{"output_path":"/missing/result.json"}}"#);
        app.set_screen(Screen::Results);
        refresh_screen(&session, &mut app);
        assert!(app.result_inspection().is_none());
        assert!(app.status().contains("result document read failed"));

        let inspection = ztick_tui::data::ResultInspection::from_json(
            r#"{"schema_version":"1.0.0","sdk_version":"1.0.0","session":{"id":"s"},"config_snapshot":{"config_version":"1.0.0","sdk_version":"1.0.0","integration":{"id":"app-a","version":"1"},"workload":{"level":"processor"},"dimensions":{},"execution":{},"profile":{},"measurement":{},"validation":{},"parallelism":{}},"build":{},"machine":{},"environment":{},"runs":[{"id":"r1","status":"incomplete","failures":[{"kind":"validation","message":"bad"}],"metrics":[{"id":"MET-01","name":"Execution time","value":1.0,"unit":"s","status":"measured","source":"instrumented_dsp","method":"","reason":"","context":{}}],"validation":{"level":"Basic","passed":false,"checks":[]}}],"comparisons":[],"warnings":[{"kind":"w","message":"warning"}],"failures":[],"status":"incomplete"}"#,
        )
        .expect("result decodes");
        app.set_result_inspection(Some(inspection));
        assert_eq!(app.screen(), Screen::Results);
        let displayed = app.result_inspection().expect("inspection present");
        assert!(!displayed.is_complete());
        assert_eq!(displayed.runs().len(), 1);
    }

    /// P5.8 principal workflow: comparison/report screens show stored records and exports.
    #[test]
    fn principal_workflow_comparison_and_report() {
        let directory = flow_dir("workflow");
        std::fs::create_dir_all(&directory).expect("temporary directory is created");
        let baseline = directory.join("baseline.json");
        let candidate = directory.join("candidate.json");
        std::fs::write(&baseline, result_document("baseline", 1.0)).expect("baseline written");
        std::fs::write(&candidate, result_document("candidate", 1.2)).expect("candidate written");
        let session = Session::new().expect("session creation succeeds");
        let mut app = configured_app();
        app.set_baseline_path(Some(baseline.clone()));
        app.set_candidate_path(Some(candidate.clone()));
        app.set_screen(Screen::Compare);
        execute_screen_action(&session, &mut app);
        assert!(app.comparison().is_some(), "status: {}", app.status());

        app.set_report_path(Some(candidate.clone()));
        app.set_screen(Screen::Report);
        refresh_screen(&session, &mut app);
        assert!(app.report().is_some(), "status: {}", app.status());
        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }

    /// P5.8 principal workflow: trust consent blocks execution until explicit grant.
    #[test]
    fn principal_workflow_trust_consent() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("ztick-p58-trust-{nonce}"));
        let trust_path = directory.join("trusted.json");
        std::fs::create_dir_all(&directory).expect("temporary trust directory is created");
        let trust_path_text = trust_path.to_string_lossy().to_string();
        let session = Session::with_trust_store_path(Some(&trust_path_text))
            .expect("session creation with trust store succeeds");
        session
            .parse_config(DEFAULT_CONFIG_JSON)
            .expect("default config parses");
        let mut app = configured_app();
        let plugins = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[],"trust":{"status":"available","reason":"","entries":[]},"plugins":[{"path":"/app.so","source":"explicit_path","status":"success","reason":"","identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7f","trust":"untrusted"}]},"error":""}"#;
        let system = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{"cpu_model":{"status":"value","value":"Test CPU","reason":""}}},"error":""}"#;
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[]},"error":""}"#;
        app.set_dashboard_data(DashboardData::from_json(system, list, plugins));
        app.set_inspection(Some(PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"","metadata":{"identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7f","metadata":"demo"},"compatibility":{"status":"success","reason":""},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        ).expect("inspection decodes")));
        refresh_portability(&mut app);
        start_run(&session, &mut app);
        assert!(app.status().to_ascii_lowercase().contains("run blocked"));
        app.set_screen(Screen::Integrations);
        open_trust_prompt(&session, &mut app);
        resolve_trust_prompt(&session, &mut app, true);
        assert!(session.trust_query("app-a").expect("trust query succeeds"));
        std::fs::remove_dir_all(&directory).expect("temporary trust directory is removed");
    }
}
