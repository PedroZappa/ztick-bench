// SPDX-License-Identifier: MIT
/**
 * @file plugin.cpp
 * @brief Placeholder translation unit for the plugin subsystem target.
 *
 * Discovery, metadata inspection, trust, loader and runner sources replace this
 * file during Phase 3. Metadata-only inspection and trust policy live in
 * `dynamic_loader.cpp` and `trust.cpp`; runner selection and both execution modes
 * live in `runner.cpp`, with child-process transport behind the platform launcher.
 * Discovery enumeration remains later work. The target links the compiled SDK runtime
 * so discovery can inspect registered descriptors through <ztick/sdk_registry.hpp>
 * without calling adapter code. This placeholder intentionally declares no functions.
 */
