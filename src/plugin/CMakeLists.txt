# SPDX-License-Identifier: MIT
# Plugin discovery, trust, loader and runner host support.
# P3.2 adds the dynamic loader with metadata-only inspection; P3.4 adds the
# trusted-plugin list and first-run consent. Runners remain later Phase 3 work.
# Platform loading and configuration paths stay behind private abstractions.

if(UNIX)
  set(ZTICK_PLATFORM_LOADER_SOURCE posix/dynamic_library.cpp)
  set(ZTICK_PLATFORM_TRUST_SOURCE posix/trust_path.cpp)
else()
  set(ZTICK_PLATFORM_LOADER_SOURCE unsupported/dynamic_library.cpp)
  set(ZTICK_PLATFORM_TRUST_SOURCE unsupported/trust_path.cpp)
endif()

add_library(ztick_plugin_host STATIC
  plugin.cpp
  compatibility.cpp
  dynamic_loader.cpp
  trust.cpp
  ${ZTICK_PLATFORM_LOADER_SOURCE}
  ${ZTICK_PLATFORM_TRUST_SOURCE}
)

target_include_directories(ztick_plugin_host
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ztick_plugin_host
  PUBLIC
    ztick_platform
    ztick_sdk
)

if(UNIX)
  target_link_libraries(ztick_plugin_host PRIVATE ${CMAKE_DL_LIBS})
endif()
