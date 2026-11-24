# SPDX-License-Identifier: MIT
# Verify the ztick-bench executable executes a config, applies command-line
# overrides and writes a portable result (P4.6). Unit tests cover the parsing and
# failure branches; this script proves the thin executable path end to end.

if(NOT DEFINED CLI_BINARY OR NOT EXISTS "${CLI_BINARY}")
  message(FATAL_ERROR "CLI_BINARY must name the ztick-bench executable")
endif()

set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/cli_run_check")
set(CONFIG "${WORK_DIR}/config.json")
set(RESULT "${WORK_DIR}/result.json")
set(DEFAULT_RESULT "${WORK_DIR}/config.result.json")
set(INVALID_RESULT "${WORK_DIR}/invalid.result.json")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(REMOVE "${RESULT}" "${DEFAULT_RESULT}" "${INVALID_RESULT}")

file(WRITE "${CONFIG}" [=[
{
  "config_version": "1.0.0",
  "sdk_version": "1.0.0",
  "integration": { "id": "app-a", "version": "1.2.3" },
  "workload": {
    "level": "processor",
    "processors": ["eq"],
    "generator": {
      "kind": "sine",
      "amplitude": 0.5,
      "frequency_hz": 1000.0,
      "phase_rad": 0.0
    }
  },
  "dimensions": {
    "sample_rate_hz": 48000.0,
    "block_size": 128,
    "channels": 2,
    "oversampling": 1,
    "instances": 1
  },
  "execution": {
    "duration_mode": "iterations",
    "iterations": 2,
    "duration_seconds": 0.0,
    "repetitions": 1,
    "warmup": { "iterations": 0 },
    "isolation": "auto"
  },
  "profile": { "name": "Quick" },
  "measurement": {
    "detail": "aggregate",
    "end_to_end": false,
    "timing_levels": ["per_block", "whole_run"]
  },
  "validation": { "level": "Basic", "tolerances": {}, "references": [] },
  "parallelism": { "workers": "auto", "affinity": "auto" }
}
]=])

execute_process(
  COMMAND "${CLI_BINARY}" run "${CONFIG}"
          --set execution.iterations=6
          --set=profile.name=CLI_Override
          --output "${RESULT}"
  RESULT_VARIABLE status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "ztick-bench run failed with ${status}: ${output}${errors}")
endif()
if(NOT output MATCHES "run complete")
  message(FATAL_ERROR "ztick-bench run did not report completion: ${output}${errors}")
endif()
if(NOT EXISTS "${RESULT}")
  message(FATAL_ERROR "ztick-bench run did not write the requested result: ${RESULT}")
endif()

file(READ "${RESULT}" result_text)
string(FIND "${result_text}" "\"iterations\":6" iterations_position)
if(iterations_position LESS 0)
  message(FATAL_ERROR "result snapshot does not contain the iterations override")
endif()
string(FIND "${result_text}" "\"name\":\"CLI_Override\"" name_position)
if(name_position LESS 0)
  message(FATAL_ERROR "result snapshot does not contain the profile override")
endif()

execute_process(
  COMMAND "${CLI_BINARY}" run "${CONFIG}" --set execution.iterations=3
  RESULT_VARIABLE default_status
  OUTPUT_VARIABLE default_output
  ERROR_VARIABLE default_errors
)
if(NOT default_status EQUAL 0)
  message(FATAL_ERROR "default-path run failed with ${default_status}: ${default_output}${default_errors}")
endif()
if(NOT EXISTS "${DEFAULT_RESULT}")
  message(FATAL_ERROR "run did not write the default result path: ${DEFAULT_RESULT}")
endif()

execute_process(
  COMMAND "${CLI_BINARY}" run "${CONFIG}"
          --set execution.iterations=invalid
          --output "${INVALID_RESULT}"
  RESULT_VARIABLE invalid_status
  OUTPUT_VARIABLE invalid_output
  ERROR_VARIABLE invalid_errors
)
if(NOT invalid_status EQUAL 1)
  message(FATAL_ERROR "invalid override should exit 1, got ${invalid_status}: ${invalid_output}${invalid_errors}")
endif()
if(EXISTS "${INVALID_RESULT}")
  message(FATAL_ERROR "invalid override must not write a result")
endif()

# P4.8: the same executable renders both stored results through report and compare.
execute_process(
  COMMAND "${CLI_BINARY}" report "${RESULT}"
  RESULT_VARIABLE report_status
  OUTPUT_VARIABLE report_output
  ERROR_VARIABLE report_errors
)
if(NOT report_status EQUAL 0)
  message(FATAL_ERROR "ztick-bench report failed with ${report_status}: ${report_output}${report_errors}")
endif()
if(NOT report_output MATCHES "result report")
  message(FATAL_ERROR "report did not render a stored result: ${report_output}${report_errors}")
endif()
if(NOT report_output MATCHES "MET-01")
  message(FATAL_ERROR "report omitted stored metrics: ${report_output}${report_errors}")
endif()

execute_process(
  COMMAND "${CLI_BINARY}" compare "${RESULT}" "${DEFAULT_RESULT}"
  RESULT_VARIABLE compare_status
  OUTPUT_VARIABLE compare_output
  ERROR_VARIABLE compare_errors
)
if(NOT compare_status EQUAL 0)
  message(FATAL_ERROR "ztick-bench compare failed with ${compare_status}: ${compare_output}${compare_errors}")
endif()
if(NOT compare_output MATCHES "stored result comparison")
  message(FATAL_ERROR "compare did not render the core comparison: ${compare_output}${compare_errors}")
endif()
if(NOT compare_output MATCHES "classification: within_variability")
  message(FATAL_ERROR "compare omitted the classified result: ${compare_output}${compare_errors}")
endif()
if(NOT compare_output MATCHES "threshold_percent: 1\\.0 \\(applied\\)")
  message(FATAL_ERROR "compare omitted the applied threshold: ${compare_output}${compare_errors}")
endif()
if(NOT compare_output MATCHES "significance: not_applicable")
  message(FATAL_ERROR "compare omitted unavailable significance: ${compare_output}${compare_errors}")
endif()

execute_process(
  COMMAND "${CLI_BINARY}" report "${WORK_DIR}/missing.json"
  RESULT_VARIABLE missing_status
  OUTPUT_VARIABLE missing_output
  ERROR_VARIABLE missing_errors
)
if(NOT missing_status EQUAL 3)
  message(FATAL_ERROR "missing result should exit 3, got ${missing_status}: ${missing_output}${missing_errors}")
endif()
