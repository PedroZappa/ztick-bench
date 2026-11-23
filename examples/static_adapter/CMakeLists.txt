# SPDX-License-Identifier: MIT
# Phase 3 static SDK test application. P3.1 adds the registration entry point;
# BR §17.2 requires the full harness, whose lifecycle callbacks arrive in P3.7.

add_library(ztick_test_app_static STATIC
  static_adapter.cpp
  static_adapter.hpp
)

target_include_directories(ztick_test_app_static
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(ztick_test_app_static
  PRIVATE
    ztick::sdk
)
