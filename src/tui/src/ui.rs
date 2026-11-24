// SPDX-License-Identifier: MIT
//! Stateless ratatui rendering for the application shell.
//!
//! The renderer reads [`App`] only. Detail widgets for the later Phase 5
//! screens will be added here without moving state or core access into the TUI.

use ratatui::{
    Frame,
    layout::{Constraint, Layout},
    text::Line,
    widgets::{Block, Paragraph},
};

use crate::app::{App, RunState};

/// Build the dashboard's left-hand information lines.
fn dashboard_lines(app: &App) -> Vec<Line<'static>> {
    let snapshot = app.snapshot();
    vec![
        Line::from(format!("State: {}", app.state().label())),
        Line::from(format!("Status: {}", app.status())),
        Line::from(format!(
            "System: {}",
            value_or_placeholder(snapshot.system())
        )),
        Line::from(format!(
            "Integrations: {}",
            value_or_placeholder(snapshot.integrations())
        )),
        Line::from(format!(
            "Plugins: {}",
            value_or_placeholder(snapshot.plugins())
        )),
    ]
}

/// Build the run-state panel's detail lines.
fn state_lines(app: &App) -> Vec<Line<'static>> {
    let warnings = app.warnings();
    let mut lines = vec![
        Line::from(format!("Run state: {}", app.state().label())),
        Line::from(format!("Warnings: {}", warnings.len())),
    ];
    lines.extend(
        warnings
            .iter()
            .map(|warning| Line::from(format!("- {warning}"))),
    );
    if app.state() == RunState::Warning || app.state() == RunState::Failed {
        lines.push(Line::from("Completed measurements are preserved."));
    }
    lines
}

/// Return a visible placeholder instead of silently hiding empty dashboard data.
fn value_or_placeholder(value: &str) -> String {
    if value.is_empty() {
        "(not loaded)".to_string()
    } else {
        value.to_string()
    }
}

/// Render one complete application frame.
pub fn render(frame: &mut Frame<'_>, app: &App) {
    let [header_area, main_area, keys_area] = Layout::vertical([
        Constraint::Length(3),
        Constraint::Min(0),
        Constraint::Length(3),
    ])
    .areas(frame.area());
    frame.render_widget(
        Paragraph::new("Benchmark control and observation")
            .block(Block::bordered().title("ztick-bench")),
        header_area,
    );

    let [dashboard_area, state_area] =
        Layout::horizontal([Constraint::Percentage(60), Constraint::Percentage(40)])
            .areas(main_area);
    frame.render_widget(
        Paragraph::new(dashboard_lines(app)).block(Block::bordered().title("Dashboard")),
        dashboard_area,
    );
    frame.render_widget(
        Paragraph::new(state_lines(app)).block(Block::bordered().title("Run state")),
        state_area,
    );

    frame.render_widget(
        Paragraph::new("q / Esc / Ctrl-C: quit").block(Block::bordered().title("Keys")),
        keys_area,
    );
}

#[cfg(test)]
mod tests {
    use ratatui::{Terminal, backend::TestBackend};

    use super::{dashboard_lines, render, state_lines, value_or_placeholder};
    use crate::app::App;
    use crate::frontend::Snapshot;

    /// Flatten one rendered buffer into trimmed rows for content assertions.
    fn buffer_text(terminal: &Terminal<TestBackend>) -> String {
        let buffer = terminal.backend().buffer();
        (0..buffer.area.height)
            .map(|y| {
                (0..buffer.area.width)
                    .map(|x| buffer[(x, y)].symbol())
                    .collect::<String>()
                    .trim_end()
                    .to_string()
            })
            .collect::<Vec<_>>()
            .join("\n")
    }

    #[test]
    fn value_or_placeholder_makes_empty_data_visible() {
        assert_eq!(value_or_placeholder(""), "(not loaded)");
        assert_eq!(value_or_placeholder("value"), "value");
    }

    #[test]
    fn dashboard_lines_show_state_status_and_all_copied_envelopes() {
        let mut app = App::new();
        app.set_state_code(crate::ZTICK_FRONTEND_STATE_RUNNING);
        app.set_status("test point 3");
        app.set_snapshot(Snapshot::new("sys".into(), "list".into(), "plugins".into()));
        let text = dashboard_lines(&app)
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("\n");
        assert!(text.contains("State: Running"));
        assert!(text.contains("Status: test point 3"));
        assert!(text.contains("System: sys"));
        assert!(text.contains("Integrations: list"));
        assert!(text.contains("Plugins: plugins"));
    }

    #[test]
    fn dashboard_lines_show_placeholders_for_missing_data() {
        let text = dashboard_lines(&App::new())
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("\n");
        assert_eq!(text.matches("(not loaded)").count(), 3);
    }

    #[test]
    fn state_lines_show_warnings_and_preservation_notice() {
        let mut app = App::new();
        app.set_state_code(crate::ZTICK_FRONTEND_STATE_FAILED);
        app.push_warning("timer drift");
        let text = state_lines(&app)
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("\n");
        assert!(text.contains("Run state: Failed"));
        assert!(text.contains("Warnings: 1"));
        assert!(text.contains("- timer drift"));
        assert!(text.contains("Completed measurements are preserved."));
    }

    #[test]
    fn render_draws_the_application_shell() {
        let app = App::new();
        let mut terminal = Terminal::new(TestBackend::new(100, 30)).expect("test backend is valid");
        terminal
            .draw(|frame| render(frame, &app))
            .expect("drawing succeeds");
        let text = buffer_text(&terminal);
        assert!(text.contains("ztick-bench"));
        assert!(text.contains("Dashboard"));
        assert!(text.contains("Run state"));
        assert!(text.contains("State: Idle"));
        assert!(text.contains("q / Esc / Ctrl-C: quit"));
    }
}
