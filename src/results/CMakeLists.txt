# SPDX-License-Identifier: MIT
# Results, config and comparison library.
# P1.2 establishes the target; P4.1 adds the config-shaped value tree, the
# dependency-free JSON layer and the portable config document schema/parser.
# P4.2 adds the result document schema, status derivation and atomic writer.
# P4.3 adds the convenience CSV projection of stored result documents.
# P4.4 adds availability-aware metadata attachment and comparability checks.

add_library(ztick_results STATIC
  atomic_writer.cpp
  config_document.cpp
  config_value.cpp
  csv_projection.cpp
  json.cpp
  metadata.cpp
  result_document.cpp
  results.cpp
)

# Build facts are captured at configure time and compiled into metadata.cpp.
# Missing facts are passed as the literal "unavailable" (BR §11.1).
target_compile_definitions(ztick_results PRIVATE
  ZTICK_BUILD_GIT_COMMIT="${ZTICK_BUILD_GIT_COMMIT}"
  ZTICK_BUILD_COMPILER="${ZTICK_BUILD_COMPILER}"
  ZTICK_BUILD_COMPILER_VERSION="${ZTICK_BUILD_COMPILER_VERSION}"
  ZTICK_BUILD_FLAGS="${ZTICK_BUILD_FLAGS}"
  ZTICK_BUILD_CONFIGURATION="${ZTICK_BUILD_CONFIGURATION}"
  ZTICK_BUILD_PROJECT_VERSION="${ZTICK_BUILD_PROJECT_VERSION}"
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
