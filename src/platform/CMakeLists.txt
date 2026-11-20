# SPDX-License-Identifier: MIT
# Platform abstraction and OS-selected implementations.
# Phase 1.10–1.11 populate the implementation; P1.2 establishes the target.

add_library(ztick_platform STATIC
  linux/platform.cpp
)

target_include_directories(ztick_platform
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/linux"
)

# Links to ztick_sdk are added with the SDK target in P1.7.
