# SPDX-License-Identifier: MIT
# Static SDK test app A. P3.1 added registration; P3.7 completes the processor
# and chain lifecycle and adds an executable host harness (BR §17.2).

add_library(ztick_test_app_static STATIC
  static_adapter.cpp
  static_adapter.hpp
  app_a_contract.hpp
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

add_library(ztick_test_app_static_harness STATIC
  app_a_harness.cpp
  app_a_harness.hpp
  app_a_contract.hpp
)

target_include_directories(ztick_test_app_static_harness
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(ztick_test_app_static_harness
  PUBLIC
    ztick_test_app_static
    ztick::sdk
)

add_executable(ztick_test_app_a app_a_main.cpp)
target_link_libraries(ztick_test_app_a
  PRIVATE
    ztick_test_app_static_harness
)

add_test(NAME integration.sdk.app_a_static COMMAND ztick_test_app_a)
