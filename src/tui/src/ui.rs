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
use crate::data::{
    AvailabilityField, DashboardData, PluginInspection, ResultInspection, ResultIssue,
    RunInspection,
};

/// Build the header lines shared by every screen.
fn header_lines(app: &App) -> Vec<Line<'static>> {
    vec![Line::from(format!(
        "{} | run state: {}",
        app.screen().title(),
        app.state().label()
    ))]
}

/// Build the key hint footer shared by every screen.
fn footer_lines(app: &App) -> Vec<Line<'static>> {
    if app.trust_prompt().is_some() {
        return vec![Line::from(
            "Trust confirmation: y/Enter grant  n/Esc deny  q quit",
        )];
    }
    if app.input().is_some() {
        return vec![Line::from(
            "type value  Backspace delete  Enter apply  Esc cancel  q quit",
        )];
    }
    vec![
        Line::from(
            "0-9 screens  c compare  e report  Tab cycle  Up/Down select  Left/Right cycle  Enter edit  +/- adjust  p priority  t trust  Ctrl+R run  Ctrl+E execute/export  Ctrl+B bundle export  Ctrl+I bundle import  Ctrl+S save  Ctrl+O reload  r refresh  q quit",
        ),
        Line::from(match app.screen() {
            Screen::Compare => {
                "Compare: b baseline  n candidate  t threshold  a alpha  s sided  f fail-on-regression  Ctrl+E compare"
            }
            Screen::Report => {
                "Report: f result  m CSV mode  g JSON path  y CSV path  u bundle path  d import dir  Ctrl+E export"
            }
            Screen::Integrations => {
                "Integrations: t confirm trust  y grant  n deny  metadata-only inspection before consent"
            }
            _ => "Compare and report screens expose their own local keys in their headers",
        }),
    ]
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
    lines.extend(trust_prompt_lines(app));
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
            Line::from("Trust consent: load metadata-only inspection before confirming first run"),
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
        Line::from("Untrusted plugins are refused until explicit consent is recorded"),
        Line::from("Process isolation is not a security sandbox"),
    ]
}

/// Build the active first-run trust confirmation prompt.
fn trust_prompt_lines(app: &App) -> Vec<Line<'static>> {
    let Some(prompt) = app.trust_prompt() else {
        if app.screen() == Screen::Integrations {
            return vec![Line::from(
                "Trust consent: press t on an inspected plugin to grant or deny first-run consent",
            )];
        }
        return Vec::new();
    };
    vec![
        Line::from("First-run trust confirmation required before execution:"),
        Line::from(format!("  Path: {}", prompt.path())),
        Line::from(format!(
            "  Identity: {} ({})",
            prompt.name(),
            prompt.identifier()
        )),
        Line::from(format!(
            "  Version: {}  SDK: {}",
            prompt.version(),
            prompt.sdk_version()
        )),
        Line::from(format!("  Capabilities: {}", prompt.capabilities())),
        Line::from(format!(
            "  Compatibility: {} ({})",
            prompt.compatibility_status(),
            value_or_placeholder(prompt.compatibility_reason(), "no reason recorded")
        )),
        Line::from(format!("  Current trust: {}", prompt.trust_state())),
        Line::from("  Decision: y/Enter grant trust, n/Esc deny and refuse execution"),
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

/// Build the standard editable-field list for one configuration screen.
fn editable_field_lines(app: &App) -> Vec<Line<'static>> {
    let Some(config) = app.config() else {
        return vec![Line::from("Config: (not loaded)")];
    };
    let mut lines = Vec::new();
    for (index, field) in app.editable_fields().iter().enumerate() {
        let marker = if index == app.selected_field_index() {
            ">"
        } else {
            " "
        };
        lines.push(Line::from(format!(
            "{marker} [{}] {:<24} {}  ({})",
            index,
            field.label(),
            field.current_value(config),
            field.path()
        )));
    }
    lines
}

/// Build the config identity and gate lines shared by all editing screens.
fn config_gate_lines(app: &App) -> Vec<Line<'static>> {
    let mut lines = Vec::new();
    if let Some(config) = app.config() {
        lines.push(Line::from(format!(
            "Config: version={} sdk={} integration={} {}",
            config.config_version(),
            config.sdk_version(),
            config.integration_id(),
            config.integration_version()
        )));
    }
    if !app.config_error().is_empty() {
        lines.push(Line::from(format!("Config error: {}", app.config_error())));
    }
    lines.push(Line::from(format!(
        "Validation: {}",
        value_or_placeholder(app.validation_note(), "not validated")
    )));
    if app.portability().is_allowed() {
        lines.push(Line::from("Portability gate: allowed"));
    } else {
        lines.push(Line::from(format!(
            "Portability gate: BLOCKED ({} findings)",
            app.portability().blockers().len()
        )));
    }
    for finding in app.portability().blockers() {
        lines.push(Line::from(format!("  blocker: {}", finding.message())));
    }
    for finding in app.portability().warnings() {
        lines.push(Line::from(format!("  warning: {}", finding.message())));
    }
    if let Some(desired) = app.pending_priority() {
        lines.push(Line::from(format!(
            "Priority confirmation pending: {desired}; press p again to confirm"
        )));
    }
    if let Some(input) = app.input() {
        lines.push(Line::from(format!(
            "Editing: {}{}",
            input.prompt(),
            input.buffer()
        )));
    }
    lines
}

