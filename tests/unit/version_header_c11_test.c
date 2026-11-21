// SPDX-License-Identifier: MIT
/**
 * @file version_header_c11_test.c
 * @brief Standalone C11 compile check and value assertions for version.h.
 *
 * This translation unit includes only the public header under test, so a
 * successful build proves that <ztick/version.h> is self-contained C11. The
 * frozen ABI values come from SDK_SPECIFICATION §1.1; all checks are performed
 * at compile time.
 */

#include <ztick/version.h>

/* The build selects C11 with extensions disabled; verify the effective mode. */
_Static_assert(__STDC_VERSION__ >= 201112L, "version.h test requires C11");

/* Frozen SDK ABI values (SDK_SPECIFICATION §1.1). */
_Static_assert(ZTICK_SDK_VERSION_MAJOR == 1, "unexpected SDK ABI major version");
_Static_assert(ZTICK_SDK_VERSION_MINOR == 0, "unexpected SDK ABI minor version");
_Static_assert(ZTICK_SDK_VERSION_PATCH == 0, "unexpected SDK ABI patch version");

/**
 * @brief Entry point for the standalone C11 header check.
 * @return Zero; every assertion is evaluated during compilation.
 */
int main(void) { return 0; }
