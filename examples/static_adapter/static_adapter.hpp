// SPDX-License-Identifier: MIT
/**
 * @file static_adapter.hpp
 * @brief Registration entry point for the Phase 3 static SDK test adapter.
 *
 * A static integration is compiled into the host and publishes a registration
 * point that the host calls during initialization (SDK_SPECIFICATION §10). The
 * call validates and stores a read-only descriptor; it creates no instance and
 * runs no lifecycle callback (SDK_SPECIFICATION §2, §10).
 */

/** @brief Include guard for the static test adapter registration header. */
#ifndef ZTICK_EXAMPLE_STATIC_ADAPTER_HPP
#define ZTICK_EXAMPLE_STATIC_ADAPTER_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the static test adapter descriptor with the host SDK registry.
 * @return A `ztick_status` code; a second call returns
 *         `ztick_status_invalid_argument` because the identifier is already
 *         registered (SDK_SPECIFICATION §2).
 * @note The descriptor has static storage duration and outlives the registry. The
 *       entry point validates metadata only: it never creates an instance or calls
 *       a lifecycle callback. Lifecycle callbacks arrive with P3.7.
 */
int32_t ztick_test_app_static_register(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZTICK_EXAMPLE_STATIC_ADAPTER_HPP
