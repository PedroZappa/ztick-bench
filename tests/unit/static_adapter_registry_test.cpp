// SPDX-License-Identifier: MIT
/**
 * @file static_adapter_registry_test.cpp
 * @brief Verify P3.1: the minimal static adapter registers and is discoverable.
 *
 * The registry is process-wide and has no reset API, so the assertions tolerate
 * the adapter having been registered by an earlier test while still checking that
 * a repeated registration is always refused.
 */

#include <catch2/catch_test_macros.hpp>

#include <static_adapter.hpp>

#include <ztick/sdk.h>
#include <ztick/sdk_registry.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

/** @brief Descriptor published by the static test adapter. */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor;

/** @brief Expected identifier and private-registry observers used by this test. */
namespace {

/** @brief Stable identifier published by the static test adapter. */
constexpr const char kStaticAdapterId[] = "ztick.test.static.minimal";

} // namespace

/**
 * @brief Verify registration stores the descriptor once and makes it findable.
 */
TEST_CASE("static adapter registers and is discoverable", "[sdk][registry][static]") {
  const std::size_t before = ztick::sdk::detail::registered_adapter_count();
  const bool already_registered =
      ztick::sdk::detail::find_registered_adapter(kStaticAdapterId) != nullptr;

  const int32_t first = ztick_test_app_static_register();
  const int32_t second = ztick_test_app_static_register();

  if (already_registered) {
    CHECK(first == ztick_status_invalid_argument);
    CHECK(ztick::sdk::detail::registered_adapter_count() == before);
  } else {
    REQUIRE(first == ztick_status_success);
    CHECK(ztick::sdk::detail::registered_adapter_count() == before + 1U);
  }
  CHECK(second == ztick_status_invalid_argument);

  const ztick_adapter_descriptor *found =
      ztick::sdk::detail::find_registered_adapter(kStaticAdapterId);
  REQUIRE(found != nullptr);
  CHECK(found == &ztick_test_app_static_descriptor);
  CHECK(std::strcmp(found->info.identifier, kStaticAdapterId) == 0);
  CHECK(found->info.name != nullptr);
  CHECK(found->info.sdk_version.major == ZTICK_SDK_VERSION_MAJOR);
  CHECK(found->info.sdk_version.minor == ZTICK_SDK_VERSION_MINOR);
  CHECK(found->info.sdk_version.patch == ZTICK_SDK_VERSION_PATCH);
}
