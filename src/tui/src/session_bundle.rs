// SPDX-License-Identifier: MIT
//! Versioned JSON session bundles for results, comparisons and configs.
//!
//! A bundle stores documents verbatim: no metric is recomputed and no result,
//! comparison or config value is reinterpreted. Export and import write each
//! file through a temporary file in the target directory followed by a rename,
//! so an interrupted write cannot truncate an existing document.

use std::collections::HashSet;
use std::error::Error;
use std::fmt;
use std::fs::{self, OpenOptions};
use std::io::{ErrorKind, Write};
use std::path::{Path, PathBuf};
use std::process;

use crate::json::{self, Json};

/// Schema version stored in every session bundle.
const SCHEMA_VERSION: &str = "1.0.0";
/// Number of unique temporary names tried before an export fails.
const TEMP_ATTEMPTS: u32 = 1000;

/// An error while parsing, exporting or importing a session bundle.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BundleError {
    /// The owned error detail.
    message: String,
}

/// Construct and inspect bundle errors.
impl BundleError {
    /// Construct one bundle error with an owned explanation.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }

    /// Return the human-readable error detail.
    pub fn message(&self) -> &str {
        &self.message
    }

    /// Wrap one filesystem error with the action and path that caused it.
    fn io(action: &str, path: &Path, error: std::io::Error) -> Self {
        Self::new(format!("failed to {action} '{}': {error}", path.display()))
    }
}

/// Format one bundle error for display.
impl fmt::Display for BundleError {
    /// Write the error detail into the supplied formatter.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

/// Mark bundle errors as standard errors.
impl Error for BundleError {}

/// Reject names that could escape the bundle target directory.
fn validate_name(name: &str) -> Result<(), BundleError> {
    if name.is_empty() || name.trim().is_empty() {
        return Err(BundleError::new("document name is empty"));
    }
    if name == "." || name.contains("..") {
        return Err(BundleError::new(format!(
            "unsafe document name '{name}': parent components are not permitted"
        )));
    }
    if name
        .chars()
        .any(|character| matches!(character, '/' | '\\' | '\0'))
    {
        return Err(BundleError::new(format!(
            "unsafe document name '{name}': path separators are not permitted"
        )));
    }
    let bytes = name.as_bytes();
    if bytes.len() >= 2 && bytes[1] == b':' && bytes[0].is_ascii_alphabetic() {
        return Err(BundleError::new(format!(
            "unsafe document name '{name}': drive prefixes are not permitted"
        )));
    }
    Ok(())
}

/// One named document retained verbatim in a session bundle.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BundleDocument {
    /// Safe file-stem name used when the document is reconstructed.
    name: String,
    /// Verbatim decoded JSON document.
    document: Json,
}

/// Construct and inspect one named bundle document.
impl BundleDocument {
    /// Build one bundle document after validating its name.
    ///
    /// # Parameters
    /// - `name`: Safe file-stem name without path separators or parent components.
    /// - `document`: Decoded result or config document to retain verbatim.
    ///
    /// # Returns
    /// The named document, or an error for an unsafe name or non-object document.
    pub fn new(name: impl Into<String>, document: Json) -> Result<Self, BundleError> {
        let name = name.into();
        validate_name(&name)?;
        if document.as_object().is_none() {
            return Err(BundleError::new(format!(
                "document '{name}' is not a JSON object"
            )));
        }
        Ok(Self { name, document })
    }

    /// Return the safe file-stem name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the retained document verbatim.
    pub fn document(&self) -> &Json {
        &self.document
    }
}

/// A versioned, portable collection of stored benchmark evidence.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SessionBundle {
    /// Named result documents in bundle order.
    results: Vec<BundleDocument>,
    /// Core comparison records in bundle order.
    comparisons: Vec<Json>,
    /// Named config documents in bundle order.
    configs: Vec<BundleDocument>,
}

/// Parse, serialize, export and import session bundles.
impl SessionBundle {
    /// Build one bundle from already validated documents and comparison records.
    pub fn new(
        results: Vec<BundleDocument>,
        comparisons: Vec<Json>,
        configs: Vec<BundleDocument>,
    ) -> Self {
        Self {
            results,
            comparisons,
            configs,
        }
    }

    /// Return every named result document in bundle order.
    pub fn results(&self) -> &[BundleDocument] {
        &self.results
    }

    /// Return every stored comparison record verbatim in bundle order.
    pub fn comparisons(&self) -> &[Json] {
        &self.comparisons
    }

