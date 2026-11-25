# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

project(ztick-bench
  VERSION 0.5.0
  DESCRIPTION "Portable DSP benchmarking framework"
  LANGUAGES C CXX
)

# CMake's VERSION field is numeric-only; the development suffix is carried
# separately so the displayed tool version is "0.5.0-dev" (BR §21).
set(ZTICK_VERSION_SUFFIX "dev")
set(ZTICK_VERSION_DISPLAY "0.5.0-${ZTICK_VERSION_SUFFIX}")
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

# The optional private front-end boundary is a shared library that links the
# static core/CLI archives. Shared linking requires PIC in those archives; this
# does not change any installed SDK header or the public C ABI (SDK §14).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

# Build metadata for attached result documents (BR §11.1, RESULTS_FORMAT §4.1).
# Values are discovered at configure time and compiled into ztick_results. Every
# missing fact is stored as "unavailable" rather than omitted.
find_package(Git QUIET)
set(ZTICK_BUILD_GIT_COMMIT "unavailable")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE _ztick_git_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _ztick_git_result
  )
  if(_ztick_git_result EQUAL 0 AND _ztick_git_commit)
    set(ZTICK_BUILD_GIT_COMMIT "${_ztick_git_commit}")
  endif()
endif()

set(ZTICK_BUILD_COMPILER "${CMAKE_CXX_COMPILER_ID}")
if(NOT ZTICK_BUILD_COMPILER)
  set(ZTICK_BUILD_COMPILER "unavailable")
endif()
set(ZTICK_BUILD_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
if(NOT ZTICK_BUILD_COMPILER_VERSION)
  set(ZTICK_BUILD_COMPILER_VERSION "unavailable")
endif()

set(ZTICK_BUILD_CONFIGURATION "${CMAKE_BUILD_TYPE}")
if(NOT ZTICK_BUILD_CONFIGURATION)
  set(ZTICK_BUILD_CONFIGURATION "unavailable")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _ztick_build_type_upper)
set(ZTICK_BUILD_FLAGS "${CMAKE_CXX_FLAGS}")
if(_ztick_build_type_upper)
  set(ZTICK_BUILD_FLAGS "${ZTICK_BUILD_FLAGS} ${CMAKE_CXX_FLAGS_${_ztick_build_type_upper}}")
endif()
string(STRIP "${ZTICK_BUILD_FLAGS}" ZTICK_BUILD_FLAGS)
if(NOT ZTICK_BUILD_FLAGS)
  set(ZTICK_BUILD_FLAGS "unavailable")
endif()
set(ZTICK_BUILD_PROJECT_VERSION "${ZTICK_VERSION_DISPLAY}")

option(ZTICK_WERROR "Treat compiler warnings as errors (enable in CI)" OFF)
option(ZTICK_ENABLE_SANITIZERS "Build with ASan and UBSan (use with Debug builds)" OFF)
# The Rust TUI, its private C11 facade and Cargo are optional: core/CLI-only users
# need no Rust toolchain and can configure with -DZTICK_BUILD_TUI=OFF (BR §21).
option(ZTICK_BUILD_TUI "Build the optional Rust TUI and private front-end boundary" ON)

if(ZTICK_BUILD_TUI)
  find_program(ZTICK_CARGO_EXECUTABLE cargo)
  if(NOT ZTICK_CARGO_EXECUTABLE)
    message(FATAL_ERROR
      "ZTICK_BUILD_TUI=ON requires cargo; configure with -DZTICK_BUILD_TUI=OFF for core/CLI-only builds")
  endif()
endif()

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

option(ZTICK_BUILD_EXAMPLES "Build the SDK test applications" ON)

# Source-tree targets (ARCHITECTURE §3.1). Per-directory CMakeLists.txt files
# declare the targets; later phases populate their implementations. The SDK target
# was already added above because install/export rules need it.
add_subdirectory(src/results)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/plugin)
add_subdirectory(src/cli)
if(ZTICK_BUILD_TUI)
  add_subdirectory(src/frontend)
endif()
add_subdirectory(src/tui)

# Phase 1 developer tool; prints the inventory with explicit availability states.
add_subdirectory(tools/ztick-sysinfo)

# Optional SDK test applications (Phase 3, BR §17.2). App A is a complete static
# harness; App B is a complete dynamic engine harness. Added before tests so CTest
# can exercise both example targets.
if(ZTICK_BUILD_EXAMPLES)
  add_subdirectory(examples/static_adapter)
  add_subdirectory(examples/dynamic_adapter)
endif()

# Standalone public-header checks are built and registered only with testing on.
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
