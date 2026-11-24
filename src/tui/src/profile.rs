// SPDX-License-Identifier: MIT
//! Named profile persistence as portable v1 config documents.
//!
//! Named profiles add no CLI subcommand: each saved profile is the same config
//! document accepted by `run`, stored under a stable file name in a user config
//! directory. Writes use a temporary file plus rename so an interrupted save
//! cannot leave a half-written profile behind.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// One saved profile discovered on disk.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SavedProfile {
    /// The stored `profile.name` shown in the Profiles screen.
    name: String,
    /// The absolute or relative path of the config document.
    path: PathBuf,
}

/// Construct and expose one saved profile record.
impl SavedProfile {
    /// Build one saved profile record.
    ///
    /// # Parameters
    /// - `name`: Stable profile name shown in the Profiles screen.
    /// - `path`: Config document path used for reloads.
    pub fn new(name: impl Into<String>, path: impl Into<PathBuf>) -> Self {
        Self {
            name: name.into(),
            path: path.into(),
        }
    }

    /// Return the stable profile name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Return the config document path.
    pub fn path(&self) -> &Path {
        &self.path
    }
}

/// Resolve the default user profile directory without creating it.
///
/// # Returns
/// `$XDG_CONFIG_HOME/ztick-bench/profiles`, `$HOME/.config/ztick-bench/profiles`,
/// or a temporary directory when neither environment variable is available.
pub fn default_directory() -> PathBuf {
    if let Some(value) = std::env::var_os("XDG_CONFIG_HOME")
        && !value.is_empty()
    {
        return PathBuf::from(value).join("ztick-bench").join("profiles");
    }
    if let Some(value) = std::env::var_os("HOME")
        && !value.is_empty()
    {
        return PathBuf::from(value)
            .join(".config")
            .join("ztick-bench")
            .join("profiles");
    }
    std::env::temp_dir().join("ztick-bench-profiles")
}

/// Build a stable file name for one profile name.
///
/// Characters other than Unicode alphanumerics, `-`, `_` and `.` collapse to a
/// single hyphen. Leading and trailing separators are removed. A name whose
/// sanitized form is not exact, or whose sanitized form is empty, receives a
/// deterministic suffix so distinct profile names cannot overwrite one another.
pub fn file_name(name: &str) -> String {
    let mut result = String::new();
    let mut separator_pending = false;
    for character in name.chars() {
        if character.is_alphanumeric() || matches!(character, '-' | '_' | '.') {
            if separator_pending && !result.is_empty() {
                result.push('-');
            }
            result.push(character);
            separator_pending = false;
        } else {
            separator_pending = true;
        }
    }
    let sanitized = result.trim_matches(|value| value == '-' || value == '.');
    if sanitized.is_empty() {
        format!("profile-{:016x}.json", stable_name_hash(name))
    } else if sanitized == name {
        format!("{sanitized}.json")
    } else {
        format!("{sanitized}-{:016x}.json", stable_name_hash(name))
    }
}

/// Return a deterministic 64-bit hash for one profile name.
fn stable_name_hash(name: &str) -> u64 {
    let mut hash = 0xcbf29ce484222325_u64;
    for byte in name.as_bytes() {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

/// Build the config document path for one profile name.
pub fn profile_path(directory: &Path, name: &str) -> PathBuf {
    directory.join(file_name(name))
}

/// Read the stored profile name from one saved config document.
///
/// A document without a non-empty `profile.name` is not listed: the sanitized
/// file name is an implementation detail and must not be shown as the profile's
/// identity.
fn stored_profile_name(path: &Path) -> Option<String> {
    let text = fs::read_to_string(path).ok()?;
    let root = crate::json::parse(&text).ok()?;
    let name = root.get("profile")?.get("name")?.as_str()?;
    if name.trim().is_empty() {
        None
    } else {
        Some(name.to_string())
    }
}

/// List saved profiles in stored-name order without hiding a missing directory.
///
/// A missing directory is an empty list; entries without a readable stored
/// `profile.name` are skipped because a partial listing is still more useful
/// than presenting a sanitized file stem as the profile's identity.
pub fn list(directory: &Path) -> Vec<SavedProfile> {
    let Ok(entries) = fs::read_dir(directory) else {
        return Vec::new();
    };
    let mut profiles = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|value| value.to_str()) != Some("json") {
            continue;
        }
        let name = stored_profile_name(&path);
        if let Some(name) = name {
            profiles.push(SavedProfile::new(name, path));
        }
    }
    profiles.sort_by(|left, right| left.name().cmp(right.name()));
    profiles
}

