# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

project(ztick-bench
  VERSION 0.1.0
  DESCRIPTION "Portable DSP benchmarking framework"
  LANGUAGES C CXX
)

# CMake's VERSION field is numeric-only; the development suffix is carried
# separately so the displayed tool version is "0.1.0-dev" (BR §22).
set(ZTICK_VERSION_SUFFIX "dev")
set(ZTICK_VERSION_DISPLAY "${PROJECT_VERSION}-${ZTICK_VERSION_SUFFIX}")
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

# Defines BUILD_TESTING and enables CTest. Test registration starts at P1.8.
include(CTest)

# Source-tree targets (ARCHITECTURE §3.1). Per-directory CMakeLists.txt files
# declare the targets; later phases populate their implementations.
add_subdirectory(src/results)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/plugin)
add_subdirectory(src/cli)
add_subdirectory(src/tui)

# Optional example SDK test applications (Phase 3 implementations, BR §17.2).
option(ZTICK_BUILD_EXAMPLES "Build placeholder/example SDK test applications" ON)
if(ZTICK_BUILD_EXAMPLES)
  add_subdirectory(examples/static_adapter)
  add_subdirectory(examples/dynamic_adapter)
endif()
