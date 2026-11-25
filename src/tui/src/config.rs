// SPDX-License-Identifier: MIT
//! Portable v1 config defaults and field-edit helpers for the configuration screens.
//!
//! Every helper produces a dotted `path=value` assignment accepted by
//! `ztick_frontend_config_override`. This module never applies an edit itself;
//! the safe front-end wrapper remains the only path that changes session config.

use crate::app::Screen;
use crate::data::{DataError, PortableConfig};

/// A complete, portable v1 config used when the TUI starts with no saved document.
pub const DEFAULT_CONFIG_JSON: &str = r#"{"config_version":"1.0.0","sdk_version":"1.0.0","integration":{"id":"app-a","version":"1.2.3"},"workload":{"level":"processor","processors":["eq"],"generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,"phase_rad":0.0}},"dimensions":{"sample_rate_hz":48000.0,"block_size":128,"channels":2,"oversampling":1,"instances":1},"execution":{"duration_mode":"iterations","iterations":100,"duration_seconds":0.0,"repetitions":2,"warmup":{"iterations":0},"isolation":"auto"},"profile":{"name":"Quick","reliability_checks":[]},"measurement":{"detail":"aggregate","end_to_end":false,"timing_levels":["per_block","whole_run"]},"validation":{"level":"Basic","tolerances":{"absolute":0.000001,"relative":0.000001},"references":[]},"parallelism":{"workers":"auto","affinity":"auto","cpu_isolation":"off","priority_opt_in":false,"priority":0}}"#;

/// Context used when an editable field cycles through discovered values.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct EditOptions {
    /// Stable identifiers advertised by discovered integrations.
    pub integration_ids: Vec<String>,
}

/// The editable fields supported by the Phase 5.3 screens.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EditableField {
    /// Stable integration identifier recorded in the config.
    IntegrationId,
    /// Integration version recorded in the config.
    IntegrationVersion,
    /// Workload level selector.
    WorkloadLevel,
    /// Processor names used by processor and chain workloads.
    Processors,
    /// Built-in generator kind.
    GeneratorKind,
    /// Generator amplitude.
    Amplitude,
    /// Sine generator frequency.
    FrequencyHz,
    /// Sample rate in hertz.
    SampleRateHz,
    /// DSP block size in frames.
    BlockSize,
    /// Channel count.
    Channels,
    /// Workload instance count.
    Instances,
    /// Oversampling factor.
    Oversampling,
    /// Active profile name.
    ProfileName,
    /// Profile reliability check tokens.
    ReliabilityChecks,
    /// Duration mode selector.
    DurationMode,
    /// Fixed iteration count.
    Iterations,
    /// Target duration in seconds.
    DurationSeconds,
    /// Repetition count.
    Repetitions,
    /// Warm-up iteration count excluded from timing.
    WarmupIterations,
    /// Measurement detail selector.
    MeasurementDetail,
    /// Explicit end-to-end timing opt-in.
    EndToEnd,
    /// Recorded timing levels.
    TimingLevels,
    /// Validation level selector.
    ValidationLevel,
    /// Absolute reference-comparison tolerance.
    AbsoluteTolerance,
    /// Relative reference-comparison tolerance.
    RelativeTolerance,
    /// Worker count token.
    Workers,
    /// CPU affinity token.
    Affinity,
    /// Optional OS-permitted CPU isolation request.
    CpuIsolation,
    /// Plugin process isolation request token.
    Isolation,
    /// Explicit scheduling-priority opt-in.
    PriorityOptIn,
    /// Requested scheduling priority value.
    PriorityValue,
}

