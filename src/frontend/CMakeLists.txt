# SPDX-License-Identifier: MIT
# Private C11 facade consumed only by the optional Rust TUI front end (P5.0).
# It is never installed or exported to third-party SDK consumers.

add_library(ztick_frontend SHARED
  frontend.cpp
)

set_target_properties(ztick_frontend PROPERTIES
  OUTPUT_NAME ztick_frontend
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
)

target_compile_definitions(ztick_frontend PRIVATE ZTICK_BUILDING_FRONTEND)

target_include_directories(ztick_frontend
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_SOURCE_DIR}/src/cli/include"
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/plugin/include"
    "${CMAKE_SOURCE_DIR}/src/results/include"
)

target_link_libraries(ztick_frontend
  PRIVATE
    ztick_cli
    ztick_core
    ztick_results
    ztick_plugin_host
)
