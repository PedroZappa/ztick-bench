# SPDX-License-Identifier: MIT
# Core engine library (workload, timing, statistics, validation).
# Phase 2 populates the implementation; P1.2 establishes the target.

add_library(ztick_core STATIC
  core.cpp
)

target_include_directories(ztick_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Links to ztick_sdk are added with the SDK target in P1.7.
