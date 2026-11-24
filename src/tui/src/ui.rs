// SPDX-License-Identifier: MIT
//! Stateless ratatui rendering for the dashboard and detail screens.
//!
//! The renderer reads [`crate::app::App`] only. It formats copied front-end data and never
//! computes metrics, changes configuration or loads plugin code.

use ratatui::{
    Frame,
    layout::{Constraint, Layout},
    text::Line,
    widgets::{Block, Paragraph},
};

use crate::app::{App, Screen};
use crate::data::{AvailabilityField, DashboardData, PluginInspection};

/// Build the header lines shared by every screen.
fn header_lines(app: &App) -> Vec<Line<'static>> {
    vec![Line::from(format!(
        "{} | run state: {}",
        app.screen().title(),
        app.state().label()
    ))]
}

/// Build the key hint footer shared by every screen.
fn footer_lines() -> Vec<Line<'static>> {
    vec![Line::from(
        "1 dashboard  2 system  3 integrations  Tab cycle  Up/Down select  r refresh  q quit",
    )]
}

/// Build the orientation summary for the dashboard screen.
fn dashboard_lines(app: &App) -> Vec<Line<'static>> {
    let data = app.data();
    let warnings = app.warnings();
    let mut lines = vec![
        Line::from(format!("Run state: {}", app.state().label())),
        Line::from(format!("Status: {}", app.status())),
        Line::from(format!(
            "System: {}",
            system_summary(data).unwrap_or_else(|| "(not loaded)".to_string())
        )),
        Line::from(format!("Profiles: {}", data.profiles().profiles().len())),
        Line::from(format!(
            "Integrations: {} discovered",
            data.plugins().plugins().len()
        )),
        Line::from(format!(
            "Plugin directories: {}",
            data.plugins().directories().len()
        )),
        Line::from("Recent runs: unavailable (no front-end listing operation)"),
        Line::from("Reliability: Convenience default; instability warnings are recorded"),
        Line::from(format!("Warnings: {}", warnings.len())),
    ];
    if warnings.is_empty() {
        lines.push(Line::from("  (none)"));
    } else {
        lines.extend(
            warnings
                .iter()
                .map(|warning| Line::from(format!("  - {warning}"))),
        );
    }
    if data.load_error().is_empty() {
        lines.push(Line::from("Load state: all envelope sections decoded"));
    } else {
        lines.push(Line::from(format!("Load errors: {}", data.load_error())));
    }
    lines
}

/// Build a compact system summary from named inventory fields.
fn system_summary(data: &DashboardData) -> Option<String> {
    let names = [
        "cpu_model",
        "logical_core_count",
        "total_ram_bytes",
        "os_name",
    ];
    let parts: Vec<String> = names
        .iter()
        .filter_map(|name| {
            let field = data.system().field(name)?;
            Some(format!("{}={}", name, field.display_value()))
        })
        .collect();
    if parts.is_empty() {
        None
    } else {
        Some(parts.join("; "))
    }
}

/// Build the full system inventory view with every availability state visible.
fn system_lines(data: &DashboardData) -> Vec<Line<'static>> {
    if data.system().fields().is_empty() {
        return vec![Line::from("(no inventory fields reported)")];
    }
    let mut lines = vec![Line::from(
        "name                         status          value or reason",
    )];
    lines.extend(data.system().fields().iter().map(field_line));
    if !data.load_error().is_empty() {
        lines.push(Line::from(format!("Load errors: {}", data.load_error())));
    }
    lines
}

/// Format one availability-aware field without hiding a non-value state.
fn field_line(field: &AvailabilityField) -> Line<'static> {
    Line::from(format!(
        "{:<28} {:<15} {}",
        field.name(),
        field.status().label(),
        field.display_value()
    ))
}

