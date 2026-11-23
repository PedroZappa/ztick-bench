# SPDX-License-Identifier: MIT
# Verify the ztick-bench executable exposes the documented commands and exit
# codes. Dispatch is unit-tested separately; this script proves the thin main()
# writes the right streams and returns the code to the shell (P4.5).

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

check_cli(0 "Usage:" --help)
check_cli(0 "ztick-bench" --version)
check_cli(0 "Built-in profiles:" list)
check_cli(0 "Comprehensive" list profiles)
check_cli(1 "unknown command" benchmark)
check_cli(1 "missing command")
check_cli(2 "not available" system)
check_cli(2 "not available" run config.json)
check_cli(2 "not available" plugin list)
