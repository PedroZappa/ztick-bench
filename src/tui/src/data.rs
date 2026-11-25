// SPDX-License-Identifier: MIT
//! Typed, read-only views over front-end JSON envelopes.
//!
//! The front end emits the versioned CLI envelopes defined in
//! [RESULTS §12](../../../docs/RESULTS_FORMAT.md). This module interprets only those
//! documented fields; it never computes metrics, changes configuration or
//! redefines a result schema.

use std::fmt;

use crate::json::{self, Json};

/// An error while interpreting a front-end envelope.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DataError {
    /// The owned interpretation error detail.
    message: String,
}

/// Construct and inspect envelope interpretation errors.
impl DataError {
    /// Construct an envelope interpretation error.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }

    /// Return the human-readable error detail.
    pub fn message(&self) -> &str {
        &self.message
    }
}

/// Format an envelope interpretation error for display.
impl fmt::Display for DataError {
    /// Write the error or value into the supplied formatter.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

/// Mark envelope interpretation errors as standard errors.
impl std::error::Error for DataError {}

/// The fixed CLI envelope wrapper around command-specific data.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Envelope {
    /// The command token carried by the envelope.
    command: String,
    /// The command status token carried by the envelope.
    status: String,
    /// The copied command error text.
    error: String,
    /// The command-specific JSON payload.
    data: Json,
}

/// Decode and validate one CLI envelope.
impl Envelope {
    /// Decode and validate one envelope for the expected command.
    fn parse(text: &str, expected_command: &str) -> Result<Self, DataError> {
        let root = json::parse(text).map_err(|error| {
            DataError::new(format!(
                "invalid {} envelope at byte {}: {}",
                expected_command,
                error.offset(),
                error.message()
            ))
        })?;
        let command = required_string(&root, "command", "envelope")?;
        if command != expected_command {
            return Err(DataError::new(format!(
                "expected '{expected_command}' envelope, received '{command}'"
            )));
        }
        let version = root
            .get("schema_version")
            .and_then(Json::as_i64)
            .ok_or_else(|| {
                DataError::new("envelope schema_version is missing or not an integer")
            })?;
        if version != 1 {
            return Err(DataError::new(format!(
                "unsupported envelope schema_version {version}"
            )));
        }
        let status = required_string(&root, "status", "envelope")?;
        let error = required_string(&root, "error", "envelope")?;
        let data = root.get("data").cloned().unwrap_or(Json::Null);
        Ok(Self {
            command,
            status,
            error,
            data,
        })
    }

    /// Return the command-specific data payload when it is an object.
    ///
    /// A failed command still carries its documented data object, so callers can
    /// render the availability records it contains instead of hiding them.
    fn data(&self) -> Result<&Json, DataError> {
        if !matches!(self.data, Json::Object(_)) {
            return Err(DataError::new(format!(
                "{} reported no object data payload",
                self.command
            )));
        }
        Ok(&self.data)
    }

    /// Return the command status token recorded in the envelope.
    fn status(&self) -> &str {
        &self.status
    }

    /// Return the command error text recorded in the envelope.
    fn error(&self) -> &str {
        &self.error
    }

    /// Build a visible note when the command reported a failure.
    fn failure_note(&self) -> Option<String> {
        if self.status() == "success" {
            return None;
        }
        let detail = if self.error().is_empty() {
            "no error detail recorded"
        } else {
            self.error()
        };
        Some(format!(
            "{} reported {}: {}",
            self.command,
            self.status(),
            detail
        ))
    }
}

/// Read one required string member.
fn required_string(value: &Json, key: &str, context: &str) -> Result<String, DataError> {
    value
        .get(key)
        .and_then(Json::as_str)
        .map(str::to_string)
        .ok_or_else(|| {
            DataError::new(format!(
                "{context} member '{key}' is missing or not a string"
            ))
        })
}

/// Read one string member, using an empty string when it is absent.
fn optional_string(value: &Json, key: &str, context: &str) -> Result<String, DataError> {
    match value.get(key) {
        None | Some(Json::Null) => Ok(String::new()),
        Some(Json::String(text)) => Ok(text.clone()),
        Some(_) => Err(DataError::new(format!(
            "{context} member '{key}' is not a string"
        ))),
    }
}

/// Read one required array member.
fn required_array<'a>(value: &'a Json, key: &str, context: &str) -> Result<&'a [Json], DataError> {
    value.get(key).and_then(Json::as_array).ok_or_else(|| {
        DataError::new(format!(
            "{context} member '{key}' is missing or not an array"
        ))
    })
}

/// Availability state carried by one metadata field.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AvailabilityStatus {
    /// The value was supported and successfully read.
    Value,
    /// The value is supported but could not be read now.
    Unavailable,
    /// The capability is not implemented for this platform or mode.
    Unsupported,
    /// The field is meaningless for the selected benchmark mode.
    NotApplicable,
    /// The envelope carried an unrecognised status token.
    Unknown(String),
}

/// Map serialized tokens to availability states.
impl AvailabilityStatus {
    /// Map one serialized availability token to a display state.
    pub fn from_token(token: &str) -> Self {
        match token {
            "value" => Self::Value,
            "unavailable" => Self::Unavailable,
            "unsupported" => Self::Unsupported,
            "not_applicable" => Self::NotApplicable,
            other => Self::Unknown(other.to_string()),
        }
    }

    /// Return the stable token rendered for this state.
    pub fn label(&self) -> &str {
        match self {
            Self::Value => "value",
            Self::Unavailable => "unavailable",
            Self::Unsupported => "unsupported",
            Self::NotApplicable => "not_applicable",
            Self::Unknown(token) => token,
        }
    }

    /// Return true only when this state carries a readable value.
    pub fn has_value(&self) -> bool {
        matches!(self, Self::Value)
    }
}

/// One availability-aware field from the system inventory.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AvailabilityField {
    /// The stable machine field name.
    name: String,
    /// The decoded availability state.
    status: AvailabilityStatus,
    /// The recorded value, empty for non-value states.
    value: String,
    /// The recorded reason for a non-value state.
    reason: String,
}

/// Decode and render availability-aware fields.
impl AvailabilityField {
    /// Decode one `{status, value, reason}` field record.
    fn from_json(name: &str, value: &Json) -> Result<Self, DataError> {
        let context = format!("field '{name}'");
        let status = required_string(value, "status", &context)?;
        Ok(Self {
            name: name.to_string(),
            status: AvailabilityStatus::from_token(&status),
            value: required_string(value, "value", &context)?,
            reason: required_string(value, "reason", &context)?,
        })
    }

    /// Return the stable machine field name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the availability state.
    pub fn status(&self) -> &AvailabilityStatus {
        &self.status
    }

    /// Return the recorded value, empty for non-value states.
    pub fn value(&self) -> &str {
        &self.value
    }

    /// Return the reason recorded for non-value states.
    pub fn reason(&self) -> &str {
        &self.reason
    }

    /// Return the most informative text for display without hiding a state.
    pub fn display_value(&self) -> String {
        if self.status.has_value() && !self.value.is_empty() {
            self.value.clone()
        } else if self.reason.is_empty() {
            "(no reason recorded)".to_string()
        } else {
            self.reason.clone()
        }
    }
}

/// The complete system inventory in envelope order.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SystemInventory {
    /// The system fields in envelope order.
    fields: Vec<AvailabilityField>,
}

/// Decode the system inventory envelope.
impl SystemInventory {
    /// Decode the `system` command envelope.
    fn from_json(text: &str) -> Result<Self, DataError> {
        let envelope = Envelope::parse(text, "system")?;
        let data = envelope.data()?;
        let fields = data
            .get("fields")
            .and_then(Json::as_object)
            .ok_or_else(|| {
                DataError::new("system data member 'fields' is missing or not an object")
            })?;
        let mut decoded = Vec::with_capacity(fields.len());
        for (name, value) in fields {
            decoded.push(AvailabilityField::from_json(name, value)?);
        }
        Ok(Self { fields: decoded })
    }

    /// Return all inventory fields in source order.
    pub fn fields(&self) -> &[AvailabilityField] {
        &self.fields
    }

    /// Return one inventory field by stable name.
    pub fn field(&self, name: &str) -> Option<&AvailabilityField> {
        self.fields.iter().find(|field| field.name == name)
    }
}

/// One built-in benchmark profile.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Profile {
    /// The stable profile name.
    name: String,
    /// The human-readable profile description.
    description: String,
}

/// Decode and expose one built-in profile.
impl Profile {
    /// Decode one profile record.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            name: required_string(value, "name", "profile")?,
            description: required_string(value, "description", "profile")?,
        })
    }

    /// Return the stable profile name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the profile description.
    pub fn description(&self) -> &str {
        &self.description
    }
}

/// The built-in profile list from the `list` command.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ProfileList {
    /// The built-in profiles in envelope order.
    profiles: Vec<Profile>,
}

/// Decode the profile list envelope.
impl ProfileList {
    /// Decode the `list` command envelope.
    fn from_json(text: &str) -> Result<Self, DataError> {
        let envelope = Envelope::parse(text, "list")?;
        let data = envelope.data()?;
        let mut profiles = Vec::new();
        for value in required_array(data, "profiles", "list data")? {
            profiles.push(Profile::from_json(value)?);
        }
        Ok(Self { profiles })
    }

    /// Return all profiles in source order.
    pub fn profiles(&self) -> &[Profile] {
        &self.profiles
    }
}

/// One inspected plugin directory status.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginDirectory {
    /// The inspected plugin directory path.
    path: String,
    /// The directory discovery status token.
    status: String,
    /// The reason for a non-available directory.
    reason: String,
}

/// Decode and expose one plugin directory status.
impl PluginDirectory {
    /// Decode one directory status record.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            path: required_string(value, "path", "plugin directory")?,
            status: required_string(value, "status", "plugin directory")?,
            reason: required_string(value, "reason", "plugin directory")?,
        })
    }

    /// Return the directory path.
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Return the discovery status token.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the reason attached to a non-success status.
    pub fn reason(&self) -> &str {
        &self.reason
    }
}

/// One identity recorded in the trusted-plugin list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TrustEntry {
    /// The stable trusted plugin identifier.
    identifier: String,
    /// The trusted plugin display name.
    name: String,
    /// The trusted adapter version.
    version: String,
    /// The declared SDK version.
    sdk_version: String,
    /// The declared capability mask.
    capabilities: String,
}

/// Decode and expose one trusted identity.
impl TrustEntry {
    /// Decode one trusted identity record.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            identifier: required_string(value, "identifier", "trust entry")?,
            name: required_string(value, "name", "trust entry")?,
            version: required_string(value, "version", "trust entry")?,
            sdk_version: required_string(value, "sdk_version", "trust entry")?,
            capabilities: required_string(value, "capabilities", "trust entry")?,
        })
    }

    /// Return the stable plugin identifier.
    pub fn identifier(&self) -> &str {
        &self.identifier
    }

    /// Return the display name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the adapter version.
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
}

/// The trust-list state shared by plugin discovery and inspection.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TrustState {
    /// The trust-list read status token.
    status: String,
    /// The reason the trust list could not be read.
    reason: String,
    /// The trusted identities in envelope order.
    entries: Vec<TrustEntry>,
    /// The per-plugin trust state from an inspection envelope.
    state: String,
}

