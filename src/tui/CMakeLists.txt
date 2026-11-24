# SPDX-License-Identifier: MIT
# Optional Rust TUI crate. P5.0 wires the C ABI FFI smoke gate; P5.1 adds the
# ratatui application. No Cargo command runs when ZTICK_BUILD_TUI=OFF.

if(NOT ZTICK_BUILD_TUI)
  return()
endif()

# Rust is built without the project's C/C++ sanitizer flags. A Rust test binary
# therefore lacks the ASan runtime needed to load the instrumented front end, so
# the sanitizer gate runs only the C11 boundary caller instead.
if(ZTICK_ENABLE_SANITIZERS)
  return()
endif()

set(_ztick_tui_manifest "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml")
set(_ztick_tui_target_dir "${CMAKE_BINARY_DIR}/cargo/tui")
set(_ztick_frontend_lib_dir "$<TARGET_FILE_DIR:ztick_frontend>")
set(_ztick_cargo_env
  "CARGO_TARGET_DIR=${_ztick_tui_target_dir}"
  "ZTICK_FRONTEND_LIB_DIR=${_ztick_frontend_lib_dir}"
  "LD_LIBRARY_PATH=${_ztick_frontend_lib_dir}"
)

add_test(
  NAME tui.rust.format
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" fmt --manifest-path "${_ztick_tui_manifest}" -- --check
)

add_test(
  NAME tui.rust.clippy
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" clippy --manifest-path "${_ztick_tui_manifest}"
          --all-targets -- -D warnings
)

add_test(
  NAME tui.ffi.smoke
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" test --manifest-path "${_ztick_tui_manifest}"
          --test ffi_smoke
)
