# SPDX-License-Identifier: MIT
# Results, config and comparison library.
# Phase 4 populates the implementation; P1.2 establishes the target.

add_library(ztick_results STATIC
  results.cpp
)

target_include_directories(ztick_results
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ztick_results
  PUBLIC
    ztick_sdk
)
