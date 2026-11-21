// SPDX-License-Identifier: MIT
/**
 * @file catch2_integration_test.cpp
 * @brief First Catch2-based test: verify the framework sees the frozen SDK ABI.
 *
 * This smoke test establishes the CTest integration required by P1.8. Later
 * phases add behavioral unit tests to this executable without changing the
 * framework wiring.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/version.h>

/**
 * @brief Verify the frozen SDK version macros are visible to Catch2 tests.
 * @note Values are fixed by BR §22 and SDK_SPECIFICATION §1.1.
 */
TEST_CASE("Catch2 sees the frozen SDK ABI version", "[sdk][version][smoke]") {
  STATIC_REQUIRE(ZTICK_SDK_VERSION_MAJOR == 1);
  STATIC_REQUIRE(ZTICK_SDK_VERSION_MINOR == 0);
  STATIC_REQUIRE(ZTICK_SDK_VERSION_PATCH == 0);

  REQUIRE(ZTICK_SDK_VERSION_MAJOR == 1);
  REQUIRE(ZTICK_SDK_VERSION_MINOR == 0);
  REQUIRE(ZTICK_SDK_VERSION_PATCH == 0);
}
