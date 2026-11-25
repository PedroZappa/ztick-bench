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

# Optional quality gates need cargo subcommands that are not required by the core,
# CLI or basic TUI build. verify.sh enables them only when those tools are
# installed; CI sets ZTICK_REQUIRE_RUST_TOOLS so a missing tool fails loudly.
option(ZTICK_RUST_COVERAGE "Register the Rust TUI coverage gate (requires cargo-llvm-cov)" OFF)
option(ZTICK_RUST_DENY "Register the Rust TUI dependency-policy gate (requires cargo-deny)" OFF)
set(ZTICK_RUST_COVERAGE_MIN_LINES "" CACHE STRING
  "Optional minimum line coverage percentage for the tui.rust.coverage gate")

if(ZTICK_RUST_COVERAGE)
  execute_process(
    COMMAND "${ZTICK_CARGO_EXECUTABLE}" llvm-cov --version
    RESULT_VARIABLE _ztick_llvm_cov_status
    OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT _ztick_llvm_cov_status EQUAL 0)
    message(FATAL_ERROR
      "ZTICK_RUST_COVERAGE=ON requires cargo-llvm-cov; install it or configure with OFF")
  endif()
endif()

if(ZTICK_RUST_DENY)
  execute_process(
    COMMAND "${ZTICK_CARGO_EXECUTABLE}" deny --version
    RESULT_VARIABLE _ztick_cargo_deny_status
    OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT _ztick_cargo_deny_status EQUAL 0)
    message(FATAL_ERROR
      "ZTICK_RUST_DENY=ON requires cargo-deny; install it or configure with OFF")
  endif()
endif()

set(_ztick_rust_coverage_dir "${CMAKE_BINARY_DIR}/coverage/rust")
set(_ztick_rust_coverage_script "${CMAKE_SOURCE_DIR}/scripts/check_rust_coverage.sh")
set(_ztick_rust_deny_config "${CMAKE_CURRENT_SOURCE_DIR}/deny.toml")
set(_ztick_coverage_env
  ${_ztick_cargo_env}
  "ZTICK_CARGO=${ZTICK_CARGO_EXECUTABLE}"
  "ZTICK_TUI_MANIFEST=${_ztick_tui_manifest}"
  "ZTICK_CARGO_TARGET_DIR=${_ztick_tui_target_dir}"
  "ZTICK_RUST_COVERAGE_DIR=${_ztick_rust_coverage_dir}"
)
if(ZTICK_RUST_COVERAGE_MIN_LINES)
  list(APPEND _ztick_coverage_env
    "ZTICK_RUST_COVERAGE_MIN_LINES=${ZTICK_RUST_COVERAGE_MIN_LINES}"
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
# The remaining named gates keep documentation, coverage and dependency policy
# on the same CTest path as formatting and linting.
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
  NAME tui.rust.doc
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "RUSTDOCFLAGS=-D warnings"
          "${ZTICK_CARGO_EXECUTABLE}" doc --manifest-path "${_ztick_tui_manifest}" --no-deps
)

if(ZTICK_RUST_COVERAGE)
  add_test(
    NAME tui.rust.coverage
    COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_coverage_env}
            "${_ztick_rust_coverage_script}"
  )
endif()

if(ZTICK_RUST_DENY)
  add_test(
    NAME tui.rust.deny
    COMMAND "${ZTICK_CARGO_EXECUTABLE}" deny --manifest-path "${_ztick_tui_manifest}"
            --config "${_ztick_rust_deny_config}" --locked check
  )
endif()

add_test(
  NAME tui.ffi.smoke
  COMMAND "${CMAKE_COMMAND}" -E env ${_ztick_cargo_env}
          "${ZTICK_CARGO_EXECUTABLE}" test --manifest-path "${_ztick_tui_manifest}"
          --test ffi_smoke
)
