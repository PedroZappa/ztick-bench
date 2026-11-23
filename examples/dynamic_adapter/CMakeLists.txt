# SPDX-License-Identifier: MIT
# Phase 3 dynamic SDK test application (metadata-only export scaffold).
# BR §17.2 requires the full harness; P3.8 supplies its lifecycle callbacks.

add_library(ztick_test_app_dynamic MODULE
  dynamic_adapter.cpp
)

target_include_directories(ztick_test_app_dynamic
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

# Deliberately not linked to ztick::sdk. A dynamic adapter exposes read-only
# metadata only (BR §6.1) and must not embed a second SDK runtime: the host owns
# the single registry (SDK_SPECIFICATION §10). Keep this module header-only; the
# host performs every version and compatibility check. P3.8 adds lifecycle
# callbacks without changing that ownership boundary.

set_target_properties(ztick_test_app_dynamic PROPERTIES
  PREFIX ""
)