/// Provide screen membership, labels, values and override assignments.
impl EditableField {
    /// Return the editable fields shown on one screen.
    pub fn fields_for(screen: Screen) -> &'static [Self] {
        match screen {
            Screen::Workload => &[
                Self::IntegrationId,
                Self::IntegrationVersion,
                Self::WorkloadLevel,
                Self::Processors,
                Self::GeneratorKind,
                Self::Amplitude,
                Self::FrequencyHz,
            ],
            Screen::Configuration => &[
                Self::SampleRateHz,
                Self::BlockSize,
                Self::Channels,
                Self::Instances,
                Self::Oversampling,
                Self::ProfileName,
                Self::DurationMode,
                Self::Iterations,
                Self::DurationSeconds,
                Self::Repetitions,
                Self::WarmupIterations,
                Self::Isolation,
            ],
            Screen::Measurement => &[
                Self::MeasurementDetail,
                Self::EndToEnd,
                Self::TimingLevels,
                Self::ValidationLevel,
                Self::AbsoluteTolerance,
                Self::RelativeTolerance,
            ],
            Screen::Scheduling => &[
                Self::Workers,
                Self::Affinity,
                Self::CpuIsolation,
                Self::Isolation,
                Self::PriorityOptIn,
                Self::PriorityValue,
            ],
            Screen::Profiles => &[Self::ProfileName, Self::ReliabilityChecks],
            Screen::Dashboard
            | Screen::System
            | Screen::Integrations
            | Screen::Execution
            | Screen::Results => &[],
        }
    }

    /// Return the stable label rendered for this field.
    pub fn label(self) -> &'static str {
        match self {
            Self::IntegrationId => "integration id",
            Self::IntegrationVersion => "integration version",
            Self::WorkloadLevel => "workload level",
            Self::Processors => "processors",
            Self::GeneratorKind => "generator kind",
            Self::Amplitude => "generator amplitude",
            Self::FrequencyHz => "generator frequency_hz",
            Self::SampleRateHz => "sample_rate_hz",
            Self::BlockSize => "block_size",
            Self::Channels => "channels",
            Self::Instances => "instances",
            Self::Oversampling => "oversampling",
            Self::ProfileName => "profile name",
            Self::ReliabilityChecks => "reliability checks",
            Self::DurationMode => "duration mode",
            Self::Iterations => "iterations",
            Self::DurationSeconds => "duration_seconds",
            Self::Repetitions => "repetitions",
            Self::WarmupIterations => "warm-up iterations",
            Self::MeasurementDetail => "measurement detail",
            Self::EndToEnd => "end-to-end timing",
            Self::TimingLevels => "timing levels",
            Self::ValidationLevel => "validation level",
            Self::AbsoluteTolerance => "absolute tolerance",
            Self::RelativeTolerance => "relative tolerance",
            Self::Workers => "workers",
            Self::Affinity => "affinity",
            Self::CpuIsolation => "CPU isolation request",
            Self::Isolation => "process isolation",
            Self::PriorityOptIn => "higher priority opt-in",
            Self::PriorityValue => "priority value",
        }
    }

    /// Return the dotted config path changed by this field.
    pub fn path(self) -> &'static str {
        match self {
            Self::IntegrationId => "integration.id",
            Self::IntegrationVersion => "integration.version",
            Self::WorkloadLevel => "workload.level",
            Self::Processors => "workload.processors",
            Self::GeneratorKind => "workload.generator",
            Self::Amplitude => "workload.generator.amplitude",
            Self::FrequencyHz => "workload.generator.frequency_hz",
            Self::SampleRateHz => "dimensions.sample_rate_hz",
            Self::BlockSize => "dimensions.block_size",
            Self::Channels => "dimensions.channels",
            Self::Instances => "dimensions.instances",
            Self::Oversampling => "dimensions.oversampling",
            Self::ProfileName => "profile.name",
            Self::ReliabilityChecks => "profile.reliability_checks",
            Self::DurationMode => "execution.duration_mode",
            Self::Iterations => "execution.iterations",
            Self::DurationSeconds => "execution.duration_seconds",
            Self::Repetitions => "execution.repetitions",
            Self::WarmupIterations => "execution.warmup.iterations",
            Self::MeasurementDetail => "measurement.detail",
            Self::EndToEnd => "measurement.end_to_end",
            Self::TimingLevels => "measurement.timing_levels",
            Self::ValidationLevel => "validation.level",
            Self::AbsoluteTolerance => "validation.tolerances.absolute",
            Self::RelativeTolerance => "validation.tolerances.relative",
            Self::Workers => "parallelism.workers",
            Self::Affinity => "parallelism.affinity",
            Self::CpuIsolation => "parallelism.cpu_isolation",
            Self::Isolation => "execution.isolation",
            Self::PriorityOptIn => "parallelism.priority_opt_in",
            Self::PriorityValue => "parallelism.priority",
        }
    }

    /// Return a human-readable current value for this field.
    pub fn current_value(self, config: &PortableConfig) -> String {
        match self {
            Self::IntegrationId => config.integration_id().to_string(),
            Self::IntegrationVersion => config.integration_version().to_string(),
            Self::WorkloadLevel => config.workload_level().to_string(),
            Self::Processors => joined_or_none(&config.processors()),
            Self::GeneratorKind => config.generator_kind().to_string(),
            Self::Amplitude => format_option(config.amplitude()),
            Self::FrequencyHz => format_option(config.frequency_hz()),
            Self::SampleRateHz => format_option(config.sample_rate_hz()),
            Self::BlockSize => format_option(config.block_size()),
            Self::Channels => format_option(config.channels()),
            Self::Instances => format_option(config.instances()),
            Self::Oversampling => format_option(config.oversampling()),
            Self::ProfileName => config.profile_name().to_string(),
            Self::ReliabilityChecks => joined_or_none(&config.reliability_checks()),
            Self::DurationMode => config.duration_mode().to_string(),
            Self::Iterations => format_option(config.iterations()),
            Self::DurationSeconds => format_option(config.duration_seconds()),
            Self::Repetitions => format_option(config.repetitions()),
            Self::WarmupIterations => format_option(config.warmup_iterations()),
            Self::MeasurementDetail => config.measurement_detail().to_string(),
            Self::EndToEnd => format_bool(config.end_to_end()),
            Self::TimingLevels => joined_or_none(&config.timing_levels()),
            Self::ValidationLevel => config.validation_level().to_string(),
            Self::AbsoluteTolerance => format_option(config.absolute_tolerance()),
            Self::RelativeTolerance => format_option(config.relative_tolerance()),
            Self::Workers => config.workers().to_string(),
            Self::Affinity => config.affinity().to_string(),
            Self::CpuIsolation => config.cpu_isolation().to_string(),
            Self::Isolation => config.isolation().to_string(),
            Self::PriorityOptIn => {
                if config.priority_opt_in() {
                    "explicitly accepted".to_string()
                } else {
                    "not accepted".to_string()
                }
            }
            Self::PriorityValue => format_option(config.priority()),
        }
    }

    /// Build the complete override batch that cycles this field.
    ///
    /// # Parameters
    /// - `config`: The currently retained config.
    /// - `forward`: `true` to advance, `false` to move backward.
    /// - `options`: Discovered identifiers available for processor selection.
    ///
    /// # Returns
    /// Dotted assignments in application order, or `None` when no meaningful
    /// alternative exists or an explicit confirmation is required. Companions
    /// keep built-in profile and generator selections runnable.
    pub fn cycle_assignments(
        self,
        config: &PortableConfig,
        forward: bool,
        options: &EditOptions,
    ) -> Option<Vec<String>> {
        let first = match self {
            Self::IntegrationId => {
                let mut ids = options.integration_ids.clone();
                ids.sort();
                ids.dedup();
                if ids.is_empty() {
                    return None;
                }
                let choices: Vec<&str> = ids.iter().map(String::as_str).collect();
                let next = cycle_str(config.integration_id(), &choices, forward)?;
                quote_assignment(self.path(), next)
            }
            Self::IntegrationVersion => return None,
            Self::WorkloadLevel => cycle_string(
                config.workload_level(),
                &["processor", "chain", "engine"],
                forward,
                self.path(),
            ),
            Self::Processors => {
                let mut ids = options.integration_ids.clone();
                ids.sort();
                ids.dedup();
                if ids.is_empty() {
                    return None;
                }
                let choices: Vec<&str> = ids.iter().map(String::as_str).collect();
                let mut processors = config.processors();
                let current_processor = processors.first().cloned().unwrap_or_default();
                let next = cycle_str(&current_processor, &choices, forward)?;
                if let Some(position) = processors.iter().position(|value| value == next) {
                    processors.remove(position);
                }
                processors.push(next.to_string());
                format!("{}={}", self.path(), json_string_array(&processors))
            }
            Self::GeneratorKind => {
                let kinds = ["sine", "multitone", "impulse"];
                let next = cycle_str(config.generator_kind(), &kinds, forward)?;
                format!("{}={}", self.path(), config.generator_with_kind(next))
            }
            Self::Amplitude => cycle_number(
                config.amplitude().unwrap_or(0.5),
                &[0.0, 0.25, 0.5, 0.75, 1.0],
                forward,
                self.path(),
            ),
            Self::FrequencyHz => cycle_number(
                config.frequency_hz().unwrap_or(1000.0),
                &[100.0, 500.0, 1000.0, 2000.0, 5000.0],
                forward,
                self.path(),
            ),
            Self::SampleRateHz => cycle_number(
                config.sample_rate_hz().unwrap_or(48000.0),
                &[44100.0, 48000.0, 96000.0, 192000.0],
                forward,
                self.path(),
            ),
            Self::BlockSize => cycle_integer(
                config.block_size().unwrap_or(128),
                &[32, 64, 128, 256, 512],
                forward,
                self.path(),
            ),
            Self::Channels => cycle_integer(
                config.channels().unwrap_or(2),
                &[1, 2, 4, 8],
                forward,
                self.path(),
            ),
            Self::Instances => cycle_integer(
                config.instances().unwrap_or(1),
                &[1, 2, 4, 8],
                forward,
                self.path(),
            ),
            Self::Oversampling => cycle_integer(
                config.oversampling().unwrap_or(1),
                &[1, 2, 4],
                forward,
                self.path(),
            ),
            Self::ProfileName => {
                let next = Self::next_profile_name(config, forward)?;
                quote_assignment(self.path(), next)
            }
            Self::ReliabilityChecks => {
                let current = config
                    .reliability_checks()
                    .first()
                    .cloned()
                    .unwrap_or_default();
                let next = cycle_str(&current, &["stability", "clipping", "thermal"], forward)?;
                format!("{}={}", self.path(), json_string_array(&[next.to_string()]))
            }
            Self::DurationMode => cycle_string(
                config.duration_mode(),
                &["iterations", "duration"],
                forward,
                self.path(),
            ),
            Self::Iterations => cycle_integer_step(
                config.iterations().unwrap_or(100),
                100,
                1,
                100_000,
                forward,
                self.path(),
            ),
            Self::DurationSeconds => cycle_number(
                config.duration_seconds().unwrap_or(5.0),
                &[5.0, 10.0, 20.0, 60.0],
                forward,
                self.path(),
            ),
            Self::Repetitions => cycle_integer(
                config.repetitions().unwrap_or(2),
                &[1, 2, 3, 5, 10],
                forward,
                self.path(),
            ),
            Self::WarmupIterations => cycle_integer(
                config.warmup_iterations().unwrap_or(0),
                &[0, 10, 50, 100],
                forward,
                self.path(),
            ),
            Self::MeasurementDetail => cycle_string(
                config.measurement_detail(),
                &["aggregate", "per_block"],
                forward,
                self.path(),
            ),
            Self::EndToEnd => format!("{}={}", self.path(), !config.end_to_end().unwrap_or(false)),
            Self::TimingLevels => {
                let levels = ["per_block", "whole_run"];
                let current = config
                    .timing_levels()
                    .first()
                    .cloned()
                    .unwrap_or_else(|| levels[0].to_string());
                let next = cycle_str(&current, &levels, forward)?;
                format!("{}={}", self.path(), json_string_array(&[next.to_string()]))
            }
            Self::ValidationLevel => cycle_string(
                config.validation_level(),
                &["Basic", "Standard", "Strict"],
                forward,
                self.path(),
            ),
            Self::AbsoluteTolerance => cycle_number(
                config.absolute_tolerance().unwrap_or(1e-6),
                &[1e-6, 1e-4, 1e-2],
                forward,
                self.path(),
            ),
            Self::RelativeTolerance => cycle_number(
                config.relative_tolerance().unwrap_or(1e-6),
                &[1e-6, 1e-4, 1e-2],
                forward,
                self.path(),
            ),
            Self::Workers => cycle_string(
                config.workers(),
                &["auto", "1", "2", "4", "8"],
                forward,
                self.path(),
            ),
            Self::Affinity => cycle_string(
                config.affinity(),
                &["auto", "physical", "logical"],
                forward,
                self.path(),
            ),
            Self::CpuIsolation => cycle_string(
                config.cpu_isolation(),
                &["off", "request"],
                forward,
                self.path(),
            ),
            Self::Isolation => cycle_string(
                config.isolation(),
                &["auto", "in_process", "out_of_process"],
                forward,
                self.path(),
            ),
            Self::PriorityOptIn => return None,
            Self::PriorityValue => {
                let current = config.priority().unwrap_or(0);
                let next = cycle_integer_value(current, &[0, 1, 5, 10, 15], forward)?;
                if next > 0 && !config.priority_opt_in() {
                    return None;
                }
                format!("{}={next}", self.path())
            }
        };
        let mut assignments = vec![first];
        if self == Self::ProfileName
            && let Some(name) = Self::profile_name_from_assignment(&assignments[0])
        {
            assignments.extend(Self::preset_assignments(name));
        }
        if self == Self::DurationMode
            && assignments[0].ends_with("=\"duration\"")
            && config.duration_seconds().unwrap_or(0.0) <= 0.0
        {
            assignments.push("execution.duration_seconds=5".to_string());
        }
        Some(assignments)
    }

    /// Build one override assignment that cycles this field.
    ///
    /// # Parameters
    /// - `config`: The currently retained config.
    /// - `forward`: `true` to advance, `false` to move backward.
    /// - `options`: Discovered identifiers available for processor selection.
    ///
    /// # Returns
    /// The first dotted assignment, or `None` when no alternative exists.
    pub fn cycle_assignment(
        self,
        config: &PortableConfig,
        forward: bool,
        options: &EditOptions,
    ) -> Option<String> {
        self.cycle_assignments(config, forward, options)?
            .into_iter()
            .next()
    }

    /// Return the next built-in profile name in the documented cycle.
    pub fn next_profile_name(config: &PortableConfig, forward: bool) -> Option<&'static str> {
        cycle_str(
            config.profile_name(),
            &["Quick", "Standard", "Comprehensive", "Custom"],
            forward,
        )
    }

    /// Return the profile name encoded in one `profile.name` assignment.
    fn profile_name_from_assignment(assignment: &str) -> Option<&str> {
        assignment
            .strip_prefix("profile.name=")
            .and_then(|value| value.strip_prefix('"'))
            .and_then(|value| value.strip_suffix('"'))
    }

    /// Return the complete override batch for a built-in profile preset.
    ///
    /// `Custom` keeps the user's existing settings. Named presets apply their
    /// documented duration, coverage, detail and validation defaults.
    pub fn preset_assignments(name: &str) -> Vec<String> {
        match name {
            "Quick" => vec![
                "execution.iterations=100".to_string(),
                "execution.duration_seconds=5".to_string(),
                "execution.repetitions=2".to_string(),
                "execution.warmup.iterations=0".to_string(),
                "measurement.detail=\"aggregate\"".to_string(),
                format!(
                    "measurement.timing_levels={}",
                    json_string_array(&["per_block".to_string(), "whole_run".to_string()])
                ),
                "validation.level=\"Basic\"".to_string(),
                format!(
                    "profile.reliability_checks={}",
                    json_string_array(&["stability".to_string()])
                ),
            ],
            "Standard" => vec![
                "execution.iterations=500".to_string(),
                "execution.duration_seconds=20".to_string(),
                "execution.repetitions=3".to_string(),
                "execution.warmup.iterations=10".to_string(),
                "measurement.detail=\"per_block\"".to_string(),
                format!(
                    "measurement.timing_levels={}",
                    json_string_array(&["per_block".to_string(), "whole_run".to_string()])
                ),
                "validation.level=\"Standard\"".to_string(),
                format!(
                    "profile.reliability_checks={}",
                    json_string_array(&["stability".to_string(), "clipping".to_string()])
                ),
            ],
            "Comprehensive" => vec![
                "execution.iterations=2000".to_string(),
                "execution.duration_seconds=60".to_string(),
                "execution.repetitions=5".to_string(),
                "execution.warmup.iterations=100".to_string(),
                "measurement.detail=\"per_block\"".to_string(),
                format!(
                    "measurement.timing_levels={}",
                    json_string_array(&["per_block".to_string(), "whole_run".to_string()])
                ),
                "validation.level=\"Strict\"".to_string(),
                format!(
                    "profile.reliability_checks={}",
                    json_string_array(&[
                        "stability".to_string(),
                        "clipping".to_string(),
                        "thermal".to_string(),
                    ])
                ),
            ],
            _ => Vec::new(),
        }
    }

    /// Validate typed text and build the matching override assignment.
    ///
    /// JSON literals are quoted by this helper so names containing spaces or
    /// punctuation cannot change the assignment's meaning.
    pub fn assignment_from_input(self, text: &str) -> Result<String, DataError> {
        let value = text.trim();
        if value.is_empty() {
            return Err(DataError::new(format!(
                "{} requires a non-empty value",
                self.label()
            )));
        }
        match self {
            Self::WorkloadLevel => validate_choice(self, value, &["processor", "chain", "engine"]),
            Self::IntegrationId | Self::IntegrationVersion | Self::ProfileName => {
                Ok(quote_assignment(self.path(), value))
            }
            Self::DurationMode => validate_choice(self, value, &["iterations", "duration"]),
            Self::Workers => {
                if value.eq_ignore_ascii_case("auto") {
                    return Ok(quote_assignment(self.path(), "auto"));
                }
                let parsed = value.parse::<u64>().map_err(|_| {
                    DataError::new(format!(
                        "{} must be 'auto' or a positive integer",
                        self.label()
                    ))
                })?;
                if parsed == 0 || parsed > u32::MAX as u64 {
                    return Err(DataError::new(format!(
                        "{} must be 'auto' or a positive 32-bit integer",
                        self.label()
                    )));
                }
                Ok(quote_assignment(self.path(), &parsed.to_string()))
            }
            Self::Affinity => validate_choice(self, value, &["auto", "physical", "logical"]),
            Self::CpuIsolation => validate_choice(self, value, &["off", "request"]),
            Self::Isolation => {
                validate_choice(self, value, &["auto", "in_process", "out_of_process"])
            }
            Self::MeasurementDetail => validate_choice(self, value, &["aggregate", "per_block"]),
            Self::ValidationLevel => validate_choice(self, value, &["Basic", "Standard", "Strict"]),
            Self::EndToEnd => match value.to_ascii_lowercase().as_str() {
                "true" => Ok(format!("{}=true", self.path())),
                "false" => Ok(format!("{}=false", self.path())),
                _ => Err(DataError::new(format!(
                    "{} must be true or false",
                    self.label()
                ))),
            },
            Self::PriorityOptIn => Err(DataError::new(
                "higher priority opt-in requires p and a second confirmation",
            )),
            Self::TimingLevels => {
                let mut levels = Vec::new();
                for token in value
                    .split(',')
                    .map(str::trim)
                    .filter(|token| !token.is_empty())
                {
                    let canonical = canonical_choice(token, &["per_block", "whole_run"])
                        .ok_or_else(|| {
                            DataError::new(format!(
                                "{} must be one of per_block, whole_run",
                                self.label()
                            ))
                        })?;
                    levels.push(canonical.to_string());
                }
                if levels.is_empty() {
                    return Err(DataError::new("timing levels require at least one token"));
                }
                Ok(format!("{}={}", self.path(), json_string_array(&levels)))
            }
            Self::Processors => {
                let members: Vec<String> = value
                    .split(',')
                    .map(str::trim)
                    .filter(|token| !token.is_empty())
                    .map(str::to_string)
                    .collect();
                if members.is_empty() {
                    return Err(DataError::new(format!(
                        "{} requires at least one token",
                        self.label()
                    )));
                }
                Ok(format!("{}={}", self.path(), json_string_array(&members)))
            }
            Self::ReliabilityChecks => {
                let mut members = Vec::new();
                for token in value
                    .split(',')
                    .map(str::trim)
                    .filter(|token| !token.is_empty())
                {
                    let canonical = canonical_choice(token, &["stability", "clipping", "thermal"])
                        .ok_or_else(|| {
                            DataError::new(format!(
                                "{} must be one of stability, clipping, thermal",
                                self.label()
                            ))
                        })?;
                    members.push(canonical.to_string());
                }
                if members.is_empty() {
                    return Err(DataError::new(format!(
                        "{} requires at least one token",
                        self.label()
                    )));
                }
                Ok(format!("{}={}", self.path(), json_string_array(&members)))
            }
            Self::GeneratorKind => {
                let canonical = canonical_choice(value, &["sine", "multitone", "impulse"])
                    .ok_or_else(|| {
                        DataError::new(format!(
                            "{} must be one of sine, multitone, impulse",
                            self.label()
                        ))
                    })?;
                Ok(format!(
                    "{}={}",
                    self.path(),
                    generator_object(canonical, 0.5)
                ))
            }
            Self::Amplitude | Self::AbsoluteTolerance | Self::RelativeTolerance => {
                nonnegative_number(self, value)
            }
            Self::FrequencyHz | Self::SampleRateHz | Self::DurationSeconds => {
                positive_number(self, value)
            }
            Self::BlockSize
            | Self::Channels
            | Self::Instances
            | Self::Oversampling
            | Self::Iterations
            | Self::Repetitions => integer_in_range(self, value, 1, u32::MAX as i64),
            Self::WarmupIterations => integer_in_range(self, value, 0, u32::MAX as i64),
            Self::PriorityValue => integer_in_range(self, value, 0, 15),
        }
    }

    /// Validate typed text and build the complete override batch for this field.
    ///
    /// Companion assignments keep built-in profile, generator and duration
    /// selections runnable. Priority changes retain the two-step confirmation:
    /// the opt-in field cannot be typed and a non-zero priority needs an accepted
    /// opt-in.
    ///
    /// # Parameters
    /// - `text`: Raw editor contents.
    /// - `config`: Currently retained config used for companion values.
    ///
    /// # Returns
    /// Dotted assignments in application order, or an error explaining the
    /// invalid input.
    pub fn assignment_batch_from_input(
        self,
        text: &str,
        config: &PortableConfig,
    ) -> Result<Vec<String>, DataError> {
        let value = text.trim();
        let first = self.assignment_from_input(value)?;
        if self == Self::PriorityValue {
            let requested = value.parse::<i64>().unwrap_or(0);
            if requested > 0 && !config.priority_opt_in() {
                return Err(DataError::new(
                    "higher priority requires p and a second confirmation",
                ));
            }
        }
        let mut assignments = vec![first];
        if self == Self::GeneratorKind {
            let canonical =
                canonical_choice(value, &["sine", "multitone", "impulse"]).ok_or_else(|| {
                    DataError::new(format!(
                        "{} must be one of sine, multitone, impulse",
                        self.label()
                    ))
                })?;
            assignments[0] = format!("{}={}", self.path(), config.generator_with_kind(canonical));
        }
        if self == Self::ProfileName
            && let Some(name) = Self::profile_name_from_assignment(&assignments[0])
        {
            assignments.extend(Self::preset_assignments(name));
        }
        if self == Self::DurationMode
            && value.eq_ignore_ascii_case("duration")
            && config.duration_seconds().unwrap_or(0.0) <= 0.0
        {
            assignments.push("execution.duration_seconds=5".to_string());
        }
        Ok(assignments)
    }
}