/// Decode and query the trusted-plugin list.
impl TrustState {
    /// Decode one trust object.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        let mut entries = Vec::new();
        for entry in required_array(value, "entries", "trust object")? {
            entries.push(TrustEntry::from_json(entry)?);
        }
        Ok(Self {
            status: required_string(value, "status", "trust object")?,
            reason: required_string(value, "reason", "trust object")?,
            entries,
            state: optional_string(value, "state", "trust object")?,
        })
    }

    /// Return the trust-list read status.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the reason when the trust list could not be read.
    pub fn reason(&self) -> &str {
        &self.reason
    }

    /// Return every trusted identity.
    pub fn entries(&self) -> &[TrustEntry] {
        &self.entries
    }

    /// Return the per-plugin trust state carried by an inspection envelope.
    pub fn state(&self) -> &str {
        &self.state
    }

    /// Return true only when the trust list was read successfully.
    pub fn is_available(&self) -> bool {
        self.status == "available"
    }

    /// Return the trust state for one identifier without hiding read failures.
    pub fn state_for(&self, identifier: &str) -> &str {
        if !self.is_available() {
            return "unknown";
        }
        if self
            .entries
            .iter()
            .any(|entry| entry.identifier == identifier)
        {
            "trusted"
        } else {
            "untrusted"
        }
    }
}

/// One discovered plugin candidate.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginCandidate {
    /// The discovered plugin descriptor path.
    path: String,
    /// The discovery source token.
    source: String,
    /// The metadata inspection status token.
    status: String,
    /// The reason metadata inspection failed.
    reason: String,
    /// The stable plugin identifier, empty when inspection failed.
    identifier: String,
    /// The plugin display name, empty when inspection failed.
    name: String,
    /// The adapter version, empty when inspection failed.
    version: String,
    /// The declared SDK version, empty when inspection failed.
    sdk_version: String,
    /// The declared capability mask, empty when inspection failed.
    capabilities: String,
    /// The per-plugin trust token.
    trust: String,
}

/// Decode and expose one discovered plugin candidate.
impl PluginCandidate {
    /// Decode one plugin candidate record.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            path: required_string(value, "path", "plugin candidate")?,
            source: required_string(value, "source", "plugin candidate")?,
            status: required_string(value, "status", "plugin candidate")?,
            reason: required_string(value, "reason", "plugin candidate")?,
            identifier: optional_string(value, "identifier", "plugin candidate")?,
            name: optional_string(value, "name", "plugin candidate")?,
            version: optional_string(value, "version", "plugin candidate")?,
            sdk_version: optional_string(value, "sdk_version", "plugin candidate")?,
            capabilities: optional_string(value, "capabilities", "plugin candidate")?,
            trust: optional_string(value, "trust", "plugin candidate")?,
        })
    }

    /// Return the candidate path.
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Return the discovery source token.
    pub fn source(&self) -> &str {
        &self.source
    }

    /// Return the inspection status token.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the reason attached to a failed inspection.
    pub fn reason(&self) -> &str {
        &self.reason
    }

    /// Return the stable plugin identifier, empty when inspection failed.
    pub fn identifier(&self) -> &str {
        &self.identifier
    }

    /// Return the display name, empty when inspection failed.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the adapter version, empty when inspection failed.
    pub fn version(&self) -> &str {
        &self.version
    }

    /// Return the declared SDK version, empty when inspection failed.
    pub fn sdk_version(&self) -> &str {
        &self.sdk_version
    }

    /// Return the declared capability mask, empty when inspection failed.
    pub fn capabilities(&self) -> &str {
        &self.capabilities
    }

    /// Return the per-plugin trust token.
    pub fn trust(&self) -> &str {
        &self.trust
    }

    /// Return true when metadata-only inspection succeeded.
    pub fn has_metadata(&self) -> bool {
        self.status == "success" && !self.identifier.is_empty()
    }
}

/// The discovered plugin list from the `plugin list` command.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PluginList {
    /// The inspected plugin directories in envelope order.
    directories: Vec<PluginDirectory>,
    /// The trust-list state.
    trust: TrustState,
    /// The discovered plugin candidates in envelope order.
    plugins: Vec<PluginCandidate>,
}

/// Decode the plugin list envelope.
impl PluginList {
    /// Decode the `plugin list` command envelope.
    fn from_json(text: &str) -> Result<Self, DataError> {
        let envelope = Envelope::parse(text, "plugin list")?;
        let data = envelope.data()?;
        let mut directories = Vec::new();
        for value in required_array(data, "directories", "plugin list data")? {
            directories.push(PluginDirectory::from_json(value)?);
        }
        let trust = data
            .get("trust")
            .ok_or_else(|| DataError::new("plugin list data member 'trust' is missing"))?;
        let trust = TrustState::from_json(trust)?;
        let mut plugins = Vec::new();
        for value in required_array(data, "plugins", "plugin list data")? {
            plugins.push(PluginCandidate::from_json(value)?);
        }
        Ok(Self {
            directories,
            trust,
            plugins,
        })
    }

    /// Return every inspected plugin directory.
    pub fn directories(&self) -> &[PluginDirectory] {
        &self.directories
    }

    /// Return the trust-list state.
    pub fn trust(&self) -> &TrustState {
        &self.trust
    }

    /// Return every discovered plugin candidate.
    pub fn plugins(&self) -> &[PluginCandidate] {
        &self.plugins
    }
}

/// SDK compatibility reported by metadata-only inspection.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Compatibility {
    /// The SDK compatibility status token.
    status: String,
    /// The compatibility explanation.
    reason: String,
}

/// Decode and expose one compatibility result.
impl Compatibility {
    /// Decode one compatibility object.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            status: required_string(value, "status", "compatibility")?,
            reason: required_string(value, "reason", "compatibility")?,
        })
    }

    /// Return the compatibility status token.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the compatibility reason.
    pub fn reason(&self) -> &str {
        &self.reason
    }
}

/// Detailed metadata for one plugin from the `plugin inspect` command.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginInspection {
    /// The inspected plugin path.
    path: String,
    /// The inspection status token.
    status: String,
    /// The inspection explanation.
    reason: String,
    /// The stable plugin identifier.
    identifier: String,
    /// The plugin display name.
    name: String,
    /// The adapter version.
    version: String,
    /// The declared SDK version.
    sdk_version: String,
    /// The declared capability mask.
    capabilities: String,
    /// The free-form metadata summary.
    metadata: String,
    /// The core compatibility result.
    compatibility: Compatibility,
    /// The trust-list state.
    trust: TrustState,
}

/// Decode and expose one plugin inspection envelope.
impl PluginInspection {
    /// Decode the `plugin inspect` command envelope.
    pub fn from_json(text: &str) -> Result<Self, DataError> {
        let envelope = Envelope::parse(text, "plugin inspect")?;
        let data = envelope.data()?;
        let metadata = data
            .get("metadata")
            .ok_or_else(|| DataError::new("plugin inspect data member 'metadata' is missing"))?;
        let compatibility = data.get("compatibility").ok_or_else(|| {
            DataError::new("plugin inspect data member 'compatibility' is missing")
        })?;
        let trust = data
            .get("trust")
            .ok_or_else(|| DataError::new("plugin inspect data member 'trust' is missing"))?;
        Ok(Self {
            path: required_string(data, "path", "plugin inspection")?,
            status: required_string(data, "status", "plugin inspection")?,
            reason: required_string(data, "reason", "plugin inspection")?,
            identifier: optional_string(metadata, "identifier", "plugin metadata")?,
            name: optional_string(metadata, "name", "plugin metadata")?,
            version: optional_string(metadata, "version", "plugin metadata")?,
            sdk_version: optional_string(metadata, "sdk_version", "plugin metadata")?,
            capabilities: optional_string(metadata, "capabilities", "plugin metadata")?,
            metadata: optional_string(metadata, "metadata", "plugin metadata")?,
            compatibility: Compatibility::from_json(compatibility)?,
            trust: TrustState::from_json(trust)?,
        })
    }

    /// Return the inspected path.
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Return the inspection status token.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the inspection reason.
    pub fn reason(&self) -> &str {
        &self.reason
    }

    /// Return the stable plugin identifier.
    pub fn identifier(&self) -> &str {
        &self.identifier
    }

    /// Return the display name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the adapter version.
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

    /// Return the free-form metadata summary.
    pub fn metadata(&self) -> &str {
        &self.metadata
    }

    /// Return the core compatibility result.
    pub fn compatibility(&self) -> &Compatibility {
        &self.compatibility
    }

    /// Return the trust-list state.
    pub fn trust(&self) -> &TrustState {
        &self.trust
    }
}

/// All read-only dashboard inputs copied from the front end.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct DashboardData {
    /// The decoded system inventory.
    system: SystemInventory,
    /// The decoded built-in profile list.
    profiles: ProfileList,
    /// The decoded plugin list.
    plugins: PluginList,
    /// The combined envelope interpretation errors.
    load_error: String,
}

/// Combine the read-only dashboard envelopes.
impl DashboardData {
    /// Build dashboard data from three independent command envelopes.
    ///
    /// A malformed or failed section is reported in [`Self::load_error`] while the
    /// other sections remain available; no field is silently dropped.
    pub fn from_json(system: &str, list: &str, plugins: &str) -> Self {
        let mut load_error = String::new();
        record_status_note(&mut load_error, "system", system);
        record_status_note(&mut load_error, "list", list);
        record_status_note(&mut load_error, "plugin list", plugins);
        let system = match SystemInventory::from_json(system) {
            Ok(value) => value,
            Err(error) => {
                record_error(&mut load_error, "system", error);
                SystemInventory::default()
            }
        };
        let profiles = match ProfileList::from_json(list) {
            Ok(value) => value,
            Err(error) => {
                record_error(&mut load_error, "list", error);
                ProfileList::default()
            }
        };
        let plugins = match PluginList::from_json(plugins) {
            Ok(value) => value,
            Err(error) => {
                record_error(&mut load_error, "plugin list", error);
                PluginList::default()
            }
        };
        Self {
            system,
            profiles,
            plugins,
            load_error,
        }
    }

    /// Append one labelled boundary error without discarding decoded sections.
    pub fn record_load_error(&mut self, section: &str, error: impl fmt::Display) {
        record_error(
            &mut self.load_error,
            section,
            DataError::new(error.to_string()),
        );
    }

    /// Return combined interpretation errors, empty when every envelope decoded.
    pub fn load_error(&self) -> &str {
        &self.load_error
    }

    /// Return the system inventory.
    pub fn system(&self) -> &SystemInventory {
        &self.system
    }

    /// Return the built-in profile list.
    pub fn profiles(&self) -> &ProfileList {
        &self.profiles
    }

    /// Return the discovered plugin list.
    pub fn plugins(&self) -> &PluginList {
        &self.plugins
    }
}

/// Read one optional array member, using an empty slice when absent.
fn optional_array<'a>(value: &'a Json, key: &str, context: &str) -> Result<&'a [Json], DataError> {
    match value.get(key) {
        None | Some(Json::Null) => Ok(&[]),
        Some(Json::Array(items)) => Ok(items),
        Some(_) => Err(DataError::new(format!(
            "{context} member '{key}' is not an array"
        ))),
    }
}

/// Return the original text of one optional number for lossless display.
fn number_text(value: Option<&Json>) -> String {
    match value {
        Some(Json::Number(text)) | Some(Json::String(text)) => text.clone(),
        _ => String::new(),
    }
}

/// Append one semicolon-separated explanation to a status reason.
fn append_reason(reason: &mut String, detail: &str) {
    if !reason.is_empty() {
        reason.push_str("; ");
    }
    reason.push_str(detail);
}

