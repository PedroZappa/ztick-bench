# SPDX-License-Identifier: MIT
# Results, config and comparison library.
# P1.2 establishes the target; P4.1 adds the config-shaped value tree, the
# dependency-free JSON layer and the portable config document schema/parser.
# P4.2 adds the result document schema, status derivation and atomic writer.

add_library(ztick_results STATIC
  atomic_writer.cpp
  config_document.cpp
  config_value.cpp
  json.cpp
  result_document.cpp
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