/// Return the canonical spelling of one case-insensitive choice token.
fn canonical_choice<'a>(value: &str, choices: &[&'a str]) -> Option<&'a str> {
    choices
        .iter()
        .copied()
        .find(|choice| choice.eq_ignore_ascii_case(value))
}

/// Validate one token and build a quoted string assignment.
fn validate_choice(
    field: EditableField,
    value: &str,
    choices: &[&str],
) -> Result<String, DataError> {
    let canonical = canonical_choice(value, choices).ok_or_else(|| {
        DataError::new(format!(
            "{} must be one of {}",
            field.label(),
            choices.join(", ")
        ))
    })?;
    Ok(quote_assignment(field.path(), canonical))
}

/// Validate one finite, non-negative real override value.
fn nonnegative_number(field: EditableField, value: &str) -> Result<String, DataError> {
    let parsed = value
        .parse::<f64>()
        .map_err(|_| DataError::new(format!("{} must be a number", field.label())))?;
    if !parsed.is_finite() || parsed < 0.0 {
        return Err(DataError::new(format!(
            "{} must be finite and non-negative",
            field.label()
        )));
    }
    Ok(format!("{}={parsed}", field.path()))
}

/// Validate one finite, strictly positive real override value.
fn positive_number(field: EditableField, value: &str) -> Result<String, DataError> {
    let parsed = value
        .parse::<f64>()
        .map_err(|_| DataError::new(format!("{} must be a number", field.label())))?;
    if !parsed.is_finite() || parsed <= 0.0 {
        return Err(DataError::new(format!(
            "{} must be finite and positive",
            field.label()
        )));
    }
    Ok(format!("{}={parsed}", field.path()))
}