/// The four metric value states defined by [METRICS §1](METRICS_REFERENCE.md#1-value-statuses).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MetricStatus {
    /// Directly observed by instrumentation or monitoring.
    Measured,
    /// Derived or approximated from other observations.
    Estimated,
    /// Supported but not readable in this run.
    Unavailable,
    /// Meaningless for the selected benchmark mode.
    NotApplicable,
    /// An unrecognised status token copied without interpretation.
    Unknown(String),
}

/// Map stored metric status tokens without hiding unknown values.
impl MetricStatus {
    /// Map one serialized metric status token to a display state.
    pub fn from_token(token: &str) -> Self {
        match token {
            "measured" => Self::Measured,
            "estimated" => Self::Estimated,
            "unavailable" => Self::Unavailable,
            "not_applicable" => Self::NotApplicable,
            other => Self::Unknown(other.to_string()),
        }
    }

    /// Return the stable token rendered for this status.
    pub fn label(&self) -> &str {
        match self {
            Self::Measured => "measured",
            Self::Estimated => "estimated",
            Self::Unavailable => "unavailable",
            Self::NotApplicable => "not_applicable",
            Self::Unknown(token) => token,
        }
    }

    /// Return true only when this status carries a numeric value.
    pub fn has_value(&self) -> bool {
        matches!(self, Self::Measured | Self::Estimated)
    }
}

/// One metric record copied from a stored result run.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResultMetric {
    /// Stable metric identifier such as `MET-01`.
    id: String,
    /// Human-readable metric name.
    name: String,
    /// The recorded four-state status.
    status: MetricStatus,
    /// The original value text, empty for non-value states.
    value: String,
    /// Recorded SI or derived unit.
    unit: String,
    /// Instrumentation source token.
    source: String,
    /// Reason recorded for a non-value state.
    reason: String,
}

/// Decode and expose one stored metric record without recomputing it.
impl ResultMetric {
    /// Decode one metric record from a run's `metrics` array.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            id: required_string(value, "id", "metric")?,
            name: required_string(value, "name", "metric")?,
            status: MetricStatus::from_token(&required_string(value, "status", "metric")?),
            value: number_text(value.get("value")),
            unit: optional_string(value, "unit", "metric")?,
            source: optional_string(value, "source", "metric")?,
            reason: optional_string(value, "reason", "metric")?,
        })
    }

    /// Return the stable metric identifier.
    pub fn id(&self) -> &str {
        &self.id
    }

    /// Return the human-readable metric name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the recorded four-state status.
    pub fn status(&self) -> &MetricStatus {
        &self.status
    }

    /// Return the recorded value text, empty for non-value states.
    pub fn value(&self) -> &str {
        &self.value
    }

    /// Return the recorded instrumentation source.
    pub fn source(&self) -> &str {
        &self.source
    }

    /// Return the most informative display text without hiding a non-value state.
    pub fn display_value(&self) -> String {
        if self.value.is_empty() {
            value_or_placeholder(&self.reason, "(no value recorded)")
        } else if self.unit.is_empty() {
            self.value.clone()
        } else {
            format!("{} {}", self.value, self.unit)
        }
    }
}

/// One validation check outcome copied from a stored result run.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ValidationCheck {
    /// Stable check identifier such as `finite_output`.
    check: String,
    /// Human-readable case label.
    case_label: String,
    /// Recorded check status token.
    status: String,
    /// Explanation attached to the outcome.
    reason: String,
}

/// Decode and expose one validation check outcome.
impl ValidationCheck {
    /// Decode one check record from a validation `checks` array.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            check: required_string(value, "check", "validation check")?,
            case_label: optional_string(value, "case_label", "validation check")?,
            status: required_string(value, "status", "validation check")?,
            reason: optional_string(value, "reason", "validation check")?,
        })
    }

    /// Return the stable check identifier.
    pub fn check(&self) -> &str {
        &self.check
    }

    /// Return the human-readable case label.
    pub fn case_label(&self) -> &str {
        &self.case_label
    }

    /// Return the recorded check status token.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the recorded explanation.
    pub fn reason(&self) -> &str {
        &self.reason
    }
}

/// Validation summary and check outcomes copied from one stored run.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ValidationSummary {
    /// Validation level used for the run.
    level: String,
    /// Number of cases checked by the core.
    cases_checked: u64,
    /// Every recorded check outcome in source order.
    checks: Vec<ValidationCheck>,
}

/// Decode and expose one validation summary.
impl ValidationSummary {
    /// Decode one validation object from a stored run.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        let checks = optional_array(value, "checks", "validation")?
            .iter()
            .map(ValidationCheck::from_json)
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self {
            level: optional_string(value, "level", "validation")?,
            cases_checked: value
                .get("cases_checked")
                .and_then(Json::as_i64)
                .unwrap_or(0)
                .max(0) as u64,
            checks,
        })
    }

    /// Return the recorded validation level.
    pub fn level(&self) -> &str {
        &self.level
    }

    /// Return the number of checked cases.
    pub fn cases_checked(&self) -> u64 {
        self.cases_checked
    }

    /// Return every recorded check outcome.
    pub fn checks(&self) -> &[ValidationCheck] {
        &self.checks
    }
}

/// One classified warning or failure copied from a stored result.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResultIssue {
    /// Stable classification category.
    category: String,
    /// Human-readable context recorded by the core.
    message: String,
}

/// Decode and display one classified issue.
impl ResultIssue {
    /// Decode one warning or failure record.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            category: optional_string(value, "category", "classified issue")?,
            message: optional_string(value, "message", "classified issue")?,
        })
    }

    /// Return the stable classification category.
    pub fn category(&self) -> &str {
        &self.category
    }

    /// Return the recorded human-readable context.
    pub fn message(&self) -> &str {
        &self.message
    }

    /// Return one visible line that retains both category and context.
    pub fn display(&self) -> String {
        if self.category.is_empty() {
            value_or_placeholder(&self.message, "(no issue detail recorded)")
        } else {
            format!(
                "{}: {}",
                self.category,
                value_or_placeholder(&self.message, "(no issue detail recorded)")
            )
        }
    }
}

/// One retained raw timing population and its sample count.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RawSampleGroup {
    /// Timing level token recorded by the core.
    level: String,
    /// Number of retained raw durations.
    count: usize,
}

/// Expose one retained raw timing population.
impl RawSampleGroup {
    /// Build one raw sample group.
    fn new(level: impl Into<String>, count: usize) -> Self {
        Self {
            level: level.into(),
            count,
        }
    }

    /// Return the timing level token.
    pub fn level(&self) -> &str {
        &self.level
    }

    /// Return the number of retained raw durations.
    pub fn count(&self) -> usize {
        self.count
    }
}

/// The overhead estimate retained separately from measured timings.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OverheadEstimate {
    /// Recorded value status of the estimate.
    status: MetricStatus,
    /// Estimated overhead in seconds.
    seconds: String,
    /// DSP time observed during calibration in seconds.
    dsp_time_seconds: String,
    /// Number of calibration intervals used.
    interval_count: u64,
    /// Calibration method token.
    method: String,
    /// Number of retained calibration samples.
    calibration_count: usize,
}

/// Decode and display one retained overhead estimate.
impl OverheadEstimate {
    /// Decode one `timing.overhead_estimate` object.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        Ok(Self {
            status: MetricStatus::from_token(&optional_string(
                value,
                "status",
                "overhead estimate",
            )?),
            seconds: number_text(value.get("seconds")),
            dsp_time_seconds: number_text(value.get("dsp_time_seconds")),
            interval_count: value
                .get("interval_count")
                .and_then(Json::as_i64)
                .unwrap_or(0)
                .max(0) as u64,
            method: optional_string(value, "method", "overhead estimate")?,
            calibration_count: value
                .get("calibration_samples_seconds")
                .and_then(Json::as_array)
                .map_or(0, |samples| samples.len()),
        })
    }

    /// Return the recorded value status of this estimate.
    pub fn status(&self) -> &MetricStatus {
        &self.status
    }

    /// Return one visible summary retaining all recorded estimate fields.
    pub fn display(&self) -> String {
        format!(
            "status={} seconds={} dsp_time_seconds={} intervals={} method={} calibration_samples={}",
            value_or_placeholder(self.status.label(), "unknown"),
            value_or_placeholder(&self.seconds, "(unset)"),
            value_or_placeholder(&self.dsp_time_seconds, "(unset)"),
            self.interval_count,
            value_or_placeholder(&self.method, "(unreported)"),
            self.calibration_count
        )
    }
}

/// Everything the results screen reads from one stored run record.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct RunInspection {
    /// Stable run identifier.
    id: String,
    /// Stored run status token.
    status: String,
    /// Compact description of the recorded test point.
    test_point: String,
    /// Execution mode actually used for the run.
    isolation_mode: String,
    /// Metric records in stored order.
    metrics: Vec<ResultMetric>,
    /// Validation summary and check outcomes.
    validation: ValidationSummary,
    /// Classified measurement warnings.
    warnings: Vec<ResultIssue>,
    /// Classified workload or infrastructure failures.
    failures: Vec<ResultIssue>,
    /// Retained raw timing populations in stored order.
    raw_samples: Vec<RawSampleGroup>,
    /// Separately retained overhead estimate, when one was recorded.
    overhead: Option<OverheadEstimate>,
}

/// Decode and expose one stored run record without recomputing metrics.
impl RunInspection {
    /// Decode one run record from a result document's `runs` array.
    fn from_json(value: &Json) -> Result<Self, DataError> {
        let metrics = required_array(value, "metrics", "run")?
            .iter()
            .map(ResultMetric::from_json)
            .collect::<Result<Vec<_>, _>>()?;
        let validation = value
            .get("validation")
            .map(ValidationSummary::from_json)
            .transpose()?
            .unwrap_or_default();
        let warnings = optional_array(value, "warnings", "run")?
            .iter()
            .map(ResultIssue::from_json)
            .collect::<Result<Vec<_>, _>>()?;
        let failures = optional_array(value, "failures", "run")?
            .iter()
            .map(ResultIssue::from_json)
            .collect::<Result<Vec<_>, _>>()?;
        let timing = value.get("timing");
        let raw_samples = timing
            .and_then(Json::as_object)
            .map(|entries| {
                entries
                    .iter()
                    .filter(|(level, _)| {
                        level != "overhead_estimate" && level != "overhead_estimates"
                    })
                    .filter_map(|(level, samples)| {
                        samples
                            .as_array()
                            .map(|items| RawSampleGroup::new(level.clone(), items.len()))
                    })
                    .collect()
            })
            .unwrap_or_default();
        let overhead = timing
            .and_then(|value| value.get("overhead_estimate"))
            .filter(|value| matches!(value, Json::Object(_)))
            .map(OverheadEstimate::from_json)
            .transpose()?;
        let point_field = |name: &str| {
            let field = value.get("test_point").and_then(|point| point.get(name));
            let text = number_text(field);
            if text.is_empty() {
                field
                    .and_then(Json::as_str)
                    .unwrap_or("(unset)")
                    .to_string()
            } else {
                text
            }
        };
        let test_point = format!(
            "level={} block_size={} channels={} instances={}",
            point_field("workload_level"),
            point_field("block_size"),
            point_field("channels"),
            point_field("instances")
        );
        let isolation_mode = value
            .get("execution")
            .and_then(|execution| execution.get("mode"))
            .and_then(Json::as_str)
            .unwrap_or("(unreported)")
            .to_string();
        Ok(Self {
            id: required_string(value, "id", "run")?,
            status: required_string(value, "status", "run")?,
            test_point,
            isolation_mode,
            metrics,
            validation,
            warnings,
            failures,
            raw_samples,
            overhead,
        })
    }

