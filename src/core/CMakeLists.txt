# SPDX-License-Identifier: MIT
# Core engine library (workload, timing, statistics, validation).
# P1.2 establishes the target; P2.1 adds config-shaped workload/profile models.

add_library(ztick_core STATIC
  config_value.cpp
  core.cpp
  profile.cpp
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