/// Validate one integer within an inclusive range.
fn integer_in_range(
    field: EditableField,
    value: &str,
    minimum: i64,
    maximum: i64,
) -> Result<String, DataError> {
    let parsed = value
        .parse::<i64>()
        .map_err(|_| DataError::new(format!("{} must be an integer", field.label())))?;
    if parsed < minimum || parsed > maximum {
        return Err(DataError::new(format!(
            "{} must be between {minimum} and {maximum}",
            field.label()
        )));
    }
    Ok(format!("{}={parsed}", field.path()))
}

/// Build a quoted string assignment for a dotted config path.
fn quote_assignment(path: &str, value: &str) -> String {
    format!("{}={}", path, json_quote(value))
}

/// Quote one string as a JSON literal with deterministic escapes.
///
/// All control characters are emitted as `\uXXXX` escapes so the result is a
/// valid JSON string even for values entered through the text editor.
fn json_quote(value: &str) -> String {
    let mut output = String::from("\"");
    for character in value.chars() {
        match character {
            '"' => output.push_str("\\\""),
            '\\' => output.push_str("\\\\"),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            other if other.is_control() => {
                output.push_str(&format!("\\u{:04x}", u32::from(other)));
            }
            other => output.push(other),
        }
    }
    output.push('"');
    output
}

