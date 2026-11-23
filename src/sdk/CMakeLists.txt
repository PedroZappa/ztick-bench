# SPDX-License-Identifier: MIT
# Compiled SDK runtime (Phase 3 preparation). The public C ABI headers stay
# header-only for adapters; the host-side runtime symbols need definitions.

add_library(ztick_sdk STATIC
  sdk.cpp
)
add_library(ztick::sdk ALIAS ztick_sdk)

set_target_properties(ztick_sdk PROPERTIES EXPORT_NAME sdk)

target_include_directories(ztick_sdk
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Export decoration for the C ABI when building on Windows (see <ztick/sdk.h>).
target_compile_definitions(ztick_sdk PRIVATE ZTICK_BUILDING_SDK)
