# SPDX-License-Identifier: MIT
# Platform abstraction and OS-selected implementations.
# P1.10 defines the portable interface; P1.11–1.12 populate and test the
# Linux implementation. P1.2 established the target.

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

target_link_libraries(ztick_platform
  PUBLIC
    ztick_sdk
)
