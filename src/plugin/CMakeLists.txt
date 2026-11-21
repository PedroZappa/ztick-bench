# SPDX-License-Identifier: MIT
# Plugin discovery, trust, loader and runner host support.
# Phase 3 populates the implementation; P1.2 establishes the target.

add_library(ztick_plugin_host STATIC
  plugin.cpp
)

target_include_directories(ztick_plugin_host
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ztick_plugin_host
  PUBLIC
    ztick_platform
    ztick_sdk
)