/// Build the integrations view, including directories, candidates and detail.
fn integration_lines(app: &App) -> Vec<Line<'static>> {
    let data = app.data();
    let plugins = data.plugins();
    let trust = plugins.trust();
    let mut lines = vec![
        Line::from(format!(
            "Trust list: {} ({})",
            trust.status(),
            value_or_placeholder(trust.reason(), "no reason recorded")
        )),
        Line::from(format!("Trusted identities: {}", trust.entries().len())),
        Line::from(format!("Directories: {}", plugins.directories().len())),
    ];
    for entry in trust.entries() {
        lines.push(Line::from(format!(
            "  trusted: {} {} ({})",
            entry.identifier(),
            entry.version(),
            entry.sdk_version()
        )));
    }
    for directory in plugins.directories() {
        lines.push(Line::from(format!(
            "  {}: {} ({})",
            directory.path(),
            directory.status(),
            value_or_placeholder(directory.reason(), "no reason recorded")
        )));
    }
    lines.push(Line::from(format!("Plugins: {}", plugins.plugins().len())));

    for (index, plugin) in plugins.plugins().iter().enumerate() {
        let marker = if index == app.selected_plugin_index() {
            ">"
        } else {
            " "
        };
        lines.push(Line::from(format!(
            "{marker} [{}] {} source={} status={}",
            index,
            plugin.path(),
            plugin.source(),
            plugin.status()
        )));
        if plugin.has_metadata() {
            lines.push(Line::from(format!(
                "    identifier={} name={}",
                plugin.identifier(),
                plugin.name()
            )));
            lines.push(Line::from(format!(
                "    version={} sdk={} capabilities={} trust={}",
                plugin.version(),
                plugin.sdk_version(),
                plugin.capabilities(),
                trust.state_for(plugin.identifier())
            )));
        } else {
            lines.push(Line::from(format!(
                "    reason={}",
                value_or_placeholder(plugin.reason(), "no reason recorded")
            )));
            lines.push(Line::from(format!("    trust={}", plugin.trust())));
        }
    }

    lines.extend(inspection_lines(app));
    if !data.load_error().is_empty() {
        lines.push(Line::from(format!("Load errors: {}", data.load_error())));
    }
    lines
}

/// Build the selected plugin's metadata-only inspection detail.
fn inspection_lines(app: &App) -> Vec<Line<'static>> {
    let Some(plugin) = app.selected_plugin() else {
        return vec![Line::from("Selected plugin: (none)")];
    };
    let Some(inspection) = matching_inspection(app.inspection(), plugin.path()) else {
        return vec![
            Line::from(format!("Detail: {}", plugin.path())),
            Line::from("Inspection: not loaded"),
            Line::from("Compatibility: not inspected"),
            Line::from("Execution mode preference: not advertised by discovery"),
        ];
    };
    vec![
        Line::from(format!("Detail: {}", inspection.path())),
        Line::from(format!(
            "Inspection: {} ({})",
            inspection.status(),
            value_or_placeholder(inspection.reason(), "no reason recorded")
        )),
        Line::from(format!(
            "Identity: {} {} ({})",
            inspection.name(),
            inspection.version(),
            inspection.identifier()
        )),
        Line::from(format!(
            "SDK: {} capabilities={} trust={}",
            inspection.sdk_version(),
            inspection.capabilities(),
            inspection.trust().state()
        )),
        Line::from(format!(
            "Compatibility: {} ({})",
            inspection.compatibility().status(),
            value_or_placeholder(inspection.compatibility().reason(), "no reason recorded")
        )),
        Line::from(format!(
            "Plugin metadata: {}",
            value_or_placeholder(inspection.metadata(), "no metadata summary")
        )),
        Line::from("Execution mode preference: not advertised by discovery"),
        Line::from("Execution mode follows the config request, not trust alone"),
        Line::from("Untrusted plugins are refused until consent is recorded"),
        Line::from("Process isolation is not a security sandbox"),
    ]
}

/// Return the inspection only when it still describes the selected path.
fn matching_inspection<'a>(
    inspection: Option<&'a PluginInspection>,
    path: &str,
) -> Option<&'a PluginInspection> {
    inspection.filter(|value| value.path() == path)
}