    /// Return every named config document in bundle order.
    pub fn configs(&self) -> &[BundleDocument] {
        &self.configs
    }

    /// Serialize the bundle as compact versioned JSON.
    pub fn to_json(&self) -> String {
        Json::Object(vec![
            (
                "schema_version".to_string(),
                Json::String(SCHEMA_VERSION.to_string()),
            ),
            ("results".to_string(), document_array(&self.results)),
            (
                "comparisons".to_string(),
                Json::Array(self.comparisons.clone()),
            ),
            ("configs".to_string(), document_array(&self.configs)),
        ])
        .compact()
    }

    /// Parse one versioned session bundle from JSON text.
    ///
    /// # Parameters
    /// - `text`: Complete serialized session bundle.
    ///
    /// # Returns
    /// The verbatim bundle, or an error naming the malformed member.
    pub fn parse(text: &str) -> Result<Self, BundleError> {
        let root = json::parse(text)
            .map_err(|error| BundleError::new(format!("invalid bundle JSON: {error}")))?;
        if root.as_object().is_none() {
            return Err(BundleError::new("bundle root is not an object"));
        }
        let version = root
            .get("schema_version")
            .and_then(Json::as_str)
            .ok_or_else(|| BundleError::new("bundle schema_version is missing or not a string"))?;
        if version != SCHEMA_VERSION {
            return Err(BundleError::new(format!(
                "unsupported bundle schema_version '{version}'"
            )));
        }
        Ok(Self {
            results: parse_documents(&root, "results")?,
            comparisons: parse_comparisons(&root)?,
            configs: parse_documents(&root, "configs")?,
        })
    }

    /// Build one bundle from document texts without exposing the JSON type.
    ///
    /// Each result and config tuple is a `(safe file name, document text)` pair.
    /// Texts are parsed and retained in compact form with their member order and
    /// number tokens intact; no value is recomputed. Unsafe names, malformed
    /// JSON and non-object documents are reported as [`BundleError`].
    ///
    /// # Parameters
    /// - `results`: Named result document texts.
    /// - `comparisons`: Stored comparison record texts.
    /// - `configs`: Named config document texts.
    ///
    /// # Returns
    /// The bundle, or an error naming the unsafe name or malformed text.
    pub fn from_texts(
        results: &[(String, String)],
        comparisons: &[String],
        configs: &[(String, String)],
    ) -> Result<Self, BundleError> {
        let parse_named = |entries: &[(String, String)], kind: &str| {
            entries
                .iter()
                .map(|(name, text)| {
                    let document = json::parse(text).map_err(|error| {
                        BundleError::new(format!("invalid {kind} document '{name}': {error}"))
                    })?;
                    BundleDocument::new(name.clone(), document)
                })
                .collect::<Result<Vec<_>, _>>()
        };
        let parsed_comparisons = comparisons
            .iter()
            .enumerate()
            .map(|(index, text)| {
                let record = json::parse(text).map_err(|error| {
                    BundleError::new(format!("invalid bundle comparisons[{index}]: {error}"))
                })?;
                if record.as_object().is_none() {
                    return Err(BundleError::new(format!(
                        "bundle comparisons[{index}] is not an object"
                    )));
                }
                Ok(record)
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self::new(
            parse_named(results, "result")?,
            parsed_comparisons,
            parse_named(configs, "config")?,
        ))
    }

    /// Atomically export this bundle to one JSON file.
    ///
    /// A unique temporary file is created beside the target and renamed only
    /// after the complete bundle has been written and flushed.
    ///
    /// # Parameters
    /// - `path`: Destination bundle file path.
    ///
    /// # Returns
    /// Success, or an error that leaves any pre-existing target untouched.
    pub fn export_to(&self, path: impl AsRef<Path>) -> Result<(), BundleError> {
        write_atomic(path.as_ref(), &self.to_json())
    }

    /// Import a bundle and reconstruct its documents into a target directory.
    ///
    /// Each result and config is written atomically as `<name>.json`. Comparison
    /// records are retained in the returned bundle without being written or
    /// recomputed. Unsafe or duplicate names are rejected before any document is
    /// written, so one stored document cannot silently overwrite another.
    ///
    /// # Parameters
    /// - `path`: Serialized session bundle file path.
    /// - `target_dir`: Directory receiving reconstructed documents.
    ///
    /// # Returns
    /// The imported bundle, or an error; each individual document write is atomic.
    pub fn import_from(
        path: impl AsRef<Path>,
        target_dir: impl AsRef<Path>,
    ) -> Result<Self, BundleError> {
        let text = fs::read_to_string(path.as_ref())
            .map_err(|error| BundleError::io("read bundle", path.as_ref(), error))?;
        let bundle = Self::parse(&text)?;
        let mut names = HashSet::new();
        for document in bundle.results.iter().chain(bundle.configs.iter()) {
            if !names.insert(document.name()) {
                return Err(BundleError::new(format!(
                    "duplicate document name '{}': import would overwrite a stored document",
                    document.name()
                )));
            }
        }
        let target = target_dir.as_ref();
        fs::create_dir_all(target)
            .map_err(|error| BundleError::io("create import directory", target, error))?;
        for document in bundle.results.iter().chain(bundle.configs.iter()) {
            let destination = target.join(format!("{}.json", document.name()));
            write_atomic(&destination, &document.document().compact())?;
        }
        Ok(bundle)
    }
}

/// Serialize named documents into their bundle array representation.
fn document_array(documents: &[BundleDocument]) -> Json {
    Json::Array(
        documents
            .iter()
            .map(|document| {
                Json::Object(vec![
                    (
                        "name".to_string(),
                        Json::String(document.name().to_string()),
                    ),
                    ("document".to_string(), document.document().clone()),
                ])
            })
            .collect(),
    )
}

/// Read one optional array member from a bundle object.
fn optional_array<'a>(root: &'a Json, key: &str, context: &str) -> Result<&'a [Json], BundleError> {
    match root.get(key) {
        None | Some(Json::Null) => Ok(&[]),
        Some(Json::Array(items)) => Ok(items),
        Some(_) => Err(BundleError::new(format!(
            "{context} member '{key}' is not an array"
        ))),
    }
}

