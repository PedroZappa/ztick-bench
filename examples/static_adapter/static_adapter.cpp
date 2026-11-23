// SPDX-License-Identifier: MIT
/**
 * @file static_adapter.cpp
 * @brief Minimal static SDK test adapter with a registration entry point (P3.1).
 *
 * This adapter publishes a complete read-only descriptor and registers it with the
 * host SDK registry when @ref ztick_test_app_static_register is called. Lifecycle
 * callbacks remain unset: the full processor/chain harness arrives in P3.7, so no
 * conformant processing is claimed here (TESTING_STRATEGY §3).
 */

#include <static_adapter.hpp>

#include <ztick/sdk.h>

/** @brief Internal constants of the minimal static test adapter. */
namespace {

/** @brief Placeholder integration version for the minimal static adapter. */
const ztick_version kAdapterVersion{static_cast<uint32_t>(sizeof(ztick_version)),
                                    ZTICK_STRUCT_LAYOUT_VERSION, 0U, 1U, 0U};

/** @brief SDK ABI version this adapter was compiled against. */
const ztick_version kSdkVersion{static_cast<uint32_t>(sizeof(ztick_version)),
                                ZTICK_STRUCT_LAYOUT_VERSION, ZTICK_SDK_VERSION_MAJOR,
                                ZTICK_SDK_VERSION_MINOR, ZTICK_SDK_VERSION_PATCH};

/** @brief Empty lifecycle table; callbacks are supplied by P3.7. */
const ztick_adapter_ops kOps{static_cast<uint32_t>(sizeof(ztick_adapter_ops)),
                             ZTICK_STRUCT_LAYOUT_VERSION,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr};

} // namespace

/**
 * @brief Static registration-point descriptor for the minimal test adapter.
 * @note The descriptor has static storage duration so it can outlive the SDK
 *       registry. P3.7 replaces the empty lifecycle table with real callbacks.
 */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor = {
    static_cast<uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     "ztick.test.static.minimal", "Minimal static SDK test adapter", kAdapterVersion, kSdkVersion,
     0U, "Minimal static adapter; registers in P3.1, lifecycle callbacks arrive in P3.7."},
    kOps};

/**
 * @brief Register this static test adapter with the host SDK registry.
 * @return The `ztick_status` code from @ref ztick_register_adapter; a second call
 *         returns `ztick_status_invalid_argument` for the duplicate identifier.
 * @note The descriptor has static storage duration and outlives the registry. This
 *       entry point validates and stores metadata only; it creates no instance and
 *       runs no lifecycle callback (SDK_SPECIFICATION §2, §10).
 */
extern "C" int32_t ztick_test_app_static_register(void) {
  return ztick_register_adapter(&ztick_test_app_static_descriptor);
}
