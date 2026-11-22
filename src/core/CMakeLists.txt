# SPDX-License-Identifier: MIT
# Core engine library (workload, timing, statistics, validation).
# P1.2 establishes the target; P2.1 adds config-shaped workload/profile models;
# P2.2 adds deterministic input generators; P2.3 adds execution sequencing;
# P2.6 adds metric formulas and aggregates; P2.7 adds the validation phase.

add_library(ztick_core STATIC
  config_value.cpp
  core.cpp
  execution.cpp
  generator.cpp
  profile.cpp
  statistics.cpp
  validation.cpp
  workload.cpp
)

target_include_directories(ztick_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ztick_core
  PUBLIC
    ztick_sdk
)