    /// Return true only when the stored run is complete and carries no failure evidence.
    ///
    /// A `complete` token is demoted by recorded failures or by any validation
    /// check whose stored status is `failed`, so contradictory documents cannot
    /// be shown as successful runs.
    pub fn is_complete(&self) -> bool {
        self.status == "complete"
            && self.failures.is_empty()
            && !self
                .validation
                .checks()
                .iter()
                .any(|check| check.status() == "failed")
    }

    /// Return the stable run identifier.
    pub fn id(&self) -> &str {
        &self.id
    }

    /// Return the stored run status token.
    pub fn status(&self) -> &str {
        &self.status
    }

    /// Return the compact test-point description.
    pub fn test_point(&self) -> &str {
        &self.test_point
    }

    /// Return the execution mode actually used for this run.
    pub fn isolation_mode(&self) -> &str {
        &self.isolation_mode
    }

    /// Return every metric record in stored order.
    pub fn metrics(&self) -> &[ResultMetric] {
        &self.metrics
    }

    /// Return the validation summary.
    pub fn validation(&self) -> &ValidationSummary {
        &self.validation
    }

    /// Return every classified measurement warning.
    pub fn warnings(&self) -> &[ResultIssue] {
        &self.warnings
    }

    /// Return every classified failure.
    pub fn failures(&self) -> &[ResultIssue] {
        &self.failures
    }

    /// Return every retained raw timing population.
    pub fn raw_samples(&self) -> &[RawSampleGroup] {
        &self.raw_samples
    }

    /// Return the separately retained overhead estimate, when present.
    pub fn overhead(&self) -> Option<&OverheadEstimate> {
        self.overhead.as_ref()
    }
}

/// A typed, read-only view of one stored result document.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ResultInspection {
    /// Result document schema version.
    schema_version: String,
    /// SDK version recorded in the document.
    sdk_version: String,
    /// Session status token as stored on disk.
    stored_status: String,
    /// Status derived from stored failure evidence, never trusted from one token alone.
    derived_status: String,
    /// Explanation when the derived status differs from the stored status.
    status_reason: String,
    /// Inspected run records in stored order.
    runs: Vec<RunInspection>,
    /// Session-level classified warnings.
    warnings: Vec<ResultIssue>,
    /// Session-level classified failures.
    failures: Vec<ResultIssue>,
}

/// Decode and inspect one stored result document without recomputing metrics.
impl ResultInspection {
    /// Decode one compact result document returned by `ztick_frontend_result_read`.
    ///
    /// A stored `complete` token is demoted whenever session or run failures,
    /// non-complete runs, or a contradictory run status are present. Both the
    /// stored and derived statuses remain available for display.
    ///
    /// # Parameters
    /// - `text`: Compact JSON result document copied through the private boundary.
    ///
    /// # Returns
    /// The typed view, or an error naming the missing or invalid document member.
    pub fn from_json(text: &str) -> Result<Self, DataError> {
        let root = json::parse(text).map_err(|error| {
            DataError::new(format!(
                "invalid result document at byte {}: {}",
                error.offset(),
                error.message()
            ))
        })?;
        if root.as_object().is_none() {
            return Err(DataError::new("result document root is not an object"));
        }
        let schema_version = required_string(&root, "schema_version", "result document")?;
        if schema_version != "1.0.0" {
            return Err(DataError::new(format!(
                "unsupported result schema_version '{schema_version}'"
            )));
        }
        let sdk_version = required_string(&root, "sdk_version", "result document")?;
        let stored_status = required_string(&root, "status", "result document")?;
        let runs = required_array(&root, "runs", "result document")?
            .iter()
            .map(RunInspection::from_json)
            .collect::<Result<Vec<_>, _>>()?;
        let warnings = optional_array(&root, "warnings", "result document")?
            .iter()
            .map(ResultIssue::from_json)
            .collect::<Result<Vec<_>, _>>()?;
        let failures = optional_array(&root, "failures", "result document")?
            .iter()
            .map(ResultIssue::from_json)
            .collect::<Result<Vec<_>, _>>()?;

        let mut status_reason = String::new();
        let mut incomplete = stored_status != "complete";
        if stored_status != "complete" {
            append_reason(
                &mut status_reason,
                &format!("stored status is '{stored_status}'"),
            );
        }
        if !failures.is_empty() {
            incomplete = true;
            append_reason(
                &mut status_reason,
                &format!("{} session failure(s) recorded", failures.len()),
            );
        }
        for run in &runs {
            if !run.is_complete() {
                incomplete = true;
                append_reason(
                    &mut status_reason,
                    &format!(
                        "run {} has status '{}' or recorded failure evidence",
                        run.id(),
                        run.status()
                    ),
                );
            }
        }
        Ok(Self {
            schema_version,
            sdk_version,
            stored_status,
            derived_status: if incomplete { "incomplete" } else { "complete" }.to_string(),
            status_reason,
            runs,
            warnings,
            failures,
        })
    }

    /// Return true only when the stored evidence supports a complete run.
    pub fn is_complete(&self) -> bool {
        self.derived_status == "complete"
    }

    /// Return the result document schema version.
    pub fn schema_version(&self) -> &str {
        &self.schema_version
    }

    /// Return the SDK version recorded in the result document.
    pub fn sdk_version(&self) -> &str {
        &self.sdk_version
    }

    /// Return the session status token exactly as stored.
    pub fn stored_status(&self) -> &str {
        &self.stored_status
    }

    /// Return the status derived from stored failure evidence.
    pub fn derived_status(&self) -> &str {
        &self.derived_status
    }

    /// Return the explanation for a derived incomplete status.
    pub fn status_reason(&self) -> &str {
        &self.status_reason
    }

    /// Return every inspected run record.
    pub fn runs(&self) -> &[RunInspection] {
        &self.runs
    }

    /// Return every session-level warning.
    pub fn warnings(&self) -> &[ResultIssue] {
        &self.warnings
    }

    /// Return every session-level failure.
    pub fn failures(&self) -> &[ResultIssue] {
        &self.failures
    }
}

/// Deterministic result fixtures shared by the result-screen tests.
#[cfg(test)]
pub(crate) mod fixtures {
    /// A stored result document with all four metric statuses and retained evidence.
    pub(crate) const RESULT_JSON: &str = r#"{"schema_version":"1.0.0","sdk_version":"1.0.0","session":{"id":"s-1","started_at":"t0","ended_at":"t1"},"config_snapshot":{"config_version":"1.0.0","sdk_version":"1.0.0","integration":{"id":"app-a","version":"1.2.3"},"workload":{"level":"processor","processors":["eq"],"generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,"phase_rad":0.0}},"dimensions":{"sample_rate_hz":48000.0,"block_size":128,"channels":2,"oversampling":1,"instances":1},"execution":{"duration_mode":"iterations","iterations":100,"duration_seconds":0.0,"repetitions":1,"warmup":{"iterations":0},"isolation":"auto"},"profile":{"name":"Quick"},"measurement":{"detail":"aggregate","end_to_end":false,"timing_levels":["per_block","whole_run"]},"validation":{"level":"Basic","tolerances":{"absolute":0.000001,"relative":0.000001},"references":[]},"parallelism":{"workers":"auto","affinity":"auto","cpu_isolation":"off","priority_opt_in":false,"priority":0}},"build":{},"machine":{},"environment":{},"runs":[{"id":"run-1","test_point":{"workload_level":"processor","processors":["eq"],"sample_rate_hz":48000.0,"block_size":128,"channels":2,"instances":1,"oversampling":1},"config_snapshot_ref":"config_snapshot","timing":{"per_block":[0.001,0.002],"whole_run":[],"overhead_estimate":{"status":"estimated","seconds":0.0001,"dsp_time_seconds":0.01,"interval_count":3,"method":"empty_interval_calibration","calibration_samples_seconds":[0.0001]}},"metrics":[{"id":"MET-01","name":"Execution time","value":0.5,"unit":"s","status":"measured","source":"instrumented_dsp","method":"timer","reason":"","context":{}},{"id":"MET-17","name":"Benchmark overhead estimate","value":0.0001,"unit":"s","status":"estimated","source":"empty_interval_calibration","method":"empty_interval_calibration","reason":"","context":{}},{"id":"MET-08","name":"P90","value":null,"unit":"s","status":"unavailable","source":"instrumented_dsp","method":"nearest_rank","reason":"no samples","context":{}},{"id":"MET-09","name":"P99","value":null,"unit":"s","status":"not_applicable","source":"instrumented_dsp","method":"nearest_rank","reason":"whole-run basis","context":{}}],"validation":{"level":"Basic","cases_checked":2,"checks":[{"check":"finite_output","case_label":"case-a","level":"Basic","status":"passed","reason":"","observed_error":0.0,"checked_samples":2,"reference_source":"host_generated","tolerance":{"absolute":0.0,"relative":0.0}}],"tolerance":{"absolute":0.0,"relative":0.0}},"execution":{"isolation_request":"auto","mode":"in_process","workers":1,"affinity":"auto"},"warnings":[{"category":"execution","message":"thermal drift"}],"failures":[],"status":"complete"}],"comparisons":[],"warnings":[],"failures":[],"status":"complete"}"#;
}

/// Record a failed command note while leaving its decoded data intact.
fn record_status_note(output: &mut String, section: &str, text: &str) {
    if let Ok(envelope) = Envelope::parse(text, section)
        && let Some(note) = envelope.failure_note()
    {
        record_error(output, section, DataError::new(note));
    }
}

/// Append one labelled section error to the combined load message.
fn record_error(output: &mut String, section: &str, error: DataError) {
    if !output.is_empty() {
        output.push_str("; ");
    }
    output.push_str(section);
    output.push_str(": ");
    output.push_str(error.message());
}

/// Find one member in an ordered JSON object's entries.
fn json_member<'a>(
    entries: &'a [(String, crate::json::Json)],
    name: &str,
) -> Option<&'a crate::json::Json> {
    entries
        .iter()
        .find(|(member, _)| member == name)
        .map(|(_, value)| value)
}

/// Replace or append one member in an ordered JSON object.
fn set_json_member(
    entries: &mut Vec<(String, crate::json::Json)>,
    name: &str,
    value: crate::json::Json,
) {
    if let Some((_, existing)) = entries.iter_mut().find(|(member, _)| member == name) {
        *existing = value;
    } else {
        entries.push((name.to_string(), value));
    }
}

/// Return whether a named object member is a non-empty JSON array.
fn has_nonempty_array(entries: &[(String, crate::json::Json)], name: &str) -> bool {
    json_member(entries, name)
        .and_then(crate::json::Json::as_array)
        .is_some_and(|items| !items.is_empty())
}

/// A decoded portable v1 configuration document.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PortableConfig {
    /// The serialized text returned by the front end.
    text: String,
    /// The decoded document root.
    root: Json,
}