/// Build one JSON array literal from string members.
fn json_string_array(values: &[String]) -> String {
    format!(
        "[{}]",
        values
            .iter()
            .map(|value| json_quote(value))
            .collect::<Vec<_>>()
            .join(",")
    )
}

/// Build one complete generator object for a built-in generator kind.
fn generator_object(kind: &str, amplitude: f64) -> String {
    match kind {
        "multitone" => format!(
            concat!(
                "{{\"kind\":\"multitone\",\"amplitude\":{amplitude},",
                "\"tones\":[{{\"frequency_hz\":1000.0,\"phase_rad\":0.0,\"weight\":1.0}}]}}"
            ),
            amplitude = amplitude
        ),
        "impulse" => format!("{{\"kind\":\"impulse\",\"amplitude\":{amplitude},\"position\":0}}"),
        _ => format!(
            concat!(
                "{{\"kind\":\"sine\",\"amplitude\":{amplitude},",
                "\"frequency_hz\":1000.0,\"phase_rad\":0.0}}"
            ),
            amplitude = amplitude
        ),
    }
}

/// Format one optional value for display with an explicit placeholder.
fn format_option<T: std::fmt::Display>(value: Option<T>) -> String {
    value
        .map(|value| value.to_string())
        .unwrap_or_else(|| "(unset)".to_string())
}