/// Build the workload editing view.
fn workload_lines(app: &App) -> Vec<Line<'static>> {
    let mut lines = vec![Line::from(
        "Workload defines what runs; generator input is deterministic.",
    )];
    lines.extend(editable_field_lines(app));
    lines.extend(config_gate_lines(app));
    lines
}

/// Build the dimensions, profile and duration editing view.
fn configuration_lines(app: &App) -> Vec<Line<'static>> {
    let mut lines = vec![Line::from(
        "Dimensions and duration are recorded in the portable config before execution.",
    )];
    lines.extend(editable_field_lines(app));
    lines.extend(config_gate_lines(app));
    lines
}

/// Build the measurement detail and validation editing view.
fn measurement_lines(app: &App) -> Vec<Line<'static>> {
    let mut lines = vec![
        Line::from("Process-level and instrumented measurements remain distinct."),
        Line::from("End-to-end timing is opt-in and includes setup outside the DSP interval."),
    ];
    lines.extend(editable_field_lines(app));
    lines.extend(config_gate_lines(app));
    lines
}

/// Build the scheduling editing view with explicit priority warnings.
fn scheduling_lines(app: &App) -> Vec<Line<'static>> {
    let mut lines = vec![
        Line::from("Higher priority is never applied without the explicit opt-in confirmation."),
        Line::from("Raising priority can reduce system responsiveness and bias measurements."),
        Line::from(
            "CPU isolation is a request; the OS may refuse and fall back to inherited scheduling.",
        ),
        Line::from("Process isolation reduces crash impact; it is not a security sandbox."),
    ];
    lines.extend(editable_field_lines(app));
    lines.extend(config_gate_lines(app));
    lines
}

/// Build the built-in and saved profile view.
fn profiles_lines(app: &App) -> Vec<Line<'static>> {
    let mut lines = vec![Line::from("Built-in profiles:")];
    for profile in app.data().profiles().profiles() {
        lines.push(Line::from(format!(
            "  {} ({})",
            profile.name(),
            profile.description()
        )));
    }
    lines.push(Line::from(format!(
        "Saved profiles in {}:",
        app.profile_directory().display()
    )));
    if app.saved_profiles().is_empty() {
        lines.push(Line::from("  (none)"));
    }
    for (index, profile) in app.saved_profiles().iter().enumerate() {
        let marker = if index == app.selected_profile_index() {
            ">"
        } else {
            " "
        };
        lines.push(Line::from(format!(
            "{marker} [{}] {} -> {}",
            index,
            profile.name(),
            profile.path().display()
        )));
    }
    lines.push(Line::from(
        "Enter edits the active profile name; Ctrl+S saves it; Ctrl+O reloads the selection.",
    ));
    lines.extend(editable_field_lines(app));
    lines.extend(config_gate_lines(app));
    lines
}

/// Build the live execution view from copied core progress events.
///
/// Process-level and instrumented measurements are rendered as separate sections
/// with explicit unavailability text. Live progress carries no metric values, so
/// this view never presents a process-level number as a DSP/callback measurement.
fn execution_lines(app: &App) -> Vec<Line<'static>> {
    let progress = app.progress();
    let mut lines = vec![
        Line::from(format!("Run state: {}", app.state().label())),
        Line::from(format!(
            "Config: {}",
            if progress.config_path.is_empty() {
                "(not started)"
            } else {
                &progress.config_path
            }
        )),
        Line::from(format!(
            "Test point: {}",
            if progress.test_point == 0 {
                "(not reported)".to_string()
            } else {
                progress.test_point.to_string()
            }
        )),
        Line::from(format!("Timing level: {}", progress.timing_level.label())),
        Line::from(format!(
            "Isolation mode: {}",
            progress.isolation_mode.label()
        )),
        Line::from(format!(
            "Progress: {}",
            if progress.message.is_empty() {
                "(waiting for core progress events)"
            } else {
                &progress.message
            }
        )),
        Line::from(format!("Result: {}", progress.result_status)),
        Line::from("Process-level measurements (black-box): unavailable in live progress"),
        Line::from(
            "  Process CPU, memory and system observations are not carried by progress events",
        ),
        Line::from(
            "Instrumented DSP/callback measurements (SDK hooks): unavailable in live progress",
        ),
        Line::from(format!(
            "  Recorded samples use timing level {}; detailed values belong to the results screen",
            progress.timing_level.label()
        )),
        Line::from(format!("Warnings: {}", app.warnings().len())),
    ];
    if app.warnings().is_empty() {
        lines.push(Line::from("  (none)"));
    } else {
        lines.extend(
            app.warnings()
                .iter()
                .map(|warning| Line::from(format!("  - {warning}"))),
        );
    }
    lines.push(Line::from(
        "Process isolation, when selected, is containment for crashes and is not a security sandbox",
    ));
    lines
}

