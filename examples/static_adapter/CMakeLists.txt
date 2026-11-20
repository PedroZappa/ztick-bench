# SPDX-License-Identifier: MIT
# Phase 3 static SDK test application (placeholder).
# Required by BR §17.2; implemented in Phase 3.

add_library(ztick_test_app_static STATIC
  static_adapter.cpp
)

target_include_directories(ztick_test_app_static
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