/// Read one saved profile document.
///
/// # Errors
/// Returns the filesystem error when the path cannot be opened or read.
pub fn read(path: &Path) -> io::Result<String> {
    fs::read_to_string(path)
}

/// Save one portable config document under a profile name atomically.
///
/// The directory is created when missing. A temporary file in the same
/// directory is written completely before it replaces the target path.
///
/// # Errors
/// Returns the first filesystem error from directory creation, temporary-file
/// writing or replacement. The temporary file is removed on a write failure.
pub fn save(directory: &Path, name: &str, config_text: &str) -> io::Result<PathBuf> {
    fs::create_dir_all(directory)?;
    let target = profile_path(directory, name);
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|value| value.as_nanos())
        .unwrap_or(0);
    let temporary = target.with_extension(format!("tmp-{}-{nonce}", std::process::id()));
    if let Err(error) = fs::write(&temporary, config_text) {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    if let Err(error) = fs::rename(&temporary, &target) {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    Ok(target)
}

/// Unit tests for profile naming and persistence.
#[cfg(test)]
mod tests {
    use std::time::{SystemTime, UNIX_EPOCH};

    use super::{SavedProfile, default_directory, file_name, list, profile_path, read, save};

    /// Build one isolated temporary directory for a filesystem test.
    fn temporary_directory(label: &str) -> std::path::PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock is after the epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("ztick-profile-{label}-{nonce}"))
    }

    /// Verify profile names map to stable safe file names.
    #[test]
    fn profile_names_map_to_stable_safe_file_names() {
        assert_eq!(file_name("Quick"), "Quick.json");
        let hashed = file_name("My Profile");
        assert!(hashed.starts_with("My-Profile-"));
        assert!(hashed.ends_with(".json"));
        assert_ne!(hashed, file_name("My  Profile"));
        assert!(file_name("  ").starts_with("profile-"));
        assert!(file_name("a/b").starts_with("a-b-"));
    }

    /// Verify default directory follows documented environment precedence.
    #[test]
    fn default_directory_ends_with_the_expected_suffix() {
        let directory = default_directory();
        assert!(directory.ends_with("profiles"));
        assert!(directory.to_string_lossy().contains("ztick-bench"));
    }

    /// Verify profile records expose their name and path.
    #[test]
    fn profile_records_expose_name_and_path() {
        let profile = SavedProfile::new("Quick", "/tmp/Quick.json");
        assert_eq!(profile.name(), "Quick");
        assert_eq!(profile.path(), std::path::Path::new("/tmp/Quick.json"));
        assert_eq!(
            profile_path(std::path::Path::new("/profiles"), "Quick"),
            std::path::Path::new("/profiles/Quick.json")
        );
    }

    /// Verify missing directories produce empty listings.
    #[test]
    fn missing_directories_produce_empty_listings() {
        let directory = temporary_directory("missing");
        assert!(list(&directory).is_empty());
    }

    /// Verify saved profiles round-trip in name order with their document text.
    #[test]
    fn saved_profiles_round_trip_in_name_order() {
        let directory = temporary_directory("round-trip");
        let first = save(&directory, "Zulu", "{\"profile\":{\"name\":\"Zulu\"}}")
            .expect("first profile is saved");
        let second = save(&directory, "Alpha", "{\"profile\":{\"name\":\"Alpha\"}}")
            .expect("second is saved");
        let mut third = save(&directory, "Alpha", "{\"profile\":{\"name\":\"Alpha\"}}")
            .expect("replacement is saved");
        third.set_file_name("notes.txt");
        std::fs::write(&third, "ignored").expect("non-profile file is written");
        save(
            &directory,
            "My Profile",
            "{\"profile\":{\"name\":\"My Profile\"}}",
        )
        .expect("named profile is saved");

        let profiles = list(&directory);
        let names: Vec<&str> = profiles.iter().map(SavedProfile::name).collect();
        assert_eq!(names, ["Alpha", "My Profile", "Zulu"]);
        assert_eq!(profiles[0].path().file_name(), second.file_name());
        assert_eq!(
            read(&second).expect("profile reads"),
            "{\"profile\":{\"name\":\"Alpha\"}}"
        );
        assert_eq!(
            read(&first).expect("profile reads"),
            "{\"profile\":{\"name\":\"Zulu\"}}"
        );

        std::fs::remove_dir_all(&directory).expect("temporary directory is removed");
    }
}
