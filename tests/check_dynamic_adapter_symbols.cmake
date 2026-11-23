# SPDX-License-Identifier: MIT
# Guard the Phase 3 dynamic-adapter contract (SDK_SPECIFICATION §10): a module
# exposes read-only metadata and must not embed or import SDK runtime symbols,
# because the host owns the single static-adapter registry.

if(NOT DEFINED MODULE_PATH OR "${MODULE_PATH}" STREQUAL "")
  message(FATAL_ERROR "MODULE_PATH is required")
endif()
if(NOT DEFINED NM_PROGRAM OR "${NM_PROGRAM}" STREQUAL "")
  message(FATAL_ERROR "NM_PROGRAM is required")
endif()

execute_process(
  COMMAND "${NM_PROGRAM}" -D "${MODULE_PATH}"
  RESULT_VARIABLE nm_result
  OUTPUT_VARIABLE nm_output
  ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "nm failed for ${MODULE_PATH}: ${nm_error}")
endif()

set(runtime_symbols
  ztick_sdk_get_version
  ztick_sdk_is_compatible
  ztick_register_adapter
  ztick_status_message
)
foreach(symbol IN LISTS runtime_symbols)
  if(nm_output MATCHES "${symbol}")
    message(FATAL_ERROR
      "Dynamic adapter must not reference ${symbol}; the host owns the SDK registry")
  endif()
endforeach()

message(STATUS "Dynamic adapter exposes no SDK runtime symbols")
