# SPDX-License-Identifier: MIT
# Standalone public-header checks (P1.3). These translation units have no test
# framework dependency so they prove each header is self-contained in its target
# language. Catch2 v3 (P1.8) is reserved for behavioral unit tests.

# Catch2 v3 is dev-only (REQ-TEST-04). Prefer an installed package; fall back to
# a pinned FetchContent checkout so the version is reproducible.
find_package(Catch2 3.9.1 EXACT QUIET CONFIG)
if(Catch2_FOUND)
  list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
else()
  include(FetchContent)
  set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(CATCH_INSTALL_HELPERS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 644821ce28cb25d7992a4d0375b1d83214392592 # v3.9.1
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(Catch)

# POSIX-only helper for real out-of-process runner tests. It is never installed.
if(UNIX)
  add_executable(ztick_process_helper support/process_helper.cpp)
  target_link_libraries(ztick_process_helper PRIVATE ztick_plugin_host)
  add_test(NAME unit.child_process_helper COMMAND ztick_process_helper self-check)
endif()

add_executable(ztick_tests
  integration/phase2_engine_test.cpp
  unit/atomic_writer_test.cpp
  unit/catch2_integration_test.cpp
  unit/cli_test.cpp
  unit/compatibility_test.cpp
  unit/config_document_test.cpp
  unit/config_value_test.cpp
  unit/core_types_test.cpp
  unit/csv_projection_test.cpp
  unit/dynamic_loader_test.cpp
  unit/dynamic_runtime_test.cpp
  unit/execution_test.cpp
  unit/generator_test.cpp
  unit/json_test.cpp
  unit/platform_interface_test.cpp
  unit/linux_system_info_test.cpp
  unit/metadata_test.cpp
  unit/out_of_process_runner_test.cpp
  unit/profile_model_test.cpp
  unit/result_document_test.cpp
  unit/run_command_test.cpp
  unit/run_session_test.cpp
  unit/runner_test.cpp
  unit/sdk_runtime_test.cpp
  unit/statistics_test.cpp
  unit/sysinfo_report_test.cpp
  unit/system_command_test.cpp
  unit/system_info_metadata_test.cpp
  unit/trust_test.cpp
  unit/validation_test.cpp
  unit/workload_model_test.cpp
)
# Catch2 defaults to __COUNTER__ for unique test names, which Clang reports as a
# C2y extension under -Wpedantic. Line-based names keep WERROR builds clean.
target_compile_definitions(ztick_tests PRIVATE CATCH_CONFIG_NO_COUNTER)
if(UNIX)
  add_dependencies(ztick_tests ztick_process_helper)
  target_compile_definitions(ztick_tests
    PRIVATE
      ZTICK_TEST_POSIX_CHILD_PROCESS=1
      ZTICK_TEST_CHILD_HELPER_PATH="$<TARGET_FILE:ztick_process_helper>"
  )
endif()
target_link_libraries(ztick_tests
  PRIVATE
    Catch2::Catch2WithMain
    ztick::sdk
    ztick_cli
    ztick_core
    ztick_results
    ztick_plugin_host
    ztick_platform
    ztick_sysinfo_report
)

# Static adapter registration and the App A end-to-end harness need no loader.
if(ZTICK_BUILD_EXAMPLES)
  target_sources(ztick_tests
    PRIVATE
      unit/static_adapter_registry_test.cpp
      unit/static_adapter_app_a_test.cpp
  )
  target_link_libraries(ztick_tests PRIVATE ztick_test_app_static_harness)
endif()

# App B export and harness inspection needs the optional example targets and the
# platform loader. Those sources are added only when the examples exist, so builds
# with -DZTICK_BUILD_EXAMPLES=OFF do not reference those targets.
if(ZTICK_BUILD_EXAMPLES AND UNIX AND NOT APPLE)
  target_sources(ztick_tests
    PRIVATE
      unit/example_adapter_export_test.cpp
      unit/dynamic_adapter_app_b_test.cpp
      integration/phase3_integrations_test.cpp
  )
  target_link_libraries(ztick_tests
    PRIVATE
      ${CMAKE_DL_LIBS}
      ztick_test_app_b_harness
  )
  add_dependencies(ztick_tests ztick_test_app_dynamic)
  target_compile_definitions(ztick_tests
    PRIVATE
      ZTICK_DYNAMIC_EXAMPLE_PATH="$<TARGET_FILE:ztick_test_app_dynamic>"
      ZTICK_DYNAMIC_APP_B_PATH="$<TARGET_FILE:ztick_test_app_dynamic>"
  )

  # The dynamic module exposes lifecycle callbacks but must not link the SDK runtime,
  # so the host owns the only registry.
  find_program(ZTICK_NM_PROGRAM nm)
  if(ZTICK_NM_PROGRAM)
    add_test(
      NAME unit.sdk.dynamic_no_runtime_symbols
      COMMAND "${CMAKE_COMMAND}"
              "-DMODULE_PATH=$<TARGET_FILE:ztick_test_app_dynamic>"
              "-DNM_PROGRAM=${ZTICK_NM_PROGRAM}"
              -P "${PROJECT_SOURCE_DIR}/tests/check_dynamic_adapter_symbols.cmake"
    )
  endif()
endif()

catch_discover_tests(ztick_tests)

add_executable(ztick_version_c11_test unit/version_header_c11_test.c)
target_include_directories(ztick_version_c11_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.version.c11 COMMAND ztick_version_c11_test)

add_executable(ztick_version_cpp20_test unit/version_header_cpp20_test.cpp)
target_include_directories(ztick_version_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.version.cpp20 COMMAND ztick_version_cpp20_test)

add_executable(ztick_types_c11_test unit/types_header_c11_test.c)
target_include_directories(ztick_types_c11_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.types.c11 COMMAND ztick_types_c11_test)

add_executable(ztick_types_cpp20_test unit/types_header_cpp20_test.cpp)
target_include_directories(ztick_types_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.types.cpp20 COMMAND ztick_types_cpp20_test)

add_executable(ztick_sdk_c11_test unit/sdk_header_c11_test.c)
target_include_directories(ztick_sdk_c11_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.sdk.c11 COMMAND ztick_sdk_c11_test)

add_executable(ztick_sdk_cpp20_test unit/sdk_header_cpp20_test.cpp)
target_include_directories(ztick_sdk_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.sdk.cpp20 COMMAND ztick_sdk_cpp20_test)

# Link-level C11 check for the compiled SDK runtime (Phase 3 preparation).
add_executable(ztick_sdk_runtime_c11_test unit/sdk_runtime_c11_test.c)
target_link_libraries(ztick_sdk_runtime_c11_test PRIVATE ztick::sdk)
add_test(NAME unit.sdk.runtime.c11 COMMAND ztick_sdk_runtime_c11_test)

add_executable(ztick_cpp_wrapper_cpp20_test unit/cpp_wrapper_cpp20_test.cpp)
target_include_directories(ztick_cpp_wrapper_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.cpp_wrapper.cpp20 COMMAND ztick_cpp_wrapper_cpp20_test)

# P1.13: verify the installed developer tool prints a complete inventory whose
# lines all carry an explicit availability state.
add_test(
  NAME integration.sysinfo_output
  COMMAND "${CMAKE_COMMAND}"
          "-DSYSINFO_BINARY=$<TARGET_FILE:ztick-sysinfo>"
          -P "${PROJECT_SOURCE_DIR}/tests/check_sysinfo_output.cmake"
)

# P4.5: verify the executable returns the documented CI exit codes and writes
# the dispatch text to the expected stream.
add_test(
  NAME integration.cli_dispatch
  COMMAND "${CMAKE_COMMAND}"
          "-DCLI_BINARY=$<TARGET_FILE:ztick-bench>"
          -P "${PROJECT_SOURCE_DIR}/tests/check_cli_dispatch.cmake"
)

# P4.6: verify the executable executes a config, applies overrides and writes a
# portable result atomically.
add_test(
  NAME integration.cli_run
  COMMAND "${CMAKE_COMMAND}"
          "-DCLI_BINARY=$<TARGET_FILE:ztick-bench>"
          -P "${PROJECT_SOURCE_DIR}/tests/check_cli_run.cmake"
)

# BR §17.8: verify an out-of-tree consumer with find_package(ztick-sdk).
add_test(
  NAME integration.sdk_consumer
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/check_sdk_consumer.sh"
          "${CMAKE_BINARY_DIR}/sdk-consumer-check"
)

# BR §17.8 and P3.9: build and run both independent integrations entirely
# outside the source tree against the installed SDK package.
add_test(
  NAME integration.external_apps
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/check_external_apps.sh"
          "${CMAKE_BINARY_DIR}/external-apps-check"
)