/// Decode and query portable config fields used by the editing screens.
impl PortableConfig {
    /// Decode one portable config document and require its v1 top-level shape.
    ///
    /// # Parameters
    /// - `text`: Compact JSON returned by `ztick_frontend_config_serialize`.
    ///
    /// # Returns
    /// The decoded document, or an error that names the missing or invalid member.
    pub fn from_json(text: &str) -> Result<Self, DataError> {
        let root = json::parse(text)
            .map_err(|error| DataError::new(format!("invalid config JSON: {error}")))?;
        if root.as_object().is_none() {
            return Err(DataError::new("config root is not an object"));
        }
        let version = Self::member(&root, &["config_version"]).and_then(Json::as_str);
        if version.is_none() {
            return Err(DataError::new("config_version is missing or not a string"));
        }
        if version != Some("1.0.0") {
            return Err(DataError::new(format!(
                "unsupported config_version '{}'",
                version.unwrap_or("(unset)")
            )));
        }
        for name in [
            "sdk_version",
            "integration",
            "workload",
            "dimensions",
            "execution",
            "profile",
            "measurement",
            "validation",
            "parallelism",
        ] {
            if Self::member(&root, &[name]).is_none() {
                return Err(DataError::new(format!("config member '{name}' is missing")));
            }
        }
        Ok(Self {
            text: text.to_string(),
            root,
        })
    }

    /// Return the serialized config text.
    pub fn text(&self) -> &str {
        &self.text
    }

    /// Follow one dotted member path from a decoded JSON root.
    fn member<'a>(root: &'a Json, path: &[&str]) -> Option<&'a Json> {
        let mut current = root;
        for key in path {
            current = current.get(key)?;
        }
        Some(current)
    }

    /// Read one string member by dotted path.
    fn string(&self, path: &[&str]) -> Option<&str> {
        Self::member(&self.root, path).and_then(Json::as_str)
    }

    /// Read one integer member by dotted path.
    fn integer(&self, path: &[&str]) -> Option<i64> {
        Self::member(&self.root, path).and_then(Json::as_i64)
    }

    /// Read one numeric member by dotted path.
    fn number(&self, path: &[&str]) -> Option<f64> {
        Self::member(&self.root, path).and_then(Json::as_f64)
    }

    /// Read one boolean member by dotted path.
    fn boolean(&self, path: &[&str]) -> Option<bool> {
        Self::member(&self.root, path).and_then(Json::as_bool)
    }

    /// Read one array-of-strings member by dotted path.
    fn strings(&self, path: &[&str]) -> Vec<String> {
        Self::member(&self.root, path)
            .and_then(Json::as_array)
            .map(|values| {
                values
                    .iter()
                    .filter_map(Json::as_str)
                    .map(str::to_string)
                    .collect()
            })
            .unwrap_or_default()
    }

    /// Return the portable config format version.
    pub fn config_version(&self) -> &str {
        self.string(&["config_version"]).unwrap_or("(unset)")
    }

    /// Return the SDK version recorded in the config.
    pub fn sdk_version(&self) -> &str {
        self.string(&["sdk_version"]).unwrap_or("(unset)")
    }

    /// Return the configured integration identifier.
    pub fn integration_id(&self) -> &str {
        self.string(&["integration", "id"]).unwrap_or("(unset)")
    }

    /// Return the configured integration version.
    pub fn integration_version(&self) -> &str {
        self.string(&["integration", "version"])
            .unwrap_or("(unset)")
    }

    /// Return the configured workload level token.
    pub fn workload_level(&self) -> &str {
        self.string(&["workload", "level"]).unwrap_or("(unset)")
    }

    /// Return the configured processor names.
    pub fn processors(&self) -> Vec<String> {
        self.strings(&["workload", "processors"])
    }

    /// Return the configured generator kind.
    pub fn generator_kind(&self) -> &str {
        self.string(&["workload", "generator", "kind"])
            .unwrap_or("(unset)")
    }

    /// Return the configured generator amplitude.
    pub fn amplitude(&self) -> Option<f64> {
        self.number(&["workload", "generator", "amplitude"])
    }

    /// Return the configured sine generator frequency.
    pub fn frequency_hz(&self) -> Option<f64> {
        self.number(&["workload", "generator", "frequency_hz"])
    }

    /// Return the generator object with its kind changed and required members retained.
    ///
    /// Existing amplitude, phase and tone or position settings are preserved. A
    /// newly selected multitone or impulse generator receives one deterministic
    /// default tone or position so the result remains runnable.
    ///
    /// # Parameters
    /// - `kind`: One of `sine`, `multitone` or `impulse`.
    ///
    /// # Returns
    /// Compact JSON for the replacement `workload.generator` value.
    pub fn generator_with_kind(&self, kind: &str) -> String {
        let mut generator = Self::member(&self.root, &["workload", "generator"])
            .cloned()
            .unwrap_or_else(|| {
                crate::json::Json::Object(vec![(
                    "amplitude".to_string(),
                    crate::json::Json::Number("0.5".to_string()),
                )])
            });
        if let crate::json::Json::Object(ref mut entries) = generator {
            set_json_member(entries, "kind", crate::json::Json::String(kind.to_string()));
            if json_member(entries, "amplitude").is_none() {
                set_json_member(
                    entries,
                    "amplitude",
                    crate::json::Json::Number("0.5".to_string()),
                );
            }
            if kind == "sine" {
                if json_member(entries, "frequency_hz").is_none() {
                    set_json_member(
                        entries,
                        "frequency_hz",
                        crate::json::Json::Number("1000.0".to_string()),
                    );
                }
                if json_member(entries, "phase_rad").is_none() {
                    set_json_member(
                        entries,
                        "phase_rad",
                        crate::json::Json::Number("0.0".to_string()),
                    );
                }
            }
            if kind == "multitone" && !has_nonempty_array(entries, "tones") {
                let tone = crate::json::Json::Object(vec![
                    (
                        "frequency_hz".to_string(),
                        crate::json::Json::Number("1000.0".to_string()),
                    ),
                    (
                        "phase_rad".to_string(),
                        crate::json::Json::Number("0.0".to_string()),
                    ),
                    (
                        "weight".to_string(),
                        crate::json::Json::Number("1.0".to_string()),
                    ),
                ]);
                set_json_member(entries, "tones", crate::json::Json::Array(vec![tone]));
            }
            if kind == "impulse" && json_member(entries, "position").is_none() {
                set_json_member(
                    entries,
                    "position",
                    crate::json::Json::Number("0".to_string()),
                );
            }
        }
        generator.compact()
    }

    /// Return the configured absolute validation tolerance.
    pub fn absolute_tolerance(&self) -> Option<f64> {
        self.number(&["validation", "tolerances", "absolute"])
    }

    /// Return the configured relative validation tolerance.
    pub fn relative_tolerance(&self) -> Option<f64> {
        self.number(&["validation", "tolerances", "relative"])
    }

    /// Return the configured sample rate.
    pub fn sample_rate_hz(&self) -> Option<f64> {
        self.number(&["dimensions", "sample_rate_hz"])
    }

    /// Return the configured block size.
    pub fn block_size(&self) -> Option<i64> {
        self.integer(&["dimensions", "block_size"])
    }

    /// Return the configured channel count.
    pub fn channels(&self) -> Option<i64> {
        self.integer(&["dimensions", "channels"])
    }

    /// Return the configured instance count.
    pub fn instances(&self) -> Option<i64> {
        self.integer(&["dimensions", "instances"])
    }

    /// Return the configured oversampling factor.
    pub fn oversampling(&self) -> Option<i64> {
        self.integer(&["dimensions", "oversampling"])
    }

    /// Return the configured profile name.
    pub fn profile_name(&self) -> &str {
        self.string(&["profile", "name"]).unwrap_or("(unset)")
    }

    /// Return the configured profile reliability check tokens.
    pub fn reliability_checks(&self) -> Vec<String> {
        self.strings(&["profile", "reliability_checks"])
    }

    /// Return the configured warm-up iteration count.
    pub fn warmup_iterations(&self) -> Option<i64> {
        self.integer(&["execution", "warmup", "iterations"])
    }

    /// Return the configured duration mode.
    pub fn duration_mode(&self) -> &str {
        self.string(&["execution", "duration_mode"])
            .unwrap_or("(unset)")
    }

    /// Return the configured iteration count.
    pub fn iterations(&self) -> Option<i64> {
        self.integer(&["execution", "iterations"])
    }

    /// Return the configured target duration in seconds.
    pub fn duration_seconds(&self) -> Option<f64> {
        self.number(&["execution", "duration_seconds"])
    }

    /// Return the configured repetition count.
    pub fn repetitions(&self) -> Option<i64> {
        self.integer(&["execution", "repetitions"])
    }

    /// Return the configured isolation request.
    pub fn isolation(&self) -> &str {
        self.string(&["execution", "isolation"])
            .unwrap_or("(unset)")
    }

    /// Return the configured measurement detail token.
    pub fn measurement_detail(&self) -> &str {
        self.string(&["measurement", "detail"]).unwrap_or("(unset)")
    }

    /// Return whether explicit end-to-end timing is enabled.
    pub fn end_to_end(&self) -> Option<bool> {
        self.boolean(&["measurement", "end_to_end"])
    }

    /// Return the configured timing level tokens.
    pub fn timing_levels(&self) -> Vec<String> {
        self.strings(&["measurement", "timing_levels"])
    }

    /// Return the configured validation level.
    pub fn validation_level(&self) -> &str {
        self.string(&["validation", "level"]).unwrap_or("(unset)")
    }

    /// Return the configured worker token.
    pub fn workers(&self) -> &str {
        self.string(&["parallelism", "workers"])
            .unwrap_or("(unset)")
    }

    /// Return the configured affinity token.
    pub fn affinity(&self) -> &str {
        self.string(&["parallelism", "affinity"])
            .unwrap_or("(unset)")
    }

    /// Return the optional OS-permitted CPU isolation request token.
    pub fn cpu_isolation(&self) -> &str {
        self.string(&["parallelism", "cpu_isolation"])
            .unwrap_or("off")
    }

    /// Return whether higher scheduling priority was explicitly accepted.
    pub fn priority_opt_in(&self) -> bool {
        self.boolean(&["parallelism", "priority_opt_in"])
            .unwrap_or(false)
    }

    /// Return the requested scheduling priority when one was recorded.
    pub fn priority(&self) -> Option<i64> {
        self.integer(&["parallelism", "priority"])
    }

    /// Compute the adapter capability bits required by this config.
    ///
    /// Unknown or missing tokens add no bits; the portability gate reports them
    /// separately so a malformed config is never silently treated as portable.
    pub fn required_capabilities(&self) -> u64 {
        let mut mask = match self.workload_level() {
            "processor" => crate::ZTICK_CAPABILITY_PROCESSOR,
            "chain" => crate::ZTICK_CAPABILITY_CHAIN,
            "engine" => crate::ZTICK_CAPABILITY_ENGINE,
            _ => 0,
        };
        if self
            .timing_levels()
            .iter()
            .any(|level| level == "per_block")
        {
            mask |= crate::ZTICK_CAPABILITY_PER_BLOCK_TIMING;
        }
        let workers = self.workers().parse::<u64>().unwrap_or(0);
        if workers > 1 {
            mask |= crate::ZTICK_CAPABILITY_INTERNAL_THREADING;
        }
        if !self.strings(&["validation", "references"]).is_empty() {
            mask |= crate::ZTICK_CAPABILITY_REFERENCE_DATA;
        }
        if self.boolean(&["validation", "custom"]).unwrap_or(false) {
            mask |= crate::ZTICK_CAPABILITY_CUSTOM_VALIDATION;
        }
        mask
    }
}

/// One explicit portability finding shown before a config can be applied.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PortabilityFinding {
    /// `blocker` prevents applying the config; `warning` remains visible.
    severity: String,
    /// The human-readable finding.
    message: String,
}

