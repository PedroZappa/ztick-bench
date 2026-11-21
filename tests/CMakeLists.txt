# SPDX-License-Identifier: MIT
# Standalone public-header checks (P1.3). These translation units have no test
# framework dependency so they prove each header is self-contained in its target
# language. Catch2 v3 (P1.8) is reserved for behavioral unit tests.

# Catch2 v3 is dev-only (REQ-TEST-04). Prefer an installed package; fall back to
# a pinned FetchContent checkout so the version is reproducible.
find_package(Catch2 3 QUIET CONFIG)
if(Catch2_FOUND)
  list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
else()
  include(FetchContent)
  set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(CATCH_INSTALL_HELPERS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 644821ce28cb25d7992a4d0375b1d83214392592 # v3.9.1
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(Catch)

add_executable(ztick_tests unit/catch2_integration_test.cpp)
# Catch2 defaults to __COUNTER__ for unique test names, which Clang reports as a
# C2y extension under -Wpedantic. Line-based names keep WERROR builds clean.
target_compile_definitions(ztick_tests PRIVATE CATCH_CONFIG_NO_COUNTER)
target_link_libraries(ztick_tests
  PRIVATE
    Catch2::Catch2WithMain
    ztick::sdk
)
catch_discover_tests(ztick_tests)

add_executable(ztick_version_c11_test unit/version_header_c11_test.c)
target_include_directories(ztick_version_c11_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.version.c11 COMMAND ztick_version_c11_test)

add_executable(ztick_version_cpp20_test unit/version_header_cpp20_test.cpp)
target_include_directories(ztick_version_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.version.cpp20 COMMAND ztick_version_cpp20_test)

add_executable(ztick_types_c11_test unit/types_header_c11_test.c)
target_include_directories(ztick_types_c11_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.types.c11 COMMAND ztick_types_c11_test)

add_executable(ztick_types_cpp20_test unit/types_header_cpp20_test.cpp)
target_include_directories(ztick_types_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.types.cpp20 COMMAND ztick_types_cpp20_test)

add_executable(ztick_sdk_c11_test unit/sdk_header_c11_test.c)
target_include_directories(ztick_sdk_c11_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.sdk.c11 COMMAND ztick_sdk_c11_test)

add_executable(ztick_sdk_cpp20_test unit/sdk_header_cpp20_test.cpp)
target_include_directories(ztick_sdk_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.sdk.cpp20 COMMAND ztick_sdk_cpp20_test)

add_executable(ztick_cpp_wrapper_cpp20_test unit/cpp_wrapper_cpp20_test.cpp)
target_include_directories(ztick_cpp_wrapper_cpp20_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
add_test(NAME unit.cpp_wrapper.cpp20 COMMAND ztick_cpp_wrapper_cpp20_test)

# BR §17.8: verify an out-of-tree consumer with find_package(ztick-sdk).
add_test(
  NAME integration.sdk_consumer
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/check_sdk_consumer.sh"
          "${CMAKE_BINARY_DIR}/sdk-consumer-check"
)
