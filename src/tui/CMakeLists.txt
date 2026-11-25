# SPDX-License-Identifier: MIT
# Optional Rust/ratatui TUI crate. P5.0 wires the C ABI FFI smoke gate; P5.1
# adds the executable and its own cargo test gate. No Cargo command runs when
# ZTICK_BUILD_TUI=OFF.

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
if(ZTICK_BUILD_EXAMPLES)
  list(APPEND _ztick_cargo_env
    "ZTICK_TUI_TEST_MODULE=$<TARGET_FILE:ztick_test_app_dynamic>"
  )
endif()

# The optional executable is built with the normal project build. Cargo uses its
# debug profile so the test and runtime gates share one compiled frontend.
add_custom_target(ztick_tui_binary ALL
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" build --manifest-path "${_ztick_tui_manifest}"
          --bin ztick-tui
  BYPRODUCTS "${_ztick_tui_target_dir}/debug/ztick-tui${CMAKE_EXECUTABLE_SUFFIX}"
  COMMENT "Building the Rust ratatui front end"
  VERBATIM
)
add_dependencies(ztick_tui_binary ztick_frontend)

# tui.rust.test is the single full Rust suite: --all-targets covers lib, bins,
# integration tests, examples and benches. tui.ffi.smoke stays registered as a
# named subset gate so the private boundary remains visible in every gate list.
add_test(
  NAME tui.rust.test
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" test --manifest-path "${_ztick_tui_manifest}"
          --all-targets
)

add_test(
  NAME tui.rust.format
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" fmt --all --manifest-path "${_ztick_tui_manifest}" -- --check
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