/// Construct and expose one portability finding.
impl PortabilityFinding {
    /// Build one finding with an explicit severity token.
    pub fn new(severity: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            severity: severity.into(),
            message: message.into(),
        }
    }

    /// Return the severity token.
    pub fn severity(&self) -> &str {
        &self.severity
    }

    /// Return the human-readable explanation.
    pub fn message(&self) -> &str {
        &self.message
    }
}

/// Portability results for one config against one discovered integration.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PortabilityReport {
    /// Findings that prevent applying the config.
    blockers: Vec<PortabilityFinding>,
    /// Findings that must remain visible without blocking.
    warnings: Vec<PortabilityFinding>,
}

/// Evaluate and inspect portability findings.
impl PortabilityReport {
    /// Compare one config with the currently discovered integration.
    ///
    /// No value is adapted. Missing metadata, incompatible SDK versions, absent
    /// capabilities and unsupported isolation requests all block explicitly.
    pub fn evaluate(
        config: &PortableConfig,
        plugin: Option<&PluginCandidate>,
        inspection: Option<&PluginInspection>,
        system: &SystemInventory,
    ) -> Self {
        let mut report = Self::default();
        report.extend_config_warnings(config, system);
        let Some(plugin) = plugin else {
            report.block("select an integration before applying the config");
            return report;
        };
        if !plugin.has_metadata() {
            report.block(format!(
                "integration inspection failed: {}",
                value_or_placeholder(plugin.reason(), "no reason recorded")
            ));
        }
        if !sdk_version_compatible(config.sdk_version(), plugin.sdk_version()) {
            report.block(format!(
                "SDK version mismatch: config {} but integration {}",
                config.sdk_version(),
                value_or_placeholder(plugin.sdk_version(), "unknown")
            ));
        }
        if !plugin.identifier().is_empty() && plugin.identifier() != config.integration_id() {
            report.block(format!(
                "integration mismatch: config '{}' but discovered '{}'",
                config.integration_id(),
                plugin.identifier()
            ));
        }
        if let Some(inspection) = inspection
            && inspection.path() == plugin.path()
            && inspection.compatibility().status() != "success"
        {
            report.block(format!(
                "core compatibility {}: {}",
                inspection.compatibility().status(),
                value_or_placeholder(inspection.compatibility().reason(), "no reason recorded")
            ));
        }
        let Some(capabilities) = parse_capability_mask(plugin.capabilities()) else {
            report.block(format!(
                "integration capability mask is not decodable: '{}'",
                plugin.capabilities()
            ));
            return report;
        };
        if !["processor", "chain", "engine"].contains(&config.workload_level()) {
            report.block(format!(
                "unsupported workload level '{}'",
                config.workload_level()
            ));
        }
        if !["sine", "multitone", "impulse"].contains(&config.generator_kind()) {
            report.block(format!(
                "unsupported generator kind '{}'",
                config.generator_kind()
            ));
        }
        if matches!(config.workload_level(), "processor" | "chain")
            && config.processors().is_empty()
        {
            report.block("processor workload requires at least one processor name");
        }
        let required = config.required_capabilities();
        let missing = required & !capabilities;
        for (bit, capability) in [
            (crate::ZTICK_CAPABILITY_PROCESSOR, "processor workload"),
            (crate::ZTICK_CAPABILITY_CHAIN, "chain workload"),
            (crate::ZTICK_CAPABILITY_ENGINE, "engine workload"),
            (crate::ZTICK_CAPABILITY_PER_BLOCK_TIMING, "per-block timing"),
            (
                crate::ZTICK_CAPABILITY_INTERNAL_THREADING,
                "internal threading",
            ),
            (
                crate::ZTICK_CAPABILITY_REFERENCE_DATA,
                "validation reference data",
            ),
            (
                crate::ZTICK_CAPABILITY_CUSTOM_VALIDATION,
                "custom validation",
            ),
        ] {
            if missing & bit != 0 {
                report.block(format!("integration lacks capability: {capability}"));
            }
        }
        if config.priority().is_some_and(|value| value > 0) && !config.priority_opt_in() {
            report.block(format!(
                "priority {} has no explicit opt-in; press p twice to accept higher priority",
                config.priority().unwrap_or(0)
            ));
        }
        if config.isolation() == "out_of_process" {
            report.block("out-of-process isolation is unavailable for the built-in workload");
        }
        if config.affinity() != "auto"
            && system
                .field("process_affinity")
                .is_some_and(|field| !field.status().has_value())
        {
            report.block(format!(
                "affinity request '{}' cannot be checked: {}",
                config.affinity(),
                system
                    .field("process_affinity")
                    .map(AvailabilityField::display_value)
                    .unwrap_or_else(|| "process affinity not reported".to_string())
            ));
        }
        if matches!(config.affinity(), "physical" | "logical") {
            let field_name = if config.affinity() == "physical" {
                "physical_core_count"
            } else {
                "logical_core_count"
            };
            match available_count(system, field_name) {
                Some(0) => report.block(format!("{field_name} is zero for affinity request")),
                Some(_) => {}
                None => report.block(format!(
                    "{field_name} is not reported; affinity cannot be checked"
                )),
            }
        }
        report
    }

    /// Add warnings that describe config choices even when integration checks block.
    fn extend_config_warnings(&mut self, config: &PortableConfig, system: &SystemInventory) {
        if config.priority().is_some_and(|value| value > 0) && !config.priority_opt_in() {
            self.warn(format!(
                "priority {} is not accepted; higher priority can reduce responsiveness and bias measurements",
                config.priority().unwrap_or(0)
            ));
        } else if config.priority_opt_in() {
            self.warn(format!(
                "higher priority {} is explicitly accepted; responsiveness and measurement bias may change",
                config.priority().unwrap_or(0)
            ));
        }
        if config.cpu_isolation() == "request" {
            let note = system
                .field("scheduling_configuration")
                .map(AvailabilityField::display_value)
                .unwrap_or_else(|| "scheduling configuration is not reported".to_string());
            self.warn(format!(
                "CPU isolation requested; {note}; the OS may refuse and fall back to inherited scheduling"
            ));
        }
        if config.end_to_end() == Some(true) {
            self.warn("end-to-end timing includes setup outside the DSP interval");
        }
    }

    /// Add one blocking finding.
    fn block(&mut self, message: impl Into<String>) {
        self.blockers
            .push(PortabilityFinding::new("blocker", message));
    }

    /// Add one non-blocking finding.
    fn warn(&mut self, message: impl Into<String>) {
        self.warnings
            .push(PortabilityFinding::new("warning", message));
    }

    /// Build a report containing one explicit blocker.
    pub fn blocked(message: impl Into<String>) -> Self {
        let mut report = Self::default();
        report.block(message);
        report
    }

    /// Return every blocking finding.
    pub fn blockers(&self) -> &[PortabilityFinding] {
        &self.blockers
    }

    /// Return every non-blocking finding.
    pub fn warnings(&self) -> &[PortabilityFinding] {
        &self.warnings
    }

    /// Return true only when no blocker was recorded.
    pub fn is_allowed(&self) -> bool {
        self.blockers.is_empty()
    }
}

/// Read one non-negative availability-aware count from the system inventory.
fn available_count(system: &SystemInventory, name: &str) -> Option<u64> {
    let field = system.field(name)?;
    if !field.status().has_value() {
        return None;
    }
    field.value().parse().ok()
}

/// Parse a three-part numeric version token.
fn parse_version(text: &str) -> Option<(u64, u64, u64)> {
    let mut parts = text.split('.');
    let major = parts.next()?.parse().ok()?;
    let minor = parts.next()?.parse().ok()?;
    let patch = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some((major, minor, patch))
}

/// Return whether an integration version can be used with a config version.
///
/// The major versions must match and the integration's minor version must not
/// exceed the config's; patch differences remain compatible.
fn sdk_version_compatible(config_version: &str, integration_version: &str) -> bool {
    let Some((config_major, config_minor, _)) = parse_version(config_version) else {
        return false;
    };
    let Some((integration_major, integration_minor, _)) = parse_version(integration_version) else {
        return false;
    };
    config_major == integration_major && integration_minor <= config_minor
}

/// Return one non-empty value or an explicit placeholder.
fn value_or_placeholder(value: &str, placeholder: &str) -> String {
    if value.is_empty() {
        placeholder.to_string()
    } else {
        value.to_string()
    }
}

/// Decode one hexadecimal or decimal adapter capability mask.
fn parse_capability_mask(text: &str) -> Option<u64> {
    let trimmed = text.trim();
    let digits = trimmed
        .strip_prefix("0x")
        .or_else(|| trimmed.strip_prefix("0X"))
        .unwrap_or(trimmed);
    if digits.is_empty() {
        return None;
    }
    u64::from_str_radix(digits, 16).ok()
}

/// Unit tests for typed envelope decoding and error reporting.
#[cfg(test)]
mod tests {
    use super::fixtures::RESULT_JSON;
    use super::{
        AvailabilityField, AvailabilityStatus, DashboardData, DataError, Envelope,
        PluginInspection, PluginList, PortabilityReport, PortableConfig, ProfileList,
        SystemInventory,
    };
    use crate::config::DEFAULT_CONFIG_JSON;
    use crate::json::{Json, parse};

    /// A system envelope covering all four availability states.
    const SYSTEM_JSON: &str = r#"{"schema_version":1,"command":"system","status":"success","exit_code":0,"data":{"fields":{"cpu_model":{"status":"value","value":"Test CPU","reason":""},"audio_backend":{"status":"not_applicable","value":"","reason":"no selected mode"},"cpu_frequency_hz":{"status":"unavailable","value":"","reason":"sysfs hidden"},"future_field":{"status":"unsupported","value":"","reason":"no backend"}}},"error":""}"#;
    /// A list envelope with one built-in profile.
    const LIST_JSON: &str = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":{"profiles":[{"name":"Quick","description":"fast"}]},"error":""}"#;
    /// A plugin list envelope with a directory and one candidate.
    const PLUGIN_JSON: &str = r#"{"schema_version":1,"command":"plugin list","status":"success","exit_code":0,"data":{"directories":[{"path":"/plugins","status":"unavailable","reason":"missing"}],"trust":{"status":"available","reason":"","entries":[]},"plugins":[{"path":"/plugins/app.so","source":"explicit_path","status":"success","reason":"","identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7c","trust":"untrusted"}]},"error":""}"#;
    /// A plugin inspection envelope with core compatibility.
    const INSPECT_JSON: &str = r#"{"schema_version":1,"command":"plugin inspect","status":"success","exit_code":0,"data":{"path":"/plugins/app.so","status":"success","reason":"no reason recorded","metadata":{"identifier":"app-a","name":"App A","version":"1.2.3","sdk_version":"1.0.0","capabilities":"0x7c","metadata":"test adapter"},"compatibility":{"status":"success","reason":"no reason recorded"},"trust":{"status":"available","reason":"","entries":[],"state":"untrusted"}},"error":""}"#;

    /// Build one availability field from explicit record parts.
    fn field(name: &str, status: &str, value: &str, reason: &str) -> AvailabilityField {
        let record = parse(&format!(
            r#"{{"status":"{status}","value":"{value}","reason":"{reason}"}}"#
        ))
        .expect("field JSON parses");
        AvailabilityField::from_json(name, &record).expect("field decodes")
    }