/// Build the stored results inspection view from one typed result document.
///
/// Completeness comes from stored failure evidence rather than from a single
/// status token, and no metric is recomputed here.
fn results_lines(app: &App) -> Vec<Line<'static>> {
    let Some(path) = app.result_path() else {
        return vec![
            Line::from("Result document: (no completed run available)"),
            Line::from("Completeness: unavailable; no result was loaded"),
            Line::from("Press Ctrl+R to run; results are read from the stored v1 document"),
        ];
    };
    let mut lines = vec![Line::from(format!("Result document: {}", path.display()))];
    let Some(inspection) = app.result_inspection() else {
        lines.push(Line::from(
            "Inspection: unavailable; the stored document was not decoded",
        ));
        lines.push(Line::from(
            "Completeness: unavailable; no stored evidence was loaded",
        ));
        if !app.status().is_empty() {
            lines.push(Line::from(format!("Status: {}", app.status())));
        }
        return lines;
    };
    lines.extend(result_summary_lines(inspection));
    for (index, run) in inspection.runs().iter().enumerate() {
        lines.extend(run_inspection_lines(run, index));
    }
    lines.extend(issue_lines("Session warnings", inspection.warnings()));
    lines.extend(issue_lines("Session failures", inspection.failures()));
    lines
}

/// Build document-level status and measurement-class context lines.
fn result_summary_lines(inspection: &ResultInspection) -> Vec<Line<'static>> {
    let mut lines = vec![
        Line::from(format!(
            "Schema: {} sdk={} runs={}",
            inspection.schema_version(),
            inspection.sdk_version(),
            inspection.runs().len()
        )),
        Line::from(format!(
            "Stored status token: {}",
            inspection.stored_status()
        )),
        Line::from(format!(
            "Completeness: {}",
            if inspection.is_complete() {
                "complete"
            } else {
                "incomplete"
            }
        )),
        Line::from(format!("Derived status: {}", inspection.derived_status())),
        Line::from(
            "Process-level measurements (black-box): stored metadata only; no DSP timing inferred",
        ),
        Line::from(
            "Instrumented DSP/callback measurements (SDK hooks): metric records listed per run",
        ),
    ];
    if !inspection.status_reason().is_empty() {
        lines.push(Line::from(format!(
            "Status evidence: {}",
            inspection.status_reason()
        )));
    }
    lines
}

/// Build one stored run's metrics, validation, issues, raw data and overhead view.
fn run_inspection_lines(run: &RunInspection, index: usize) -> Vec<Line<'static>> {
    let mut lines = vec![
        Line::from(format!(
            "Run [{}] {} stored_status={} complete={}",
            index,
            run.id(),
            run.status(),
            if run.is_complete() { "yes" } else { "no" }
        )),
        Line::from(format!("  Test point: {}", run.test_point())),
        Line::from(format!(
            "  Execution mode: {} (process isolation is not a security sandbox)",
            run.isolation_mode()
        )),
        Line::from(format!(
            "  Validation: level={} cases_checked={}",
            value_or_placeholder(run.validation().level(), "(unreported)"),
            run.validation().cases_checked()
        )),
    ];
    for check in run.validation().checks() {
        lines.push(Line::from(format!(
            "    check={} case={} status={} reason={}",
            check.check(),
            value_or_placeholder(check.case_label(), "(none)"),
            check.status(),
            value_or_placeholder(check.reason(), "no reason recorded")
        )));
    }
    lines.push(Line::from(
        "  Metrics (stored status/value; nothing recomputed by the TUI):",
    ));
    for metric in run.metrics() {
        lines.push(Line::from(format!(
            "    {} {} status={} value={} source={}",
            metric.id(),
            metric.name(),
            metric.status().label(),
            metric.display_value(),
            value_or_placeholder(metric.source(), "(unreported)")
        )));
    }
    lines.extend(issue_lines("  Warnings", run.warnings()));
    lines.extend(issue_lines("  Failures", run.failures()));
    if run.raw_samples().is_empty() {
        lines.push(Line::from(
            "  Raw samples: none serialized; an aggregate raw basis need not be retained",
        ));
    } else {
        let groups = run
            .raw_samples()
            .iter()
            .map(|group| format!("{}={}", group.level(), group.count()))
            .collect::<Vec<_>>()
            .join(" ");
        lines.push(Line::from(format!("  Retained raw samples: {groups}")));
    }
    match run.overhead() {
        Some(overhead) => lines.push(Line::from(format!(
            "  Retained overhead estimate (separate; not subtracted): {}",
            overhead.display()
        ))),
        None => lines.push(Line::from(
            "  Retained overhead estimate: unavailable in this document",
        )),
    }
    lines
}

