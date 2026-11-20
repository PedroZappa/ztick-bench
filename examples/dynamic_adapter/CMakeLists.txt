# SPDX-License-Identifier: MIT
# Phase 3 dynamic SDK test application (placeholder).
# Required by BR §17.2; implemented in Phase 3.

add_library(ztick_test_app_dynamic MODULE
  dynamic_adapter.cpp
)

target_include_directories(ztick_test_app_dynamic
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

set_target_properties(ztick_test_app_dynamic PROPERTIES
  PREFIX ""
)