/// Decode one bundle's named document array.
fn parse_documents(root: &Json, key: &str) -> Result<Vec<BundleDocument>, BundleError> {
    let mut decoded = Vec::new();
    for (index, entry) in optional_array(root, key, "bundle")?.iter().enumerate() {
        let context = format!("bundle {key}[{index}]");
        if entry.as_object().is_none() {
            return Err(BundleError::new(format!("{context} is not an object")));
        }
        let name = entry
            .get("name")
            .and_then(Json::as_str)
            .ok_or_else(|| BundleError::new(format!("{context} member 'name' is not a string")))?;
        let document = entry
            .get("document")
            .filter(|member| member.as_object().is_some())
            .ok_or_else(|| {
                BundleError::new(format!("{context} member 'document' is not an object"))
            })?;
        decoded.push(BundleDocument::new(name, document.clone())?);
    }
    Ok(decoded)
}

/// Copy one bundle's comparison records without interpreting them.
fn parse_comparisons(root: &Json) -> Result<Vec<Json>, BundleError> {
    optional_array(root, "comparisons", "bundle")?
        .iter()
        .enumerate()
        .map(|(index, record)| {
            if record.as_object().is_none() {
                return Err(BundleError::new(format!(
                    "bundle comparisons[{index}] is not an object"
                )));
            }
            Ok(record.clone())
        })
        .collect()
}

/// Write one file through a unique temporary file in its target directory.
///
/// Bundles and copied report exports share this helper so neither can truncate a
/// previously valid artifact when a write is interrupted.
///
/// # Parameters
/// - `path`: Destination file path.
/// - `contents`: Complete text to write.
///
/// # Returns
/// Success, or an error that leaves any pre-existing target untouched.
pub fn write_atomic(path: &Path, contents: &str) -> Result<(), BundleError> {
    let parent = path
        .parent()
        .filter(|directory| !directory.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(parent)
        .map_err(|error| BundleError::io("create target directory", parent, error))?;
    let stem = path
        .file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_else(|| "bundle.json".to_string());
    for attempt in 0..TEMP_ATTEMPTS {
        let temporary: PathBuf =
            parent.join(format!(".{stem}.ztick-tmp-{}-{attempt}", process::id()));
        let mut file = match OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)
        {
            Ok(file) => file,
            Err(error) if error.kind() == ErrorKind::AlreadyExists => continue,
            Err(error) => {
                return Err(BundleError::io("create temporary file", &temporary, error));
            }
        };
        let written = file
            .write_all(contents.as_bytes())
            .and_then(|()| file.sync_all());
        drop(file);
        if let Err(error) = written {
            let _ = fs::remove_file(&temporary);
            return Err(BundleError::io("write temporary file", &temporary, error));
        }
        if let Err(error) = fs::rename(&temporary, path) {
            let _ = fs::remove_file(&temporary);
            return Err(BundleError::io("rename temporary file", path, error));
        }
        return Ok(());
    }
    Err(BundleError::new(format!(
        "could not allocate a unique temporary file for '{}'",
        path.display()
    )))
}

