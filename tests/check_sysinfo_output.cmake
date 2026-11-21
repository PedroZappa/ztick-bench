# SPDX-License-Identifier: MIT
# Verify the ztick-sysinfo executable prints the complete inventory and that
# every printed line carries an explicit availability state (P1.13).

if(NOT DEFINED SYSINFO_BINARY OR NOT EXISTS "${SYSINFO_BINARY}")
  message(FATAL_ERROR "SYSINFO_BINARY must name the ztick-sysinfo executable")
endif()

execute_process(
  COMMAND "${SYSINFO_BINARY}"
  RESULT_VARIABLE status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT status EQUAL 0)
  message(FATAL_ERROR "ztick-sysinfo exited with ${status}: ${errors}")
endif()

# Semicolons are list separators in CMake; escape them before splitting lines so
# rendered cache and metadata values stay inside their own line.
string(REPLACE ";" "\\;" output "${output}")
string(REPLACE "\n" ";" lines "${output}")
set(line_count 0)
foreach(line IN LISTS lines)
  if(line STREQUAL "")
    continue()
  endif()
  math(EXPR line_count "${line_count}+1")
  if(NOT line MATCHES "^[^:]+: (value|unavailable|unsupported|not_applicable)( |$)")
    message(FATAL_ERROR "Line ${line_count} lacks an explicit availability state: ${line}")
  endif()
endforeach()

if(NOT line_count EQUAL 17)
  message(FATAL_ERROR "Expected 17 inventory lines, found ${line_count}")
endif()
