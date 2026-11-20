# SPDX-License-Identifier: MIT
# Optional TUI front-end library.
# Phase 5 populates the implementation; P1.2 establishes the target.

option(ZTICK_BUILD_TUI "Build the optional TUI front end" ON)

if(ZTICK_BUILD_TUI)
  add_library(ztick_tui STATIC
    tui.cpp
  )

  target_include_directories(ztick_tui
    PUBLIC
      "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
  )

  target_link_libraries(ztick_tui
    PUBLIC
      ztick_core
      ztick_results
      ztick_cli
  )
endif()
