# SPDX-License-Identifier: MIT
# Verify the ztick-bench executable exposes the documented commands, output
# formats and exit codes. Dispatch is unit-tested separately; this script proves
# the thin main() writes the right streams and returns the code to the shell
# (P4.5, P4.10).

if(NOT DEFINED CLI_BINARY OR NOT EXISTS "${CLI_BINARY}")
  message(FATAL_ERROR "CLI_BINARY must name the ztick-bench executable")
endif()

function(check_cli expected_code expected_text)
  execute_process(
    COMMAND "${CLI_BINARY}" ${ARGN}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT status EQUAL expected_code)
    message(FATAL_ERROR
      "ztick-bench ${ARGN}: expected exit ${expected_code}, got ${status}: ${errors}")
  endif()
  if(NOT "${output}${errors}" MATCHES "${expected_text}")
    message(FATAL_ERROR
      "ztick-bench ${ARGN}: expected text '${expected_text}', got stdout='${output}' stderr='${errors}'")
  endif()
endfunction()

# Verify a machine-readable envelope: JSON on stdout, empty stderr and the stable
# header fields from RESULTS_FORMAT §12.
function(check_json_cli expected_code expected_command expected_status)
  execute_process(
    COMMAND "${CLI_BINARY}" --format json ${ARGN}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT status EQUAL expected_code)
    message(FATAL_ERROR
      "ztick-bench --format json ${ARGN}: expected exit ${expected_code}, got ${status}: ${output}${errors}")
  endif()
  if(NOT errors STREQUAL "")
    message(FATAL_ERROR
      "ztick-bench --format json ${ARGN}: expected empty stderr, got '${errors}'")
  endif()
  foreach(fragment
          "\"schema_version\":1"
          "\"command\":\"${expected_command}\""
          "\"status\":\"${expected_status}\""
          "\"exit_code\":${expected_code}"
          "\"data\":"
          "\"error\":")
    if(NOT output MATCHES "${fragment}")
      message(FATAL_ERROR
        "ztick-bench --format json ${ARGN}: missing '${fragment}' in '${output}'")
    endif()
  endforeach()
endfunction()

check_cli(0 "Usage:" --help)
check_cli(0 "ztick-bench" --version)
check_cli(0 "Built-in profiles:" list)
check_cli(0 "Comprehensive" list profiles)
check_cli(1 "unknown command" benchmark)
check_cli(1 "missing command")
check_cli(0 "availability states" system)
check_cli(1 "run requires a config path" run)
check_cli(1 "report requires exactly one result path" report)
check_cli(1 "compare requires a baseline and a candidate path" compare a.json)

# P4.10: plugin commands are metadata-only and available without a running plugin.
check_cli(0 "discovered plugins" plugin list)
check_cli(1 "requires exactly one path" plugin inspect)
check_cli(1 "accepts only --path options" plugin list stray.so)

# P4.10: the JSON envelope wraps success, failure and usage outcomes on stdout.
check_json_cli(0 list success list profiles)
check_json_cli(3 report failure report missing.json)
check_json_cli(1 compare usage_error compare only-one.json)
check_json_cli(0 "plugin list" success plugin list)
