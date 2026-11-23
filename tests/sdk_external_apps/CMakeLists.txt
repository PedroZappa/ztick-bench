# SPDX-License-Identifier: MIT
# Out-of-tree integration fixture for BR §17.8 and ROADMAP P3.9. This is a
# standalone project: it is never added as a subdirectory of ztick-bench and
# builds only from the installed ztick-sdk package, using no source-tree paths
# and no private ztick-bench targets.
cmake_minimum_required(VERSION 3.16)

project(ztick_external_apps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ztick-sdk 1.0 REQUIRED)
include(CTest)

# App A: a static integration built from one source file. The descriptor is
# registered through the public C ABI and driven directly by its own host.
add_executable(ztick_external_static static_app/static_app.cpp)
target_link_libraries(ztick_external_static PRIVATE ztick::sdk)
add_test(NAME external.static_app COMMAND ztick_external_static)

# App B: a shared module that exports the read-only descriptor without linking
# the SDK runtime, plus a separate host that loads it with the platform loader.
# Both use only installed public headers.
if(UNIX)
  add_library(ztick_external_dynamic_module MODULE dynamic_app/module.cpp)
  # Reuse the installed package's include path without linking the SDK runtime:
  # a dynamic module must expose data and callbacks only (SDK_SPECIFICATION §10).
  target_include_directories(ztick_external_dynamic_module
    PRIVATE
      $<TARGET_PROPERTY:ztick::sdk,INTERFACE_INCLUDE_DIRECTORIES>
  )
  set_target_properties(ztick_external_dynamic_module PROPERTIES PREFIX "")

  add_executable(ztick_external_dynamic_host dynamic_app/host.cpp)
  target_link_libraries(ztick_external_dynamic_host PRIVATE ztick::sdk ${CMAKE_DL_LIBS})
  add_dependencies(ztick_external_dynamic_host ztick_external_dynamic_module)
  add_test(
    NAME external.dynamic_app
    COMMAND ztick_external_dynamic_host $<TARGET_FILE:ztick_external_dynamic_module>
  )
endif()