/// Format one optional boolean for display without hiding a missing value.
fn format_bool(value: Option<bool>) -> String {
    match value {
        Some(true) => "true".to_string(),
        Some(false) => "false".to_string(),
        None => "(unset)".to_string(),
    }
}

/// Join string values for display with an explicit empty placeholder.
fn joined_or_none(values: &[String]) -> String {
    if values.is_empty() {
        "(none)".to_string()
    } else {
        values.join(", ")
    }
}

/// Cycle one string token through a fixed choice list.
fn cycle_string(current: &str, choices: &[&str], forward: bool, path: &str) -> String {
    let next = cycle_str(current, choices, forward).unwrap_or(choices[0]);
    quote_assignment(path, next)
}

/// Cycle one numeric token through a fixed choice list.
fn cycle_number(current: f64, choices: &[f64], forward: bool, path: &str) -> String {
    let position = choices
        .iter()
        .position(|value| (*value - current).abs() < f64::EPSILON)
        .unwrap_or(0);
    let offset = if forward { 1 } else { choices.len() - 1 };
    format!("{}={}", path, choices[(position + offset) % choices.len()])
}

/// Cycle one integer token through a fixed choice list.
fn cycle_integer(current: i64, choices: &[i64], forward: bool, path: &str) -> String {
    let values: Vec<String> = choices.iter().map(|value| value.to_string()).collect();
    cycle_string(
        &current.to_string(),
        &values.iter().map(String::as_str).collect::<Vec<_>>(),
        forward,
        path,
    )
}

