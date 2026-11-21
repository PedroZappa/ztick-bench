// SPDX-License-Identifier: MIT
/**
 * @file sdk_header_c11_test.c
 * @brief Standalone C11 compile check for the public SDK C ABI header.
 *
 * The header is included first and on its own, so a successful build proves it
 * is self-contained C11 and free of C++ types. The assertions exercise the
 * exported macro surface, opaque handles, descriptor fields and function
 * declarations without requiring any implementation to be linked.
 */

#include <ztick/sdk.h>

/* The build selects C11 with extensions disabled; verify the effective mode. */
_Static_assert(__STDC_VERSION__ >= 201112L, "sdk.h test requires C11");

#ifndef ZTICK_API
#error "sdk.h must define ZTICK_API"
#endif

#ifndef ZTICK_ADAPTER_EXPORT_SYMBOL
#error "sdk.h must define the dynamic adapter export symbol"
#endif

/* The runtime query must agree with the compile-time version macros. */
_Static_assert(ZTICK_SDK_VERSION_MAJOR == 1, "unexpected SDK ABI major version");
_Static_assert(ZTICK_SDK_VERSION_MINOR == 0, "unexpected SDK ABI minor version");
_Static_assert(ZTICK_SDK_VERSION_PATCH == 0, "unexpected SDK ABI patch version");

/* Opaque handles are incomplete but pointers to them are usable. */
_Static_assert(_Generic((ztick_adapter *)0, ztick_adapter *: 1, default: 0),
               "ztick_adapter handle is not declared");

/* Public structs follow the frozen size/version evolution rule. */
_Static_assert(offsetof(ztick_adapter_ops, size) == 0, "ztick_adapter_ops::size offset");
_Static_assert(offsetof(ztick_adapter_ops, version) == sizeof(uint32_t),
               "ztick_adapter_ops::version offset");
_Static_assert(offsetof(ztick_adapter_descriptor, size) == 0,
               "ztick_adapter_descriptor::size offset");
_Static_assert(offsetof(ztick_adapter_descriptor, version) == sizeof(uint32_t),
               "ztick_adapter_descriptor::version offset");

/* Every lifecycle entry point and descriptor field must have its declared type. */
_Static_assert(_Generic(((ztick_adapter_ops *)0)->create, ztick_adapter_create_fn: 1, default: 0),
               "ops.create missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->destroy, ztick_adapter_destroy_fn: 1, default: 0),
               "ops.destroy missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->describe,
                   ztick_adapter_describe_fn: 1,
                   default: 0),
               "ops.describe missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->configure,
                   ztick_adapter_configure_fn: 1,
                   default: 0),
               "ops.configure missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->prepare, ztick_adapter_prepare_fn: 1, default: 0),
               "ops.prepare missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->process, ztick_adapter_process_fn: 1, default: 0),
               "ops.process missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->finish, ztick_adapter_finish_fn: 1, default: 0),
               "ops.finish missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->validate,
                   ztick_adapter_validate_fn: 1,
                   default: 0),
               "ops.validate missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_ops *)0)->report, ztick_adapter_report_fn: 1, default: 0),
               "ops.report missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_descriptor *)0)->info, ztick_adapter_info: 1, default: 0),
               "descriptor.info missing or wrong type");
_Static_assert(_Generic(((ztick_adapter_descriptor *)0)->ops, ztick_adapter_ops: 1, default: 0),
               "descriptor.ops missing or wrong type");
_Static_assert(_Generic((ztick_metric_sink_fn)0, ztick_metric_sink_fn: 1, default: 0),
               "metric sink callback missing");

/* Host-side functions are declared in the C ABI with integer status returns. */
_Static_assert(_Generic(&ztick_sdk_get_version, int32_t (*)(ztick_version *): 1, default: 0),
               "ztick_sdk_get_version missing or wrong signature");
_Static_assert(_Generic(&ztick_sdk_is_compatible,
                   int32_t (*)(const ztick_version *): 1,
                   default: 0),
               "ztick_sdk_is_compatible missing or wrong signature");
_Static_assert(_Generic(&ztick_register_adapter,
                   int32_t (*)(const ztick_adapter_descriptor *): 1,
                   default: 0),
               "ztick_register_adapter missing or wrong signature");
_Static_assert(_Generic(&ztick_status_message, const char *(*)(int32_t): 1, default: 0),
               "ztick_status_message missing or wrong signature");

/**
 * @brief Entry point for the standalone C11 SDK header check.
 * @return Zero; every assertion is evaluated during compilation.
 */
int main(void) { return 0; }
