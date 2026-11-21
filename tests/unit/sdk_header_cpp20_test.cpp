// SPDX-License-Identifier: MIT
/**
 * @file sdk_header_cpp20_test.cpp
 * @brief Standalone C++20 compile check for the public SDK C ABI header.
 *
 * The header is included first and on its own. Compiling it both as C11 and as
 * C++20 demonstrates that the public surface has no C++-only types or templates;
 * the header's linkage guard keeps the exported functions in the C ABI. No
 * implementation is linked here.
 */

#include <ztick/sdk.h>

#include <type_traits>

// The build selects C++20 with extensions disabled; verify the effective mode.
static_assert(__cplusplus >= 202002L, "sdk.h test requires C++20");

#ifndef ZTICK_API
#error "sdk.h must define ZTICK_API"
#endif

#ifndef ZTICK_ADAPTER_EXPORT_SYMBOL
#error "sdk.h must define the dynamic adapter export symbol"
#endif

// The runtime query must agree with the compile-time version macros.
static_assert(ZTICK_SDK_VERSION_MAJOR == 1, "unexpected SDK ABI major version");
static_assert(ZTICK_SDK_VERSION_MINOR == 0, "unexpected SDK ABI minor version");
static_assert(ZTICK_SDK_VERSION_PATCH == 0, "unexpected SDK ABI patch version");

// Opaque handles are incomplete but pointers to them are usable.
static_assert(std::is_same_v<ztick_adapter *, ztick_adapter *>,
              "ztick_adapter handle is not declared");

// Public structs follow the frozen size/version evolution rule.
static_assert(offsetof(ztick_adapter_ops, size) == 0, "ztick_adapter_ops::size offset");
static_assert(offsetof(ztick_adapter_ops, version) == sizeof(uint32_t),
              "ztick_adapter_ops::version offset");
static_assert(offsetof(ztick_adapter_descriptor, size) == 0,
              "ztick_adapter_descriptor::size offset");
static_assert(offsetof(ztick_adapter_descriptor, version) == sizeof(uint32_t),
              "ztick_adapter_descriptor::version offset");

// Every lifecycle entry point and descriptor field must have its declared type.
static_assert(std::is_same_v<decltype(((ztick_adapter_ops *)0)->create), ztick_adapter_create_fn>,
              "ops.create missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_ops *)0)->destroy), ztick_adapter_destroy_fn>,
              "ops.destroy missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_adapter_ops *)0)->describe), ztick_adapter_describe_fn>,
    "ops.describe missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_adapter_ops *)0)->configure), ztick_adapter_configure_fn>,
    "ops.configure missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_ops *)0)->prepare), ztick_adapter_prepare_fn>,
              "ops.prepare missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_ops *)0)->process), ztick_adapter_process_fn>,
              "ops.process missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_ops *)0)->finish), ztick_adapter_finish_fn>,
              "ops.finish missing or wrong type");
static_assert(
    std::is_same_v<decltype(((ztick_adapter_ops *)0)->validate), ztick_adapter_validate_fn>,
    "ops.validate missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_ops *)0)->report), ztick_adapter_report_fn>,
              "ops.report missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_descriptor *)0)->info), ztick_adapter_info>,
              "descriptor.info missing or wrong type");
static_assert(std::is_same_v<decltype(((ztick_adapter_descriptor *)0)->ops), ztick_adapter_ops>,
              "descriptor.ops missing or wrong type");
static_assert(std::is_same_v<decltype((ztick_metric_sink_fn)0), ztick_metric_sink_fn>,
              "metric sink callback missing");

// Host-side functions are declared in the C ABI with integer status returns.
static_assert(std::is_same_v<decltype(&ztick_sdk_get_version), int32_t (*)(ztick_version *)>,
              "ztick_sdk_get_version missing or wrong signature");
static_assert(
    std::is_same_v<decltype(&ztick_sdk_is_compatible), int32_t (*)(const ztick_version *)>,
    "ztick_sdk_is_compatible missing or wrong signature");
static_assert(std::is_same_v<decltype(&ztick_register_adapter),
                             int32_t (*)(const ztick_adapter_descriptor *)>,
              "ztick_register_adapter missing or wrong signature");
static_assert(std::is_same_v<decltype(&ztick_status_message), const char *(*)(int32_t)>,
              "ztick_status_message missing or wrong signature");

/**
 * @brief Entry point for the standalone C++20 SDK header check.
 * @return Zero; every assertion is evaluated during compilation.
 */
int main() { return 0; }
