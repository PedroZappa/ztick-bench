# SPDX-License-Identifier: MIT
# CLI command layer and the ztick-bench executable. The dispatch module owns
# argument routing, output formats and exit codes; the executable only writes the
# returned streams. P4.10 adds plugin commands and the JSON output envelope.

add_library(ztick_cli STATIC
  cli.cpp
  plugin_commands.cpp
  result_commands.cpp
  run_command.cpp
  system_command.cpp
)

target_include_directories(ztick_cli
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Captured at configure time so --version agrees with the result build metadata.
target_compile_definitions(ztick_cli
  PUBLIC
    ZTICK_CLI_VERSION="${ZTICK_VERSION_DISPLAY}"
)

target_link_libraries(ztick_cli
  PUBLIC
    ztick_core
    ztick_results
    ztick_platform
    ztick_plugin_host
)

add_executable(ztick-bench
  main.cpp
)

target_link_libraries(ztick-bench
  PRIVATE
    ztick_cli
)