/// Build a labelled list of classified warnings or failures with no empty state hidden.
fn issue_lines(label: &str, issues: &[ResultIssue]) -> Vec<Line<'static>> {
    let mut lines = vec![Line::from(format!("{label}: {}", issues.len()))];
    if issues.is_empty() {
        lines.push(Line::from("  (none)"));
    } else {
        lines.extend(
            issues
                .iter()
                .map(|issue| Line::from(format!("  - {}", issue.display()))),
        );
    }
    lines
}

/// Build the comparison view from one copied core comparison record.
///
/// Threshold, significance and classification text comes from that record; the
/// TUI never recomputes a difference or promotes a raw difference to regression.
fn comparison_lines(app: &App) -> Vec<Line<'static>> {
    let baseline = app
        .baseline_path()
        .map(|path| path.display().to_string())
        .unwrap_or_else(|| "(none selected)".to_string());
    let candidate = app
        .candidate_path()
        .map(|path| path.display().to_string())
        .unwrap_or_else(|| "(none selected)".to_string());
    let mut lines = vec![
        Line::from("Comparison evidence is read from a core record; no metric is recomputed here"),
        Line::from(format!("Baseline: {baseline}")),
        Line::from(format!("Candidate: {candidate}")),
        Line::from(format!(
            "Policy: threshold={} alpha={} sided={} fail_on_regression={}",
            value_or_placeholder(app.threshold_percent(), "(unset)"),
            value_or_placeholder(app.alpha(), "(unset)"),
            if app.one_sided() { "one" } else { "two" },
            app.fail_on_regression()
        )),
    ];
    let Some(inspection) = app.comparison() else {
        lines.push(Line::from(
            "Comparison: unavailable; select both paths then press Ctrl+E",
        ));
        if !app.status().is_empty() {
            lines.push(Line::from(format!("Status: {}", app.status())));
        }
        return lines;
    };
    let record = inspection.record();
    lines.push(Line::from(format!(
        "Envelope: status={} regression_detected={}",
        value_or_placeholder(inspection.envelope_status(), "(unreported)"),
        inspection.regression_detected()
    )));
    if !inspection.envelope_error().is_empty() {
        lines.push(Line::from(format!(
            "Envelope error: {}",
            inspection.envelope_error()
        )));
    }
    lines.push(Line::from(format!(
        "Level: {}  Classification: {}",
        value_or_placeholder(record.level(), "(unreported)"),
        value_or_placeholder(record.classification(), "(unreported)")
    )));
    lines.push(Line::from(format!(
        "Baseline evidence: {} status={}",
        value_or_placeholder(record.baseline_reference(), "(unreported)"),
        value_or_placeholder(record.baseline_status(), "(unreported)")
    )));
    lines.push(Line::from(format!(
        "Candidate evidence: {} status={}",
        value_or_placeholder(record.candidate_reference(), "(unreported)"),
        value_or_placeholder(record.candidate_status(), "(unreported)")
    )));
    lines.push(Line::from(format!(
        "Threshold: {} status={} reason={}",
        record.threshold_percent().unwrap_or("null"),
        value_or_placeholder(record.threshold_status(), "(unreported)"),
        value_or_placeholder(record.threshold_reason(), "(none recorded)")
    )));
    for (name, value) in record.statistics_summary() {
        lines.push(Line::from(format!("Statistics {name}: {value}")));
    }
    for (name, value) in record.significance_summary() {
        lines.push(Line::from(format!("Significance {name}: {value}")));
    }
    if record.comparability_warnings().is_empty() {
        lines.push(Line::from("Comparability warnings: none recorded"));
    } else {
        lines.push(Line::from("Comparability warnings:"));
        for warning in record.comparability_warnings() {
            lines.push(Line::from(format!(
                "  - {}: {} left={} ({}) right={} ({})",
                value_or_placeholder(warning.category(), "warning"),
                value_or_placeholder(warning.message(), "(no message)"),
                value_or_placeholder(warning.left(), "(unavailable)"),
                value_or_placeholder(warning.field(), "(unknown field)"),
                value_or_placeholder(warning.right(), "(unavailable)"),
                value_or_placeholder(warning.field(), "(unknown field)")
            )));
        }
    }
    if record.metrics().is_empty() {
        lines.push(Line::from("Metrics: none recorded"));
    } else {
        lines.push(Line::from("Metrics (stored decisions):"));
        for metric in record.metrics() {
            lines.push(Line::from(format!(
                "  - {} [{}] comparison={} classification={}",
                value_or_placeholder(metric.name(), "(unnamed)"),
                value_or_placeholder(metric.metric_id(), "(unknown id)"),
                value_or_placeholder(metric.comparison_status(), "(unreported)"),
                value_or_placeholder(metric.classification(), "(unreported)")
            )));
            lines.push(Line::from(format!(
                "    baseline={} candidate={} delta={} relative={}%",
                value_or_placeholder(metric.baseline_value(), "null"),
                value_or_placeholder(metric.candidate_value(), "null"),
                value_or_placeholder(metric.delta(), "null"),
                value_or_placeholder(metric.relative_change_percent(), "null")
            )));
            lines.push(Line::from(format!(
                "    threshold={} significance={} reason={} interval={}",
                value_or_placeholder(metric.threshold_status(), "(unreported)"),
                value_or_placeholder(metric.significance_status(), "(unreported)"),
                value_or_placeholder(metric.significance_reason(), "(none recorded)"),
                value_or_placeholder(metric.interval(), "null")
            )));
            if !metric.reason().is_empty() {
                lines.push(Line::from(format!("    note: {}", metric.reason())));
            }
        }
    }
    lines
}

