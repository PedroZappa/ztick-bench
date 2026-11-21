# SPDX-License-Identifier: MIT
# Phase 1 developer system-info executable (P1.13). Rendering is a separate
# library so its output can be unit-tested with injected inventory data.

add_library(ztick_sysinfo_report STATIC
  sysinfo_report.cpp
)

target_include_directories(ztick_sysinfo_report
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(ztick_sysinfo_report
  PUBLIC
    ztick_platform
)

add_executable(ztick-sysinfo
  main.cpp
)

target_link_libraries(ztick-sysinfo
  PRIVATE
    ztick_sysinfo_report
)
