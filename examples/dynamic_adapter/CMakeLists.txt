# SPDX-License-Identifier: MIT
# Dynamic SDK test app B. The module exposes read-only descriptor data plus the
# lifecycle callbacks needed by the host harness (BR §17.2, SDK_SPECIFICATION §10).
# The module never links ztick::sdk; the host owns the single SDK runtime and registry.

find_package(Threads REQUIRED)

add_library(ztick_test_app_dynamic MODULE
  dynamic_adapter.cpp
  app_b_contract.hpp
)

target_include_directories(ztick_test_app_dynamic
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Deliberately not linked to ztick::sdk. A dynamic adapter exposes a read-only
# descriptor and lifecycle ops but must not embed a second SDK runtime: the host owns
# the single registry and performs every version and compatibility check.
target_link_libraries(ztick_test_app_dynamic PRIVATE Threads::Threads)

set_target_properties(ztick_test_app_dynamic PROPERTIES
  PREFIX ""
)

add_library(ztick_test_app_b_harness STATIC
  app_b_harness.cpp
  app_b_harness.hpp
  app_b_contract.hpp
)

target_include_directories(ztick_test_app_b_harness
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(ztick_test_app_b_harness
  PRIVATE
    ztick::sdk
    ztick_plugin_host
    Threads::Threads
)

add_executable(ztick_test_app_b app_b_main.cpp)
target_link_libraries(ztick_test_app_b
  PRIVATE
    ztick_test_app_b_harness
    ztick_plugin_host
)
add_dependencies(ztick_test_app_b ztick_test_app_dynamic)

add_test(
  NAME integration.sdk.app_b_in_process
  COMMAND ztick_test_app_b
          in_process
          --module $<TARGET_FILE:ztick_test_app_dynamic>
)

if(UNIX)
  add_test(
    NAME integration.sdk.app_b_out_of_process
    COMMAND ztick_test_app_b
            out_of_process
            --module $<TARGET_FILE:ztick_test_app_dynamic>
            --child-executable $<TARGET_FILE:ztick_test_app_b>
  )
endif()
