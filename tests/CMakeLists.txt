# SPDX-License-Identifier: MIT
# Standalone public-header checks (P1.3). These translation units have no test
# framework dependency so they prove each header is self-contained in its target
# language; Catch2-based unit tests are added in P1.8.

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
