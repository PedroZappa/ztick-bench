// SPDX-License-Identifier: MIT
//! Configure native linkage to the private `ztick_frontend` C boundary.
//!
//! CMake builds `libztick_frontend` and passes its directory through
//! `ZTICK_FRONTEND_LIB_DIR`. A direct `cargo test` after `make build` falls back
//! to the default CMake library directory so the gate is usable outside CTest.

use std::env;
use std::path::PathBuf;

/// Resolve the directory containing the private front-end shared library.
///
/// The environment variable takes precedence; otherwise the default CMake build
/// tree is used. A missing directory produces a build-script error with an
/// explicit configuration hint.
fn library_dir() -> PathBuf {
    if let Some(value) = env::var_os("ZTICK_FRONTEND_LIB_DIR") {
        return PathBuf::from(value);
    }
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap_or_default());
    manifest.join("../../build/lib")
}

/// Emit Cargo directives for the native library and its runtime search path.
fn main() {
    println!("cargo:rerun-if-env-changed=ZTICK_FRONTEND_LIB_DIR");
    let dir = library_dir();
    if !dir.is_dir() {
        panic!(
            "ztick_frontend library directory {} is missing; build with CMake or set ZTICK_FRONTEND_LIB_DIR",
            dir.display()
        );
    }
    println!("cargo:rustc-link-search=native={}", dir.display());
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
}