/// Return the text or a visible fallback instead of silently hiding empty data.
fn value_or_placeholder(value: &str, placeholder: &str) -> String {
    if value.is_empty() {
        placeholder.to_string()
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
        Paragraph::new(header_lines(app)).block(Block::bordered().title("ztick-bench")),
        header_area,
    );

    let body = match app.screen() {
        Screen::Dashboard => dashboard_lines(app),
        Screen::System => system_lines(app.data()),
        Screen::Integrations => integration_lines(app),
    };
    frame.render_widget(
        Paragraph::new(body).block(Block::bordered().title(app.screen().title())),
        main_area,
    );

    frame.render_widget(
        Paragraph::new(footer_lines()).block(Block::bordered().title("Keys")),
        keys_area,
    );
}

/// Unit tests for headless screen rendering.
#[cfg(test)]
mod tests {
    use ratatui::{Terminal, backend::TestBackend};

    use super::{
        dashboard_lines, field_line, integration_lines, matching_inspection, render, system_lines,
        system_summary, value_or_placeholder,
    };
    use crate::app::{App, Screen};
    use crate::data::{DashboardData, PluginInspection};

    /// Dashboard data containing all four availability states and one plugin.
    fn test_data() -> DashboardData {
        let system = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{"cpu_model":{"status":"value","value":"Test CPU","reason":""},"audio_backend":{"status":"not_applicable","value":"","reason":"no selected mode"},"cpu_frequency_hz":{"status":"unavailable","value":"","reason":"sysfs hidden"},"future_field":{"status":"unsupported","value":"","reason":"no backend"},"os_name":{"status":"value","value":"TestOS","reason":""}}},"error":""}"#;
        let list = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[{"name":"Quick","description":"fast"}]},"error":""}"#;
        let plugins = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[{"path":"/plugins","status":"unavailable","reason":"missing"}],"trust":{"status":"available","reason":"","entries":[]},"plugins":[{"path":"/app.so","source":"explicit_path","status":"success","reason":"","identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7c","trust":"untrusted"}]},"error":""}"#;
        DashboardData::from_json(system, list, plugins)
    }

    /// Inspection describing the plugin in [`test_data`].
    fn test_inspection() -> PluginInspection {
        PluginInspection::from_json(
            r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/app.so","status":"success","reason":"no reason recorded","metadata":{"identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7c","metadata":"test adapter"},"compatibility":{"status":"success","reason":"no reason recorded"},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#,
        )
        .expect("inspection decodes")
    }

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

    /// Join line helpers into one searchable string.
    fn lines_text(lines: &[ratatui::text::Line<'static>]) -> String {
        lines
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("\n")
    }

    /// Verify value or placeholder makes empty data visible.
    #[test]
    fn value_or_placeholder_makes_empty_data_visible() {
        assert_eq!(value_or_placeholder("", "(none)"), "(none)");
        assert_eq!(value_or_placeholder("value", "(none)"), "value");
    }

    /// Verify system summary uses named fields and handles missing data.
    #[test]
    fn system_summary_uses_named_fields_and_handles_missing_data() {
        let data = test_data();
        let summary = system_summary(&data).expect("named fields exist");
        assert!(summary.contains("cpu_model=Test CPU"));
        assert!(summary.contains("os_name=TestOS"));
        assert_eq!(system_summary(&DashboardData::default()), None);
    }

    /// Verify dashboard lines show state counts warnings and unavailable notes.
    #[test]
    fn dashboard_lines_show_state_counts_warnings_and_unavailable_notes() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        app.set_status("test point 2");
        app.push_warning("timer drift");
        let text = lines_text(&dashboard_lines(&app));
        assert!(text.contains("Run state: Idle"));
        assert!(text.contains("Status: test point 2"));
        assert!(text.contains("Profiles: 1"));
        assert!(text.contains("Integrations: 1 discovered"));
        assert!(text.contains("Recent runs: unavailable"));
        assert!(text.contains("Reliability: Convenience"));
        assert!(text.contains("Warnings: 1"));
        assert!(text.contains("timer drift"));
    }

    /// Verify system lines show every field and non value states.
    #[test]
    fn system_lines_show_every_field_and_non_value_states() {
        let text = lines_text(&system_lines(&test_data()));
        assert!(text.contains("cpu_model"));
        assert!(text.contains("audio_backend"));
        assert!(text.contains("cpu_frequency_hz"));
        assert!(text.contains("future_field"));
        assert!(text.contains("not_applicable"));
        assert!(text.contains("unavailable"));
        assert!(text.contains("unsupported"));
        assert!(text.contains("no selected mode"));
        assert!(text.contains("sysfs hidden"));
        assert!(text.contains("no backend"));
    }

    /// Verify system lines report an empty inventory explicitly.
    #[test]
    fn system_lines_report_an_empty_inventory_explicitly() {
        let text = lines_text(&system_lines(&DashboardData::default()));
        assert_eq!(text, "(no inventory fields reported)");
    }

    /// Verify field line preserves status and reason.
    #[test]
    fn field_line_preserves_status_and_reason() {
        let data = test_data();
        let field = data.system().field("future_field").expect("field exists");
        let text = field_line(field).to_string();
        assert!(text.contains("future_field"));
        assert!(text.contains("unsupported"));
        assert!(text.contains("no backend"));
    }

    /// Verify integration lines show identity versions capabilities and trust.
    #[test]
    fn integration_lines_show_identity_versions_capabilities_and_trust() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        let text = lines_text(&integration_lines(&app));
        assert!(text.contains("Trust list: available"));
        assert!(text.contains("/plugins: unavailable (missing)"));
        assert!(text.contains("identifier=app-a name=App A"));
        assert!(text.contains("version=1.2.3 sdk=1.0.0 capabilities=0x7c trust=untrusted"));
        assert!(text.contains("Compatibility: not inspected"));
        assert!(text.contains("Inspection: not loaded"));
        assert!(text.contains("not advertised by discovery"));
    }

    /// Verify integration lines show core compatibility when loaded.
    #[test]
    fn integration_lines_show_core_compatibility_when_loaded() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        app.set_inspection(Some(test_inspection()));
        let text = lines_text(&integration_lines(&app));
        assert!(text.contains("Inspection: success (no reason recorded)"));
        assert!(text.contains("Compatibility: success"));
        assert!(text.contains("Execution mode follows the config request"));
        assert!(text.contains("Untrusted plugins are refused until consent is recorded"));
        assert!(text.contains("Process isolation is not a security sandbox"));
        assert!(!text.contains("Trusted plugins run in-process"));
    }

    /// Verify failed inspection state and reason remain visible.
    #[test]
    fn integration_lines_show_failed_inspection_state() {
        let failed = r#"{"schema_version":1,"command":"plugin inspect","status":"failure","exit_code":1,"data":{"path":"/app.so","status":"failure","reason":"descriptor rejected","metadata":{},"compatibility":{"status":"unsupported","reason":"sdk mismatch"},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":"inspection rejected"}"#;
        let inspection = PluginInspection::from_json(failed).expect("failed data decodes");
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        app.set_inspection(Some(inspection));
        let text = lines_text(&integration_lines(&app));
        assert!(text.contains("Inspection: failure (descriptor rejected)"));
        assert!(text.contains("Compatibility: unsupported (sdk mismatch)"));
        assert!(text.contains("trust=untrusted"));
    }

    /// Verify matching inspection refuses a stale path.
    #[test]
    fn matching_inspection_refuses_a_stale_path() {
        let inspection = test_inspection();
        assert!(matching_inspection(Some(&inspection), "/app.so").is_some());
        assert!(matching_inspection(Some(&inspection), "/other.so").is_none());
        assert!(matching_inspection(None, "/app.so").is_none());
    }

    /// Verify render draws all three screens.
    #[test]
    fn render_draws_all_three_screens() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        for screen in [Screen::Dashboard, Screen::System, Screen::Integrations] {
            app.set_screen(screen);
            let mut terminal =
                Terminal::new(TestBackend::new(120, 30)).expect("test backend is valid");
            terminal
                .draw(|frame| render(frame, &app))
                .expect("drawing succeeds");
            let text = buffer_text(&terminal);
            assert!(
                text.contains("ztick-bench"),
                "screen {screen:?} has a header"
            );
            assert!(
                text.contains(screen.title()),
                "screen {screen:?} shows its title"
            );
            assert!(
                text.contains("1 dashboard"),
                "screen {screen:?} shows key hints"
            );
        }
    }
}