/// Unit tests for verbatim bundle storage, validation and atomic writes.
#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::{Path, PathBuf};
    use std::process;

    use super::{BundleDocument, BundleError, SessionBundle};
    use crate::json::{self, Json};

    /// A minimal result document used as a verbatim bundle payload.
    const RESULT_JSON: &str =
        r#"{"schema_version":"1.0.0","sdk_version":"1.0.0","status":"complete","runs":[]}"#;
    /// A minimal config document used as a verbatim bundle payload.
    const CONFIG_JSON: &str =
        r#"{"config_version":"1.0.0","sdk_version":"1.0.0","profile":{"name":"Quick"}}"#;
    /// A minimal comparison record used as a verbatim bundle payload.
    const COMPARISON_JSON: &str = r#"{"baseline":{"reference":"a.json"},"candidate":{"reference":"b.json"},"level":"run_to_run","classification":"raw_difference","metrics":[]}"#;

    /// Build a unique temporary directory for one bundle test.
    fn temp_dir(label: &str) -> PathBuf {
        std::env::temp_dir().join(format!("ztick_bundle_rust_{}_{label}", process::id()))
    }

    /// Remove one test directory before and after a test.
    fn reset_dir(path: &Path) {
        let _ = fs::remove_dir_all(path);
        fs::create_dir_all(path).expect("temporary directory is created");
    }

    /// Decode one JSON literal used by a bundle test.
    fn document(text: &str) -> Json {
        json::parse(text).expect("fixture JSON parses")
    }

    /// Build one bundle containing a result, comparison and config.
    fn sample_bundle() -> SessionBundle {
        SessionBundle::new(
            vec![BundleDocument::new("baseline", document(RESULT_JSON)).expect("name is safe")],
            vec![document(COMPARISON_JSON)],
            vec![BundleDocument::new("quick", document(CONFIG_JSON)).expect("name is safe")],
        )
    }

    /// Verify a bundle round-trip preserves every document verbatim.
    #[test]
    fn bundle_round_trip_preserves_documents_verbatim() {
        let bundle = sample_bundle();
        let serialized = bundle.to_json();
        let parsed = SessionBundle::parse(&serialized).expect("bundle parses");
        assert_eq!(parsed, bundle);
        assert_eq!(parsed.results()[0].name(), "baseline");
        assert_eq!(parsed.results()[0].document(), &document(RESULT_JSON));
        assert_eq!(parsed.configs()[0].document(), &document(CONFIG_JSON));
        assert_eq!(parsed.comparisons()[0], document(COMPARISON_JSON));

        let directory = temp_dir("round_trip");
        reset_dir(&directory);
        let archive = directory.join("session.bundle.json");
        bundle.export_to(&archive).expect("bundle exports");
        let imported = SessionBundle::import_from(&archive, directory.join("restored"))
            .expect("bundle imports");
        assert_eq!(imported, bundle);
        assert_eq!(
            fs::read_to_string(directory.join("restored/baseline.json"))
                .expect("result is restored"),
            document(RESULT_JSON).compact()
        );
        assert_eq!(
            fs::read_to_string(directory.join("restored/quick.json")).expect("config is restored"),
            document(CONFIG_JSON).compact()
        );
        let _ = fs::remove_dir_all(directory);
    }

    /// Verify unsafe document names are rejected at every entry point.
    #[test]
    fn unsafe_document_names_are_rejected() {
        for name in [
            "",
            "   ",
            ".",
            "..",
            "a..b",
            "../escape",
            "a/b",
            "a\\b",
            "/abs",
            "C:drive",
        ] {
            let error = BundleDocument::new(name, document(RESULT_JSON))
                .expect_err("unsafe name is rejected");
            assert!(!error.message().is_empty());
        }
        let unsafe_bundle = format!(
            r#"{{"schema_version":"1.0.0","results":[{{"name":"../escape","document":{RESULT_JSON}}}],"comparisons":[],"configs":[]}}"#
        );
        assert!(SessionBundle::parse(&unsafe_bundle).is_err());
        assert!(matches!(BundleError::new("bad"), error if error.message() == "bad"));
    }

    /// Verify duplicate reconstruction names are rejected before any write.
    #[test]
    fn duplicate_document_names_are_rejected_before_import_writes() {
        let directory = temp_dir("duplicate_names");
        reset_dir(&directory);
        let archive = directory.join("duplicate.bundle.json");
        let target = directory.join("restored");
        let duplicate = SessionBundle::new(
            vec![
                BundleDocument::new("result", document(RESULT_JSON)).expect("name is safe"),
                BundleDocument::new("result", document(CONFIG_JSON)).expect("name is safe"),
            ],
            Vec::new(),
            Vec::new(),
        );
        duplicate
            .export_to(&archive)
            .expect("duplicate bundle exports for import validation");
        let error = SessionBundle::import_from(&archive, &target)
            .expect_err("duplicate reconstruction names are rejected");
        assert!(error.message().contains("duplicate document name"));
        assert!(!target.exists());

        let _ = fs::remove_dir_all(directory);
    }

    /// Verify text-based construction preserves documents and rejects bad inputs.
    #[test]
    fn bundle_from_texts_preserves_documents_and_rejects_bad_inputs() {
        let results = vec![("baseline".to_string(), RESULT_JSON.to_string())];
        let comparisons = vec![COMPARISON_JSON.to_string()];
        let configs = vec![("quick".to_string(), CONFIG_JSON.to_string())];
        let bundle = SessionBundle::from_texts(&results, &comparisons, &configs)
            .expect("texts build a bundle");
        assert_eq!(bundle, sample_bundle());
        assert_eq!(bundle.to_json(), sample_bundle().to_json());

        let unsafe_results = vec![("../escape".to_string(), RESULT_JSON.to_string())];
        assert!(SessionBundle::from_texts(&unsafe_results, &[], &[]).is_err());
        let malformed_results = vec![("baseline".to_string(), "not json".to_string())];
        assert!(SessionBundle::from_texts(&malformed_results, &[], &[]).is_err());
        let malformed_comparisons = vec!["not json".to_string()];
        assert!(SessionBundle::from_texts(&[], &malformed_comparisons, &[]).is_err());
        let non_object_comparisons = vec!["[]".to_string()];
        assert!(SessionBundle::from_texts(&[], &non_object_comparisons, &[]).is_err());
    }

    /// Verify malformed bundles report explicit errors.
    #[test]
    fn malformed_bundles_report_explicit_errors() {
        assert!(SessionBundle::parse("not json").is_err());
        assert!(SessionBundle::parse("[]").is_err());
        assert!(SessionBundle::parse(r#"{"schema_version":"2.0.0"}"#).is_err());
        assert!(
            SessionBundle::parse(
                r#"{"schema_version":"1.0.0","results":[{"name":"r","document":[]}]}"#
            )
            .is_err()
        );
        assert!(SessionBundle::parse(r#"{"schema_version":"1.0.0","comparisons":[[]]}"#).is_err());
        assert!(SessionBundle::parse(r#"{"schema_version":"1.0.0","configs":{}}"#).is_err());
    }

    /// Verify import writes atomically and leaves existing files intact on error.
    #[test]
    fn import_is_atomic_and_preserves_existing_targets_on_failure() {
        let directory = temp_dir("atomic_import");
        reset_dir(&directory);
        let target = directory.join("restored");
        fs::create_dir_all(&target).expect("target directory is created");
        fs::write(target.join("baseline.json"), "sentinel").expect("existing file is written");

        let archive = directory.join("session.bundle.json");
        sample_bundle()
            .export_to(&archive)
            .expect("bundle exports before import");
        let imported = SessionBundle::import_from(&archive, &target).expect("bundle imports");
        assert_eq!(imported.comparisons().len(), 1);
        assert_eq!(
            fs::read_to_string(target.join("baseline.json")).expect("result is overwritten"),
            document(RESULT_JSON).compact()
        );
        assert!(
            fs::read_dir(&target)
                .expect("target is readable")
                .all(|entry| !entry
                    .expect("entry is readable")
                    .file_name()
                    .to_string_lossy()
                    .starts_with('.'))
        );

        let unsafe_bundle = format!(
            r#"{{"schema_version":"1.0.0","results":[{{"name":"../escape","document":{RESULT_JSON}}}],"comparisons":[],"configs":[]}}"#
        );
        fs::write(&archive, unsafe_bundle).expect("unsafe bundle is written");
        assert!(SessionBundle::import_from(&archive, &target).is_err());
        assert_eq!(
            fs::read_to_string(target.join("baseline.json")).expect("existing file is intact"),
            document(RESULT_JSON).compact()
        );
        let _ = fs::remove_dir_all(directory);
    }
}
