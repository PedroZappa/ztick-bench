# SPDX-License-Identifier: MIT
# CLI command-layer library. The ztick-bench executable lands in Phase 4.
# P1.2 establishes the library target from a placeholder source.

add_library(ztick_cli STATIC
  cli.cpp
)

target_include_directories(ztick_cli
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ztick_cli
  PUBLIC
    ztick_core
    ztick_results
    ztick_platform
)