/// Build the report, export and session-bundle view from copied envelopes.
fn report_lines(app: &App) -> Vec<Line<'static>> {
    let path = app
        .report_path()
        .map(|path| path.display().to_string())
        .unwrap_or_else(|| "(none selected)".to_string());
    let mut lines = vec![
        Line::from(
            "Report and CSV text are copied projections; no metric is recomputed in the TUI",
        ),
        Line::from(format!("Report document: {path}")),
        Line::from(format!("CSV row mode: {}", app.csv_row_mode())),
    ];
    if let Some(inspection) = app.report() {
        lines.extend(result_summary_lines(inspection.result()));
        lines.push(Line::from(format!(
            "Report envelope: status={} error={}",
            value_or_placeholder(inspection.envelope_status(), "(unreported)"),
            value_or_placeholder(inspection.envelope_error(), "(none)")
        )));
        lines.push(Line::from(format!(
            "Report stored status: {}",
            value_or_placeholder(inspection.stored_status(), "(unreported)")
        )));
        lines.push(Line::from(format!(
            "Report derived status: {}",
            value_or_placeholder(inspection.derived_status(), "(unreported)")
        )));
        lines.push(Line::from(format!(
            "Status reason: {}",
            value_or_placeholder(inspection.status_reason(), "(none recorded)")
        )));
        if inspection.result().comparisons().is_empty() {
            lines.push(Line::from("Stored comparisons: none"));
        } else {
            lines.push(Line::from(format!(
                "Stored comparisons: {} record(s) decoded from the result document",
                inspection.result().comparisons().len()
            )));
        }
    } else {
        lines.push(Line::from(
            "Report: unavailable; select a result path then press r",
        ));
        if !app.status().is_empty() {
            lines.push(Line::from(format!("Status: {}", app.status())));
        }
    }
    lines.push(Line::from(format!(
        "JSON export path: {}",
        app.json_export_path()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "(unset; press g)".to_string())
    )));
    lines.push(Line::from(format!(
        "CSV export path: {}",
        app.csv_export_path()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "(unset; press y)".to_string())
    )));
    lines.push(Line::from(format!(
        "CSV preview: {}",
        if app.csv_export().is_empty() {
            "(no copied projection)".to_string()
        } else {
            app.csv_export()
                .lines()
                .take(3)
                .collect::<Vec<_>>()
                .join(" | ")
        }
    )));
    lines.push(Line::from(format!(
        "Session bundle path: {}",
        app.bundle_path()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "(unset; press u)".to_string())
    )));
    lines.push(Line::from(format!(
        "Bundle import directory: {}",
        app.bundle_target_dir().display()
    )));
    lines.push(Line::from(format!(
        "Bundle status: {}",
        value_or_placeholder(app.bundle_status(), "(none)")
    )));
    lines
}

