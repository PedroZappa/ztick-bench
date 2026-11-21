// SPDX-License-Identifier: MIT
/**
 * @file version_header_cpp20_test.cpp
 * @brief Standalone C++20 compile check and value assertions for version.h.
 *
 * This translation unit includes only the public header under test, so a
 * successful build proves that <ztick/version.h> is self-contained C++20. The
 * frozen ABI values come from SDK_SPECIFICATION §1.1; all checks are performed
 * at compile time.
 */

#include <ztick/version.h>

// The build selects C++20 with extensions disabled; verify the effective mode.
static_assert(__cplusplus >= 202002L, "version.h test requires C++20");

// Frozen SDK ABI values (SDK_SPECIFICATION §1.1).
static_assert(ZTICK_SDK_VERSION_MAJOR == 1, "unexpected SDK ABI major version");
static_assert(ZTICK_SDK_VERSION_MINOR == 0, "unexpected SDK ABI minor version");
static_assert(ZTICK_SDK_VERSION_PATCH == 0, "unexpected SDK ABI patch version");

/**
 * @brief Entry point for the standalone C++20 header check.
 * @return Zero; every assertion is evaluated during compilation.
 */
int main() { return 0; }
