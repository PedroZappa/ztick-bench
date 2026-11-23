# SPDX-License-Identifier: MIT
# Phase 3 static SDK test application (metadata-only scaffold).
# BR §17.2 requires the full harness; P3.7 supplies its lifecycle callbacks.

add_library(ztick_test_app_static STATIC
  static_adapter.cpp
)

target_include_directories(ztick_test_app_static
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(ztick_test_app_static
  PRIVATE
    ztick::sdk
)