/// Render one complete application frame.
pub fn render(frame: &mut Frame<'_>, app: &App) {
    let [header_area, main_area, keys_area] = Layout::vertical([
        Constraint::Length(3),
        Constraint::Min(0),
        Constraint::Length(4),
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
        Screen::Workload => workload_lines(app),
        Screen::Configuration => configuration_lines(app),
        Screen::Measurement => measurement_lines(app),
        Screen::Scheduling => scheduling_lines(app),
        Screen::Execution => execution_lines(app),
        Screen::Profiles => profiles_lines(app),
        Screen::Results => results_lines(app),
        Screen::Compare => comparison_lines(app),
        Screen::Report => report_lines(app),
    };
    frame.render_widget(
        Paragraph::new(body).block(Block::bordered().title(app.screen().title())),
        main_area,
    );

    frame.render_widget(
        Paragraph::new(footer_lines(app)).block(Block::bordered().title("Keys")),
        keys_area,
    );
}

/// Unit tests for headless screen rendering.
#[cfg(test)]
mod tests {
    use std::ffi::c_char;
    use std::mem::size_of;

    use ratatui::{Terminal, backend::TestBackend};

    use super::{
        comparison_lines, config_gate_lines, dashboard_lines, editable_field_lines,
        execution_lines, field_line, integration_lines, matching_inspection, profiles_lines,
        render, report_lines, results_lines, scheduling_lines, system_lines, system_summary,
        value_or_placeholder,
    };
    use crate::app::{App, Screen};
    use crate::config::DEFAULT_CONFIG_JSON;
    use crate::data::{
        ComparisonInspection, DashboardData, PluginInspection, PortabilityReport, ReportInspection,
        ResultInspection,
    };
    use crate::profile::SavedProfile;
    use crate::{
        ZTICK_FRONTEND_EVENT_MESSAGE_SIZE, ZTICK_FRONTEND_EVENT_PROGRESS,
        ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS, ZTICK_FRONTEND_STATE_RUNNING,
        ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION, ZTICK_FRONTEND_TIMING_PER_BLOCK, ZtickFrontendEvent,
    };

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

    /// Build an app with a retained result path and one decoded inspection.
    fn result_app(text: &str) -> App {
        let mut app = App::new();
        app.set_run_result(r#"{"status":"success","data":{"output_path":"/tmp/result.json"}}"#);
        let inspection = ResultInspection::from_json(text).expect("result document decodes");
        app.set_result_inspection(Some(inspection));
        app
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
        assert!(text.contains("Untrusted plugins are refused until explicit consent is recorded"));
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

    /// Verify trust confirmation prompt shows identity, capabilities and decision keys.
    #[test]
    fn integration_lines_show_trust_confirmation_prompt() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        app.set_inspection(Some(test_inspection()));
        assert!(app.open_trust_prompt());
        let text = lines_text(&integration_lines(&app));
        assert!(text.contains("First-run trust confirmation required before execution:"));
        assert!(text.contains("Identity: App A (app-a)"));
        assert!(text.contains("Version: 1.2.3  SDK: 1.0.0"));
        assert!(text.contains("Capabilities: 0x7c"));
        assert!(text.contains("Compatibility: success"));
        assert!(text.contains("Current trust: untrusted"));
        assert!(text.contains("y/Enter grant trust, n/Esc deny and refuse execution"));
    }

    /// Verify matching inspection refuses a stale path.
    #[test]
    fn matching_inspection_refuses_a_stale_path() {
        let inspection = test_inspection();
        assert!(matching_inspection(Some(&inspection), "/app.so").is_some());
        assert!(matching_inspection(Some(&inspection), "/other.so").is_none());
        assert!(matching_inspection(None, "/app.so").is_none());
    }

    /// Verify editable screens show field paths, current values and gate state.
    #[test]
    fn editable_screens_show_fields_values_and_gate_state() {
        let mut app = App::new();
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        app.set_validation_note("valid");
        let report = PortabilityReport::blocked("integration lacks capability: per-block timing");
        app.set_portability(report);

        for screen in [
            Screen::Workload,
            Screen::Configuration,
            Screen::Measurement,
            Screen::Scheduling,
            Screen::Profiles,
        ] {
            app.set_screen(screen);
            let fields = lines_text(&editable_field_lines(&app));
            let first_path = app
                .editable_fields()
                .first()
                .expect("editing screens have fields")
                .path();
            assert!(fields.contains(first_path), "screen {screen:?} has fields");
            let gate = lines_text(&config_gate_lines(&app));
            assert!(gate.contains("Validation: valid"));
            assert!(gate.contains("Portability gate: BLOCKED"));
            assert!(gate.contains("integration lacks capability"));
        }

        app.set_screen(Screen::Scheduling);
        let scheduling = lines_text(&scheduling_lines(&app));
        assert!(scheduling.contains("explicit opt-in confirmation"));
        assert!(scheduling.contains("not a security sandbox"));
    }

    /// Verify profile lines list built-ins, saved profiles and an empty fallback.
    #[test]
    fn profile_lines_list_builtins_and_saved_profiles() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        let empty = lines_text(&profiles_lines(&app));
        assert!(empty.contains("Quick (fast)"));
        assert!(empty.contains("(none)"));

        app.set_saved_profiles(vec![SavedProfile::new("Saved", "/profiles/Saved.json")]);
        let text = lines_text(&profiles_lines(&app));
        assert!(text.contains("Saved -> /profiles/Saved.json"));
        assert!(!text.contains("(none)"));
    }

    /// Verify render draws every registered screen.
    #[test]
    fn render_draws_every_registered_screen() {
        let mut app = App::new();
        app.set_dashboard_data(test_data());
        app.set_config_json(DEFAULT_CONFIG_JSON)
            .expect("default config decodes");
        for screen in Screen::ALL {
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
                text.contains("0-9 screens"),
                "screen {screen:?} shows key hints"
            );
        }
    }

    /// Verify the execution view shows copied context and separates measurement classes.
    #[test]
    fn execution_lines_show_live_context_and_distinct_measurement_sections() {
        let mut app = App::new();
        app.begin_run("/tmp/ztick-run.json");
        let message = "repetition 2/3";
        let mut bytes = [0 as c_char; ZTICK_FRONTEND_EVENT_MESSAGE_SIZE];
        for (slot, value) in bytes.iter_mut().zip(message.bytes()) {
            *slot = value as c_char;
        }
        let event = ZtickFrontendEvent {
            size: size_of::<ZtickFrontendEvent>() as u32,
            version: ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION,
            kind: ZTICK_FRONTEND_EVENT_PROGRESS,
            state: ZTICK_FRONTEND_STATE_RUNNING,
            test_point: 2,
            timing_level: ZTICK_FRONTEND_TIMING_PER_BLOCK,
            isolation_mode: ZTICK_FRONTEND_ISOLATION_OUT_OF_PROCESS,
            message: bytes,
        };
        app.observe_event(&event);
        app.push_warning("timer drift");
        app.set_run_result(
            r#"{"status":"success","exit_code":0,"data":{"output_path":"/tmp/result.json"}}"#,
        );

        let text = lines_text(&execution_lines(&app));
        assert!(text.contains("Run state: Running"));
        assert!(text.contains("Config: /tmp/ztick-run.json"));
        assert!(text.contains("Test point: 2"));
        assert!(text.contains("Timing level: per_block"));
        assert!(text.contains("Isolation mode: out_of_process"));
        assert!(text.contains("Progress: repetition 2/3"));
        assert!(text.contains("Result: success; output=/tmp/result.json"));
        assert!(text.contains("Warnings: 1"));
        assert!(text.contains("timer drift"));
        assert!(
            text.contains("Process-level measurements (black-box): unavailable in live progress")
        );
        assert!(text.contains(
            "Instrumented DSP/callback measurements (SDK hooks): unavailable in live progress"
        ));
    }

    /// Verify stored results expose all four statuses and every retained evidence class.
    #[test]
    fn results_lines_show_all_statuses_validation_and_retained_evidence() {
        let app = result_app(crate::data::fixtures::RESULT_JSON);
        let text = lines_text(&results_lines(&app));
        assert!(text.contains("Result document: /tmp/result.json"));
        assert!(text.contains("Completeness: complete"));
        assert!(text.contains("status=measured"));
        assert!(text.contains("status=estimated"));
        assert!(text.contains("status=unavailable"));
        assert!(text.contains("status=not_applicable"));
        assert!(text.contains("check=finite_output"));
        assert!(text.contains("status=passed"));
        assert!(text.contains("execution: thermal drift"));
        assert!(text.contains("Retained raw samples: per_block=2 whole_run=0"));
        assert!(text.contains("Retained overhead estimate (separate; not subtracted)"));
        assert!(text.contains("calibration_samples=1"));
        assert!(text.contains("Process-level measurements (black-box)"));
        assert!(text.contains("Instrumented DSP/callback measurements (SDK hooks)"));
        assert!(text.contains("not a security sandbox"));
    }

    /// Verify failure evidence is never rendered as a complete result.
    #[test]
    fn results_lines_never_render_incomplete_evidence_as_complete() {
        let failed = crate::data::fixtures::RESULT_JSON.replacen(
            "\"failures\":[]",
            "\"failures\":[{\"category\":\"workload_failure\",\"message\":\"validation checks failed\"}]",
            1,
        );
        let app = result_app(&failed);
        let text = lines_text(&results_lines(&app));
        assert!(text.contains("Completeness: incomplete"));
        assert!(!text.contains("Completeness: complete"));
        assert!(text.contains("workload_failure: validation checks failed"));
        assert!(text.contains("complete=no"));

        let empty = App::new();
        let text = lines_text(&results_lines(&empty));
        assert!(text.contains("no completed run available"));
        assert!(!text.contains("Completeness: complete"));
    }

    /// Verify an unreadable result document is shown as unavailable evidence.
    #[test]
    fn results_lines_report_an_unreadable_result_document() {
        let mut app = App::new();
        app.set_run_result(
            r#"{"status":"success","data":{"output_path":"/tmp/missing-result.json"}}"#,
        );
        let text = lines_text(&results_lines(&app));
        assert!(text.contains("Inspection: unavailable"));
        assert!(text.contains("Completeness: unavailable"));
        assert!(!text.contains("Completeness: complete"));
    }

    /// Verify comparison rendering shows stored decisions without recomputing them.
    #[test]
    fn comparison_lines_render_stored_policy_significance_and_warnings() {
        const COMPARISON: &str = r#"{"baseline":{"reference":"a.json","status":"complete"},"candidate":{"reference":"b.json","status":"complete"},"level":"run_to_run","threshold_percent":7.5,"threshold_status":{"status":"applied","reason":"configured threshold applied"},"statistics":{"alpha":0.05,"sided":"two"},"significance":{"status":"applied","reason":"bootstrap interval excludes zero"},"classification":"regression","comparability_warnings":[{"category":"comparability","field":"machine.cpu_model","message":"cpu model differs","left_status":"value","left_value":"old","right_status":"value","right_value":"new"}],"metrics":[{"metric_id":"MET-01","name":"Execution time","baseline_status":"measured","candidate_status":"measured","baseline_value":0.5,"candidate_value":0.6,"delta":0.1,"relative_change_percent":20.0,"comparison_status":"raw_difference","classification":"regression","threshold_status":{"status":"applied"},"significance":{"status":"applied","reason":"interval excludes zero","interval_low":0.08,"interval_high":0.12},"reason":""}]}"#;
        let envelope = format!(
            r#"{{"schema_version":1,"command":"compare","status":"success","exit_code":0,"data":{{"comparison":{COMPARISON},"regression_detected":true}},"error":""}}"#
        );
        let inspection =
            ComparisonInspection::from_envelope(&envelope).expect("compare envelope decodes");
        let mut app = App::new();
        app.set_baseline_path(Some(std::path::PathBuf::from("a.json")));
        app.set_candidate_path(Some(std::path::PathBuf::from("b.json")));
        app.set_threshold_percent("7.5");
        app.set_alpha("0.05");
        app.set_comparison(Some(inspection));
        let text = lines_text(&comparison_lines(&app));
        assert!(text.contains("no metric is recomputed"));
        assert!(text.contains("threshold=7.5 alpha=0.05 sided=two"));
        assert!(text.contains("regression_detected=true"));
        assert!(text.contains("Classification: regression"));
        assert!(text.contains("Threshold: 7.5 status=applied reason=configured threshold applied"));
        assert!(text.contains("Significance status: applied"));
        assert!(text.contains("cpu model differs"));
        assert!(text.contains("comparison=raw_difference classification=regression"));
        assert!(text.contains("interval=[0.08, 0.12]"));

        let empty = App::new();
        let text = lines_text(&comparison_lines(&empty));
        assert!(text.contains("Comparison: unavailable"));
    }

    /// Verify report rendering keeps stored and derived status distinct.
    #[test]
    fn report_lines_keep_stored_and_derived_status_distinct() {
        let envelope = format!(
            r#"{{"schema_version":1,"command":"report","status":"success","exit_code":0,"data":{{"document":{},"stored_status":"complete","derived_status":"incomplete","status_reason":"recorded failures"}},"error":""}}"#,
            crate::data::fixtures::RESULT_JSON
        );
        let inspection = ReportInspection::from_envelope(&envelope).expect("report decodes");
        let mut app = App::new();
        app.set_report(Some(inspection));
        app.set_csv_export("session_id,run_id\ns-1,run-1");
        app.set_json_export_path(Some(std::path::PathBuf::from("out/report.json")));
        app.set_csv_export_path(Some(std::path::PathBuf::from("out/report.csv")));
        let text = lines_text(&report_lines(&app));
        assert!(text.contains("Report stored status: complete"));
        assert!(text.contains("Report derived status: incomplete"));
        assert!(text.contains("Status reason: recorded failures"));
        assert!(text.contains("CSV preview: session_id,run_id | s-1,run-1"));
        assert!(text.contains("JSON export path: out/report.json"));
        assert!(text.contains("CSV export path: out/report.csv"));

        let empty = App::new();
        let text = lines_text(&report_lines(&empty));
        assert!(text.contains("Report: unavailable"));
    }
}
