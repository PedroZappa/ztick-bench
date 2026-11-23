# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

project(ztick-bench
  VERSION 0.2.0
  DESCRIPTION "Portable DSP benchmarking framework"
  LANGUAGES C CXX
)

# CMake's VERSION field is numeric-only; the development suffix is carried
# separately so the displayed tool version is "0.2.0-dev" (BR §21).
set(ZTICK_VERSION_SUFFIX "dev")
set(ZTICK_VERSION_DISPLAY "${PROJECT_VERSION}-${ZTICK_VERSION_SUFFIX}")
# Tool maturity and the frozen C ABI contract are independent: the ABI version
# drives find_package compatibility, while the project version is informational.
set(ZTICK_SDK_ABI_VERSION "1.0.0")

message(STATUS "ztick-bench ${ZTICK_VERSION_DISPLAY} (SDK ABI ${ZTICK_SDK_ABI_VERSION})")

# Language levels (BR §18 Q46 confirmed).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler baseline from BR §22. AppleClang is handled in post-v1.0 expansion.
foreach(_ztick_lang C CXX)
  if(CMAKE_${_ztick_lang}_COMPILER_ID STREQUAL "GNU" AND
     CMAKE_${_ztick_lang}_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR
      "GCC 13+ is required for ${_ztick_lang}; found ${CMAKE_${_ztick_lang}_COMPILER_VERSION}")
  endif()
  if(CMAKE_${_ztick_lang}_COMPILER_ID STREQUAL "Clang" AND
     CMAKE_${_ztick_lang}_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR
      "Clang 16+ is required for ${_ztick_lang}; found ${CMAKE_${_ztick_lang}_COMPILER_VERSION}")
  endif()
endforeach()

option(ZTICK_WERROR "Treat compiler warnings as errors (enable in CI)" OFF)
option(ZTICK_ENABLE_SANITIZERS "Build with ASan and UBSan (use with Debug builds)" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(ZTICK_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

if(ZTICK_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR "ASan/UBSan require GCC or Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fsanitize-address-use-after-scope -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# Match the binary paths expected by the root Makefile.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Install and export rules for the SDK (P1.7, BR §17.8). The compiled target is
# declared in src/sdk/CMakeLists.txt and exported as ztick::sdk for find_package.
include(GNUInstallDirs)
add_subdirectory(src/sdk)

install(TARGETS ztick_sdk
  EXPORT ztick-sdk-targets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/ztick/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ztick"
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)

install(EXPORT ztick-sdk-targets
  FILE ztick-sdk-targets.cmake
  NAMESPACE ztick::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ztick-sdk"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/ztick-sdk-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ztick-sdk-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ztick-sdk"
)
# find_package requests the SDK ABI version (SDK_SPECIFICATION §2); the package
# config exposes the tool version separately as ztick-sdk_PROJECT_VERSION.
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ztick-sdk-config-version.cmake"
  VERSION "${ZTICK_SDK_ABI_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/ztick-sdk-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ztick-sdk-config-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ztick-sdk"
)

# Defines BUILD_TESTING and enables CTest. Test registration starts at P1.8.
include(CTest)

# Source-tree targets (ARCHITECTURE §3.1). Per-directory CMakeLists.txt files
# declare the targets; later phases populate their implementations. The SDK target
# was already added above because install/export rules need it.
add_subdirectory(src/results)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/plugin)
add_subdirectory(src/cli)
add_subdirectory(src/tui)

# Phase 1 developer tool; prints the inventory with explicit availability states.
add_subdirectory(tools/ztick-sysinfo)

# Optional metadata-only SDK test-app scaffolding (Phase 3, BR §17.2). Added
# before tests so CTest can exercise the example descriptors when they exist.
option(ZTICK_BUILD_EXAMPLES "Build SDK test-app scaffolding" ON)
if(ZTICK_BUILD_EXAMPLES)
  add_subdirectory(examples/static_adapter)
  add_subdirectory(examples/dynamic_adapter)
endif()

# Standalone public-header checks are built and registered only with testing on.
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