/// Return the next or previous integer token in a non-empty choice list.
fn cycle_integer_value(current: i64, choices: &[i64], forward: bool) -> Option<i64> {
    let tokens: Vec<String> = choices.iter().map(|value| value.to_string()).collect();
    let references: Vec<&str> = tokens.iter().map(String::as_str).collect();
    cycle_str(&current.to_string(), &references, forward)?
        .parse()
        .ok()
}

/// Cycle one bounded integer using a fixed step.
fn cycle_integer_step(
    current: i64,
    step: i64,
    minimum: i64,
    maximum: i64,
    forward: bool,
    path: &str,
) -> String {
    let next = if forward {
        (current + step).clamp(minimum, maximum)
    } else {
        (current - step).clamp(minimum, maximum)
    };
    format!("{path}={next}")
}

/// Return the next or previous token in a non-empty choice list.
fn cycle_str<'a>(current: &str, choices: &'a [&'a str], forward: bool) -> Option<&'a str> {
    if choices.is_empty() {
        return None;
    }
    let position = choices
        .iter()
        .position(|value| *value == current)
        .unwrap_or(if forward { choices.len() - 1 } else { 0 });
    let offset = if forward { 1 } else { choices.len() - 1 };
    Some(choices[(position + offset) % choices.len()])
}

/// Unit tests for config defaults and override construction.
#[cfg(test)]
mod tests {
    use super::{
        DEFAULT_CONFIG_JSON, EditOptions, EditableField, generator_object, json_quote,
        json_string_array,
    };
    use crate::data::PortableConfig;

    /// Verify the default document is a decodable v1 config with explicit defaults.
    #[test]
    fn default_config_decodes_with_explicit_priority_defaults() {
        let config =
            PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("default config decodes");
        assert_eq!(config.config_version(), "1.0.0");
        assert_eq!(config.workload_level(), "processor");
        assert_eq!(config.profile_name(), "Quick");
        assert!(!config.priority_opt_in());
        assert_eq!(config.priority(), Some(0));
        assert_eq!(config.cpu_isolation(), "off");
        assert_eq!(config.absolute_tolerance(), Some(0.000001));
        assert_eq!(config.warmup_iterations(), Some(0));
    }

    /// Verify each editing screen exposes a stable non-empty field list.
    #[test]
    fn editing_screens_expose_stable_field_lists() {
        let screens = [
            crate::app::Screen::Workload,
            crate::app::Screen::Configuration,
            crate::app::Screen::Measurement,
            crate::app::Screen::Scheduling,
            crate::app::Screen::Profiles,
        ];
        for screen in screens {
            let fields = EditableField::fields_for(screen);
            assert!(!fields.is_empty(), "screen {screen:?} has editable fields");
            let mut paths = Vec::new();
            for field in fields {
                assert!(!field.label().is_empty());
                assert!(!field.path().is_empty());
                paths.push(field.path());
            }
            let unique: std::collections::HashSet<_> = paths.iter().collect();
            assert_eq!(
                unique.len(),
                paths.len(),
                "screen {screen:?} has unique fields"
            );
        }
    }

    /// Verify forward and backward cycling produces documented assignments.
    #[test]
    fn field_cycling_produces_documented_assignments() {
        let config =
            PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("default config decodes");
        let options = EditOptions {
            integration_ids: vec!["app-a".to_string(), "app-b".to_string()],
        };
        assert_eq!(
            EditableField::WorkloadLevel.cycle_assignment(&config, true, &options),
            Some("workload.level=\"chain\"".to_string())
        );
        assert_eq!(
            EditableField::WorkloadLevel.cycle_assignment(&config, false, &options),
            Some("workload.level=\"engine\"".to_string())
        );
        assert_eq!(
            EditableField::Processors.cycle_assignment(&config, true, &options),
            Some("workload.processors=[\"eq\",\"app-a\"]".to_string())
        );
        assert_eq!(
            EditableField::Processors.cycle_assignment(&config, false, &options),
            Some("workload.processors=[\"eq\",\"app-b\"]".to_string())
        );
        assert_eq!(
            EditableField::EndToEnd.cycle_assignment(&config, true, &options),
            Some("measurement.end_to_end=true".to_string())
        );
        assert_eq!(
            EditableField::PriorityOptIn.cycle_assignment(&config, true, &options),
            None
        );
        assert_eq!(
            EditableField::PriorityValue.cycle_assignment(&config, true, &options),
            None
        );
        let duration = EditableField::DurationMode
            .cycle_assignments(&config, true, &options)
            .expect("duration mode has a batch");
        assert_eq!(duration[0], "execution.duration_mode=\"duration\"");
        assert_eq!(duration[1], "execution.duration_seconds=5");
        let preset = EditableField::ProfileName
            .cycle_assignments(&config, true, &options)
            .expect("profile name has a batch");
        assert_eq!(preset[0], "profile.name=\"Standard\"");
        assert!(
            preset
                .iter()
                .any(|value| value.starts_with("measurement.detail="))
        );
    }

    /// Verify typed input validation rejects malformed values and quotes strings.
    #[test]
    fn typed_input_validation_rejects_malformed_values() {
        assert_eq!(
            EditableField::ProfileName
                .assignment_from_input("My Profile")
                .expect("name is valid"),
            "profile.name=\"My Profile\""
        );
        assert!(
            EditableField::PriorityOptIn
                .assignment_from_input("maybe")
                .is_err()
        );
        assert!(
            EditableField::Iterations
                .assignment_from_input("0")
                .is_err()
        );
        assert!(
            EditableField::SampleRateHz
                .assignment_from_input("not-a-number")
                .is_err()
        );
        assert!(
            EditableField::Isolation
                .assignment_from_input("sandboxed")
                .is_err()
        );
        assert_eq!(
            EditableField::DurationMode
                .assignment_from_input("duration")
                .expect("duration mode is valid"),
            "execution.duration_mode=\"duration\""
        );
        assert!(
            EditableField::DurationMode
                .assignment_from_input("auto")
                .is_err()
        );
        assert_eq!(
            EditableField::TimingLevels
                .assignment_from_input("whole_run")
                .expect("timing level is valid"),
            "measurement.timing_levels=[\"whole_run\"]"
        );
        assert_eq!(
            EditableField::Processors
                .assignment_from_input("eq")
                .expect("processor name is valid"),
            "workload.processors=[\"eq\"]"
        );
        assert!(
            EditableField::GeneratorKind
                .assignment_from_input("sine")
                .expect("generator kind is valid")
                .contains("\"kind\":\"sine\"")
        );
        assert_eq!(
            EditableField::PriorityValue
                .assignment_from_input("0")
                .expect("priority can be cleared"),
            "parallelism.priority=0"
        );
        assert!(EditableField::Workers.assignment_from_input("0").is_err());
        assert!(
            EditableField::FrequencyHz
                .assignment_from_input("0")
                .is_err()
        );
        assert!(
            EditableField::Workers
                .assignment_from_input("4294967296")
                .is_err()
        );
        assert!(
            EditableField::Iterations
                .assignment_from_input("4294967296")
                .is_err()
        );
    }

    /// Verify typed batches preserve generator parameters and enforce priority consent.
    #[test]
    fn typed_batches_preserve_parameters_and_enforce_priority_consent() {
        let config =
            PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("default config decodes");
        let batch = EditableField::GeneratorKind
            .assignment_batch_from_input("multitone", &config)
            .expect("generator kind is valid");
        assert_eq!(batch.len(), 1);
        assert!(batch[0].contains("\"amplitude\":0.5"));
        assert!(batch[0].contains("\"tones\":[{"));
        assert!(
            EditableField::PriorityValue
                .assignment_batch_from_input("5", &config)
                .is_err()
        );
        let accepted =
            DEFAULT_CONFIG_JSON.replace("\"priority_opt_in\":false", "\"priority_opt_in\":true");
        let accepted = PortableConfig::from_json(&accepted).expect("accepted config decodes");
        assert_eq!(
            EditableField::PriorityValue
                .assignment_batch_from_input("5", &accepted)
                .expect("accepted priority is valid"),
            vec!["parallelism.priority=5".to_string()]
        );
    }

    /// Verify JSON literal helpers escape strings and build complete generators.
    #[test]
    fn json_literal_helpers_escape_strings_and_build_generators() {
        assert_eq!(json_quote("a\"b"), "\"a\\\"b\"");
        assert_eq!(
            json_quote("a\u{0}b\u{8}c\u{b}d\u{c}e\u{1f}f"),
            "\"a\\u0000b\\u0008c\\u000bd\\u000ce\\u001ff\""
        );
        assert_eq!(json_string_array(&["eq".to_string()]), "[\"eq\"]");
        assert!(generator_object("multitone", 0.5).contains("\"tones\":["));
        assert!(generator_object("impulse", 0.5).contains("\"position\":0"));
        assert!(generator_object("sine", 0.5).contains("\"frequency_hz\""));
    }
}
