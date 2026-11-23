# SPDX-License-Identifier: MIT
# Results, config and comparison library.
# P1.2 establishes the target; P4.1 adds the config-shaped value tree, the
# dependency-free JSON layer and the portable config document schema/parser.

add_library(ztick_results STATIC
  config_document.cpp
  config_value.cpp
  json.cpp
  results.cpp
)

target_include_directories(ztick_results
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ztick_results
  PUBLIC
    ztick_sdk
)