    /// Verify envelope rejects wrong command and version but keeps failed data.
    #[test]
    fn envelope_rejects_wrong_command_and_version_but_keeps_failed_data() {
        assert!(Envelope::parse(LIST_JSON, "system").is_err());
        let bad_version = SYSTEM_JSON.replace("\"schema_version\":1", "\"schema_version\":2");
        assert!(Envelope::parse(&bad_version, "system").is_err());
        let failed = SYSTEM_JSON.replace("\"status\":\"success\"", "\"status\":\"failure\"");
        let envelope = Envelope::parse(&failed, "system").expect("envelope decodes");
        assert!(envelope.data().is_ok());
        assert!(
            envelope
                .failure_note()
                .expect("failure is recorded")
                .contains("system reported failure")
        );
        let no_object = r#"{"schema_version":1,"command":"list","status":"success","exit_code":0,"data":null,"error":""}"#;
        let envelope = Envelope::parse(no_object, "list").expect("envelope decodes");
        assert!(envelope.data().is_err());
    }

    /// Verify failed system envelope still exposes its availability record.
    #[test]
    fn failed_system_envelope_still_exposes_its_availability_record() {
        let failed = r#"{"schema_version":1,"command":"system","status":"failure","exit_code":1,"data":{"fields":{"system_inventory":{"status":"unavailable","value":"","reason":"collector denied"}}},"error":"ztick-bench: system inventory unavailable: collector denied"}"#;
        let inventory = SystemInventory::from_json(failed).expect("failed data still decodes");
        let record = inventory
            .field("system_inventory")
            .expect("availability record is preserved");
        assert_eq!(record.status(), &super::AvailabilityStatus::Unavailable);
        assert_eq!(record.reason(), "collector denied");

        let dashboard = DashboardData::from_json(failed, LIST_JSON, PLUGIN_JSON);
        assert!(dashboard.load_error().contains("system reported failure"));
        assert!(dashboard.system().field("system_inventory").is_some());
    }

    /// Verify failed inspection envelope still exposes compatibility and trust.
    #[test]
    fn failed_inspection_envelope_still_exposes_compatibility_and_trust() {
        let failed = INSPECT_JSON
            .replacen("\"status\":\"success\"", "\"status\":\"failure\"", 2)
            .replace("\"error\":\"\"", "\"error\":\"inspection rejected\"");
        let inspection =
            PluginInspection::from_json(&failed).expect("failed inspection data still decodes");
        assert_eq!(inspection.status(), "failure");
        assert_eq!(inspection.compatibility().status(), "success");
        assert_eq!(inspection.trust().state(), "untrusted");
    }

    /// Verify availability tokens map without hiding unknown values.
    #[test]
    fn availability_tokens_map_without_hiding_unknown_values() {
        assert_eq!(
            AvailabilityStatus::from_token("unsupported"),
            AvailabilityStatus::Unsupported
        );
        assert_eq!(AvailabilityStatus::from_token("future").label(), "future");
        assert!(!AvailabilityStatus::Unavailable.has_value());
    }

    /// Verify availability field displays value or reason.
    #[test]
    fn availability_field_displays_value_or_reason() {
        let value = field("cpu", "value", "Test CPU", "");
        let unavailable = field("freq", "unavailable", "", "sysfs hidden");
        let empty = field("empty", "unsupported", "", "");
        assert_eq!(value.display_value(), "Test CPU");
        assert_eq!(unavailable.display_value(), "sysfs hidden");
        assert_eq!(empty.display_value(), "(no reason recorded)");
    }

    /// Verify system inventory keeps every field in source order.
    #[test]
    fn system_inventory_keeps_every_field_in_source_order() {
        let inventory = SystemInventory::from_json(SYSTEM_JSON).expect("inventory decodes");
        let names: Vec<&str> = inventory
            .fields()
            .iter()
            .map(AvailabilityField::name)
            .collect();
        assert_eq!(
            names,
            [
                "cpu_model",
                "audio_backend",
                "cpu_frequency_hz",
                "future_field"
            ]
        );
        assert_eq!(
            inventory
                .field("audio_backend")
                .map(AvailabilityField::status),
            Some(&AvailabilityStatus::NotApplicable)
        );
    }

    /// Verify profile list decodes documented shape.
    #[test]
    fn profile_list_decodes_documented_shape() {
        let profiles = ProfileList::from_json(LIST_JSON).expect("profiles decode");
        assert_eq!(profiles.profiles()[0].name(), "Quick");
        assert_eq!(profiles.profiles()[0].description(), "fast");
    }

    /// Verify plugin list decodes directories trust and candidates.
    #[test]
    fn plugin_list_decodes_directories_trust_and_candidates() {
        let plugins = PluginList::from_json(PLUGIN_JSON).expect("plugin list decodes");
        assert_eq!(plugins.directories()[0].status(), "unavailable");
        assert!(plugins.trust().is_available());
        let candidate = &plugins.plugins()[0];
        assert!(candidate.has_metadata());
        assert_eq!(candidate.identifier(), "app-a");
        assert_eq!(candidate.sdk_version(), "1.0.0");
        assert_eq!(candidate.capabilities(), "0x7c");
        assert_eq!(plugins.trust().state_for("app-a"), "untrusted");
    }

