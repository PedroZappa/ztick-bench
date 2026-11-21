# SPDX-License-Identifier: MIT
# Platform abstraction and OS-selected implementations.
# P1.10 defines the portable interface; P1.11 implements Linux system-info
# collection and P1.12 tests its missing/unsupported paths. P1.2 established
# the target.

add_library(ztick_platform STATIC
  linux/platform.cpp
  linux/system_info.cpp
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