    /// Verify trust state reports unknown when the list is unreadable.
    #[test]
    fn trust_state_reports_unknown_when_the_list_is_unreadable() {
        let json =
            parse(r#"{"status":"unavailable","reason":"denied","entries":[],"state":"untrusted"}"#)
                .expect("trust JSON parses");
        let trust = super::TrustState::from_json(&json).expect("trust decodes");
        assert_eq!(trust.state_for("app-a"), "unknown");
        assert_eq!(trust.state(), "untrusted");
    }

    /// Verify plugin inspection decodes core compatibility.
    #[test]
    fn plugin_inspection_decodes_core_compatibility() {
        let inspection = PluginInspection::from_json(INSPECT_JSON).expect("inspection decodes");
        assert_eq!(inspection.compatibility().status(), "success");
        assert_eq!(inspection.trust().state(), "untrusted");
        assert_eq!(inspection.capabilities(), "0x7c");
    }

    /// Verify dashboard data preserves good sections and reports errors.
    #[test]
    fn dashboard_data_preserves_good_sections_and_reports_errors() {
        let data = DashboardData::from_json(SYSTEM_JSON, "not json", PLUGIN_JSON);
        assert!(data.load_error().contains("list"));
        assert_eq!(data.system().fields().len(), 4);
        assert_eq!(data.plugins().plugins().len(), 1);
        assert!(data.profiles().profiles().is_empty());
    }

    /// Verify dashboard data defaults are visible and empty.
    #[test]
    fn dashboard_data_defaults_are_visible_and_empty() {
        let data = DashboardData::default();
        assert_eq!(data.load_error(), "");
        assert!(data.system().fields().is_empty());
        assert!(data.profiles().profiles().is_empty());
        assert!(data.plugins().plugins().is_empty());
    }

    /// Verify data error carries its message.
    #[test]
    fn data_error_carries_its_message() {
        let error = DataError::new("bad envelope");
        assert_eq!(error.message(), "bad envelope");
        assert_eq!(error.to_string(), "bad envelope");
    }

    /// Verify required helpers reject missing or wrong members.
    #[test]
    fn required_helpers_reject_missing_or_wrong_members() {
        let object = parse(r#"{"text":"x","array":[]}"#).expect("object parses");
        assert!(super::required_string(&object, "missing", "test").is_err());
        assert!(super::required_string(&object, "array", "test").is_err());
        assert!(super::required_array(&object, "text", "test").is_err());
        assert_eq!(
            super::optional_string(&object, "missing", "test").expect("missing text defaults"),
            ""
        );
        assert!(super::optional_string(&object, "array", "test").is_err());
    }

    /// Verify json accessor helpers cover all value kinds.
    #[test]
    fn json_accessor_helpers_cover_all_value_kinds() {
        let value = parse(r#"[null,true,1,"x",{},[]]"#).expect("array parses");
        let items = value.as_array().expect("array value");
        assert_eq!(items[0], Json::Null);
        assert_eq!(items[1], Json::Bool(true));
        assert_eq!(items[2].as_i64(), Some(1));
        assert_eq!(items[3].as_str(), Some("x"));
        assert!(items[4].as_object().is_some());
        assert!(items[5].as_array().is_some());
        assert_eq!(items[1].as_bool(), Some(true));
        assert_eq!(items[2].as_f64(), Some(1.0));
    }

    /// Verify portable config decoding exposes typed fields and required capabilities.
    #[test]
    fn portable_config_decodes_typed_fields_and_capabilities() {
        let config = PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("config decodes");
        assert_eq!(config.sdk_version(), "1.0.0");
        assert_eq!(config.integration_id(), "app-a");
        assert_eq!(config.workload_level(), "processor");
        assert_eq!(config.processors(), ["eq"]);
        assert_eq!(config.sample_rate_hz(), Some(48000.0));
        assert_eq!(config.block_size(), Some(128));
        assert_eq!(config.profile_name(), "Quick");
        assert_eq!(config.timing_levels(), ["per_block", "whole_run"]);
        assert_eq!(config.required_capabilities(), 0x9);
        assert_eq!(config.cpu_isolation(), "off");

        let future = DEFAULT_CONFIG_JSON.replace(
            "\"config_version\":\"1.0.0\"",
            "\"config_version\":\"2.0.0\"",
        );
        assert!(PortableConfig::from_json(&future).is_err());
        assert!(PortableConfig::from_json("{\"config_version\":\"1.0.0\"}").is_err());
    }

    /// Verify generator kind changes preserve existing parameters and add required defaults.
    #[test]
    fn generator_kind_change_preserves_parameters() {
        let config = PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("config decodes");
        let multitone = config.generator_with_kind("multitone");
        assert!(multitone.contains("\"amplitude\":0.5"));
        assert!(multitone.contains("\"frequency_hz\":1000.0"));
        assert!(multitone.contains("\"tones\":[{"));
        let impulse = config.generator_with_kind("impulse");
        assert!(impulse.contains("\"amplitude\":0.5"));
        assert!(impulse.contains("\"position\":0"));
        assert!(!impulse.contains("\"tones\""));
    }

    /// Verify SDK compatibility allows patch differences but rejects minor or major regressions.
    #[test]
    fn sdk_compatibility_follows_major_and_minor_rules() {
        assert!(super::sdk_version_compatible("1.0.0", "1.0.3"));
        assert!(!super::sdk_version_compatible("1.0.0", "1.1.0"));
        assert!(!super::sdk_version_compatible("1.0.0", "2.0.0"));
        assert!(!super::sdk_version_compatible("1.0.0", "malformed"));
    }

    /// Verify a priority value without explicit opt-in blocks and still warns.
    #[test]
    fn portability_blocks_priority_without_opt_in() {
        let text = DEFAULT_CONFIG_JSON.replace("\"priority\":0", "\"priority\":5");
        let config = PortableConfig::from_json(&text).expect("priority config decodes");
        let plugins =
            PluginList::from_json(&PLUGIN_JSON.replace("0x7c", "0x7f")).expect("plugins decode");
        let inspection = PluginInspection::from_json(&INSPECT_JSON.replace("0x7c", "0x7f"))
            .expect("inspection decodes");
        let system = SystemInventory::from_json(SYSTEM_JSON).expect("system decodes");
        let report = PortabilityReport::evaluate(
            &config,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(!report.is_allowed());
        assert!(
            report.blockers()[0]
                .message()
                .contains("no explicit opt-in")
        );
        assert!(report.warnings()[0].message().contains("responsiveness"));
    }

    /// Verify CPU isolation and unknown affinity requests are reported explicitly.
    #[test]
    fn portability_reports_cpu_isolation_and_affinity_requests() {
        let plugins =
            PluginList::from_json(&PLUGIN_JSON.replace("0x7c", "0x7f")).expect("plugins decode");
        let inspection = PluginInspection::from_json(&INSPECT_JSON.replace("0x7c", "0x7f"))
            .expect("inspection decodes");
        let system = SystemInventory::from_json(SYSTEM_JSON).expect("system decodes");
        let cpu_request = DEFAULT_CONFIG_JSON
            .replace("\"cpu_isolation\":\"off\"", "\"cpu_isolation\":\"request\"");
        let cpu_request = PortableConfig::from_json(&cpu_request).expect("config decodes");
        let report = PortabilityReport::evaluate(
            &cpu_request,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(report.is_allowed());
        assert!(
            report.warnings()[0]
                .message()
                .contains("CPU isolation requested")
        );

        let affinity =
            DEFAULT_CONFIG_JSON.replace("\"affinity\":\"auto\"", "\"affinity\":\"physical\"");
        let affinity = PortableConfig::from_json(&affinity).expect("config decodes");
        let report = PortabilityReport::evaluate(
            &affinity,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(!report.is_allowed());
        assert!(
            report
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("physical_core_count"))
        );
    }

    /// Verify missing integrations and missing capabilities block explicitly.
    #[test]
    fn portability_blocks_missing_integration_and_capabilities() {
        let config = PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("config decodes");
        let plugins = PluginList::from_json(PLUGIN_JSON).expect("plugins decode");
        let inspection = PluginInspection::from_json(INSPECT_JSON).expect("inspection decodes");
        let system = SystemInventory::from_json(SYSTEM_JSON).expect("system decodes");

        let empty = PortabilityReport::evaluate(&config, None, None, &system);
        assert!(!empty.is_allowed());
        assert!(
            empty.blockers()[0]
                .message()
                .contains("select an integration")
        );

        let missing = PortabilityReport::evaluate(
            &config,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(!missing.is_allowed());
        assert!(
            missing
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("processor workload"))
        );
    }

    /// Verify supported capabilities pass and explicit priority opt-in warns.
    #[test]
    fn portability_allows_supported_capabilities_and_warns_on_priority() {
        let config = PortableConfig::from_json(DEFAULT_CONFIG_JSON).expect("config decodes");
        let supported_plugin = PLUGIN_JSON.replace("0x7c", "0x7f");
        let supported_inspection = INSPECT_JSON.replace("0x7c", "0x7f");
        let priority_config =
            DEFAULT_CONFIG_JSON.replace("\"priority_opt_in\":false", "\"priority_opt_in\":true");
        let priority_config =
            PortableConfig::from_json(&priority_config).expect("priority config decodes");
        let plugins = PluginList::from_json(&supported_plugin).expect("plugins decode");
        let inspection =
            PluginInspection::from_json(&supported_inspection).expect("inspection decodes");
        let system = SystemInventory::from_json(SYSTEM_JSON).expect("system decodes");

        let allowed = PortabilityReport::evaluate(
            &config,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(allowed.is_allowed(), "{:?}", allowed.blockers());
        assert!(allowed.warnings().is_empty());

        let warned = PortabilityReport::evaluate(
            &priority_config,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(warned.is_allowed());
        assert!(
            warned.warnings()[0]
                .message()
                .contains("explicitly accepted")
        );
    }

    /// Verify unsupported isolation and SDK mismatch are never silently adapted.
    #[test]
    fn portability_blocks_isolation_and_sdk_mismatch() {
        let config = DEFAULT_CONFIG_JSON
            .replace("\"isolation\":\"auto\"", "\"isolation\":\"out_of_process\"");
        let config = PortableConfig::from_json(&config).expect("config decodes");
        let plugins =
            PluginList::from_json(&PLUGIN_JSON.replace("1.0.0", "2.0.0")).expect("plugins decode");
        let inspection = PluginInspection::from_json(INSPECT_JSON).expect("inspection decodes");
        let system = SystemInventory::from_json(SYSTEM_JSON).expect("system decodes");

        let report = PortabilityReport::evaluate(
            &config,
            Some(&plugins.plugins()[0]),
            Some(&inspection),
            &system,
        );
        assert!(
            report
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("out-of-process isolation"))
        );
        assert!(
            report
                .blockers()
                .iter()
                .any(|finding| finding.message().contains("SDK version mismatch"))
        );
    }

    /// Verify a stored result keeps all four metric statuses and retained evidence.
    #[test]
    fn result_inspection_keeps_four_metric_statuses_and_retained_evidence() {
        let result =
            super::ResultInspection::from_json(RESULT_JSON).expect("result document decodes");
        assert_eq!(result.schema_version(), "1.0.0");
        assert_eq!(result.sdk_version(), "1.0.0");
        assert_eq!(result.stored_status(), "complete");
        assert_eq!(result.derived_status(), "complete");
        assert!(result.is_complete());
        assert!(result.status_reason().is_empty());

        let run = &result.runs()[0];
        assert!(run.is_complete());
        assert_eq!(run.id(), "run-1");
        assert_eq!(run.isolation_mode(), "in_process");
        assert!(run.test_point().contains("level=processor"));
        assert!(run.test_point().contains("block_size=128"));
        let statuses: Vec<&str> = run
            .metrics()
            .iter()
            .map(|metric| metric.status().label())
            .collect();
        assert_eq!(
            statuses,
            ["measured", "estimated", "unavailable", "not_applicable"]
        );
        assert_eq!(run.metrics()[0].display_value(), "0.5 s");
        assert_eq!(run.metrics()[1].display_value(), "0.0001 s");
        assert_eq!(run.metrics()[2].display_value(), "no samples");
        assert_eq!(run.metrics()[3].display_value(), "whole-run basis");
        assert_eq!(run.metrics()[0].source(), "instrumented_dsp");
        assert_eq!(run.metrics()[1].source(), "empty_interval_calibration");
        assert_eq!(run.validation().level(), "Basic");
        assert_eq!(run.validation().cases_checked(), 2);
        assert_eq!(run.validation().checks()[0].status(), "passed");
        assert_eq!(run.warnings()[0].display(), "execution: thermal drift");
        assert!(run.failures().is_empty());
        assert_eq!(
            run.raw_samples()
                .iter()
                .map(super::RawSampleGroup::count)
                .collect::<Vec<_>>(),
            [2, 0]
        );
        let overhead = run.overhead().expect("overhead estimate is retained");
        assert_eq!(overhead.status(), &super::MetricStatus::Estimated);
        assert!(overhead.display().contains("seconds=0.0001"));
        assert!(overhead.display().contains("calibration_samples=1"));
    }

    /// Verify a stored complete token is demoted by recorded failure evidence.
    #[test]
    fn result_inspection_demotes_contradictory_complete_status() {
        let failed = RESULT_JSON.replacen(
            "\"failures\":[]",
            "\"failures\":[{\"category\":\"workload_failure\",\"message\":\"validation checks failed\"}]",
            1,
        );
        let result = super::ResultInspection::from_json(&failed).expect("result decodes");
        assert_eq!(result.stored_status(), "complete");
        assert_eq!(result.derived_status(), "incomplete");
        assert!(!result.is_complete());
        assert!(result.status_reason().contains("run run-1"));
        assert!(!result.runs()[0].is_complete());
        assert_eq!(
            result.runs()[0].failures()[0].display(),
            "workload_failure: validation checks failed"
        );
    }

    /// Verify a failed validation check demotes a contradictory complete run.
    #[test]
    fn result_inspection_demotes_failed_validation_check() {
        let failed = RESULT_JSON.replacen("\"status\":\"passed\"", "\"status\":\"failed\"", 1);
        let result = super::ResultInspection::from_json(&failed).expect("result decodes");
        assert_eq!(result.stored_status(), "complete");
        assert_eq!(result.derived_status(), "incomplete");
        assert!(!result.is_complete());
        assert!(result.status_reason().contains("run run-1"));
        assert!(result.runs()[0].validation().checks()[0].status() == "failed");
    }

    /// Verify session-level failures and stored incomplete status both demote.
    #[test]
    fn result_inspection_demotes_session_failure_and_stored_incomplete_status() {
        let status_marker = "\"status\":\"complete\"}";
        let status_position = RESULT_JSON
            .rfind(status_marker)
            .expect("the session status is present");
        let mut incomplete = RESULT_JSON.to_string();
        incomplete.replace_range(
            status_position..status_position + status_marker.len(),
            "\"status\":\"incomplete\"}",
        );
        let marker = "\"failures\":[]";
        let position = incomplete
            .rfind(marker)
            .expect("the session failure array is present");
        incomplete.replace_range(
            position..position + marker.len(),
            "\"failures\":[{\"category\":\"infrastructure_failure\",\"message\":\"host lost\"}]",
        );
        let result = super::ResultInspection::from_json(&incomplete).expect("result decodes");
        assert_eq!(result.stored_status(), "incomplete");
        assert_eq!(result.derived_status(), "incomplete");
        assert!(
            result
                .status_reason()
                .contains("stored status is 'incomplete'")
        );
        assert!(result.status_reason().contains("; "));
        assert!(
            result
                .status_reason()
                .contains("1 session failure(s) recorded")
        );
        assert_eq!(
            result.failures()[0].display(),
            "infrastructure_failure: host lost"
        );
    }

    /// Verify malformed and unsupported result documents report explicit errors.
    #[test]
    fn result_inspection_rejects_malformed_and_unsupported_documents() {
        assert!(super::ResultInspection::from_json("not json").is_err());
        assert!(super::ResultInspection::from_json("[]").is_err());
        let future = RESULT_JSON.replacen(
            "\"schema_version\":\"1.0.0\"",
            "\"schema_version\":\"2.0.0\"",
            1,
        );
        assert!(super::ResultInspection::from_json(&future).is_err());
        let broken = RESULT_JSON.replacen("\"runs\":[{", "\"runs\":{\"id\":\"bad\"", 1);
        assert!(super::ResultInspection::from_json(&broken).is_err());
    }

    /// Verify missing optional sections stay visible as empty evidence.
    #[test]
    fn result_inspection_uses_empty_defaults_for_missing_optional_sections() {
        let minimal = r#"{"schema_version":"1.0.0","sdk_version":"1.0.0","status":"incomplete","runs":[{"id":"run-1","status":"failed","metrics":[]}]}"#;
        let result = super::ResultInspection::from_json(minimal).expect("minimal result decodes");
        assert_eq!(result.derived_status(), "incomplete");
        assert!(!result.is_complete());
        let run = &result.runs()[0];
        assert!(run.metrics().is_empty());
        assert!(run.validation().checks().is_empty());
        assert!(run.raw_samples().is_empty());
        assert!(run.overhead().is_none());
        assert!(run.warnings().is_empty());
        assert_eq!(run.isolation_mode(), "(unreported)");
    }

    /// Verify unknown status and empty issue tokens are never hidden.
    #[test]
    fn result_tokens_preserve_unknown_values() {
        assert_eq!(super::MetricStatus::from_token("future").label(), "future");
        assert!(!super::MetricStatus::from_token("future").has_value());
        assert!(super::MetricStatus::Measured.has_value());
        let empty = parse(r#"{"category":"","message":""}"#).expect("issue JSON parses");
        let issue = super::ResultIssue::from_json(&empty).expect("issue decodes");
        assert_eq!(issue.display(), "(no issue detail recorded)");
        let message = parse(r#"{"category":"","message":"reason"}"#).expect("issue JSON parses");
        let issue = super::ResultIssue::from_json(&message).expect("issue decodes");
        assert_eq!(issue.display(), "reason");
    }
}
