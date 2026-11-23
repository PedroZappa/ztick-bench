// SPDX-License-Identifier: MIT
/**
 * @file sdk_runtime_test.cpp
 * @brief Deterministic tests for the compiled SDK runtime and its registry.
 *
 * Every expected value is an independent literal or version macro, never a value
 * returned by the implementation under test. Descriptors that are registered are
 * file-scope objects because the registry stores non-owning pointers.
 *
 * The `catch (...)` containment branches are deliberately not fault-injected:
 * forcing an allocation failure would make the test non-deterministic. They are
 * excluded under the recorded-exception rule of TESTING_STRATEGY §2.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/sdk.h>
#include <ztick/sdk_registry.hpp>
#include <ztick/sdk_validation.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

/**
 * @brief Fill a descriptor with valid headers and no lifecycle callbacks.
 * @param descriptor Descriptor to initialize; must not be NULL.
 * @param identifier Stable identifier to publish in the descriptor info.
 */
void init_descriptor(ztick_adapter_descriptor *descriptor, const char *identifier) {
  *descriptor = ztick_adapter_descriptor{};
  descriptor->size = sizeof(*descriptor);
  descriptor->version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor->info.size = sizeof(descriptor->info);
  descriptor->info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor->info.identifier = identifier;
  descriptor->info.name = identifier;
  descriptor->info.adapter_version.size = sizeof(descriptor->info.adapter_version);
  descriptor->info.adapter_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor->info.adapter_version.major = 0U;
  descriptor->info.adapter_version.minor = 1U;
  descriptor->info.adapter_version.patch = 0U;
  descriptor->info.sdk_version.size = sizeof(descriptor->info.sdk_version);
  descriptor->info.sdk_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor->info.sdk_version.major = ZTICK_SDK_VERSION_MAJOR;
  descriptor->info.sdk_version.minor = ZTICK_SDK_VERSION_MINOR;
  descriptor->info.sdk_version.patch = ZTICK_SDK_VERSION_PATCH;
  descriptor->info.capabilities = 0U;
  descriptor->info.metadata = nullptr;
  descriptor->ops.size = sizeof(descriptor->ops);
  descriptor->ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
}

/** @brief Descriptor kept alive for the registration observation test. */
ztick_adapter_descriptor register_descriptor{};

/** @brief Descriptor kept alive for the duplicate registration test. */
ztick_adapter_descriptor duplicate_descriptor{};

} // namespace

/**
 * @brief Verify the shared descriptor check accepts only safe, complete metadata.
 * @note This is the same check used by registration and dynamic inspection.
 */
TEST_CASE("sdk runtime validates descriptor layout", "[sdk][runtime]") {
  ztick_adapter_descriptor descriptor{};
  init_descriptor(&descriptor, "runtime.test.validation");
  REQUIRE(ztick::sdk::detail::has_valid_descriptor_layout(&descriptor));
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(nullptr));

  ztick_adapter_descriptor candidate = descriptor;
  candidate.size = 1U;
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.info.size = 1U;
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.ops.version = 0U;
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.info.adapter_version.size = 1U;
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.info.sdk_version.version = 0U;
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.info.identifier = "";
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.info.name = nullptr;
  CHECK_FALSE(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));

  candidate = descriptor;
  candidate.info.sdk_version.major = ZTICK_SDK_VERSION_MAJOR + 1U;
  CHECK(ztick::sdk::detail::has_valid_descriptor_layout(&candidate));
}

/**
 * @brief Verify the runtime version query matches the compile-time macros.
 * @note The caller's size and layout version fields must not be overwritten.
 */
TEST_CASE("sdk runtime reports the compiled ABI version", "[sdk][runtime]") {
  ztick_version version{};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;

  REQUIRE(ztick_sdk_get_version(&version) == ztick_status_success);
  CHECK(version.major == ZTICK_SDK_VERSION_MAJOR);
  CHECK(version.minor == ZTICK_SDK_VERSION_MINOR);
  CHECK(version.patch == ZTICK_SDK_VERSION_PATCH);
  CHECK(version.size == sizeof(version));
  CHECK(version.version == ZTICK_STRUCT_LAYOUT_VERSION);
}

/**
 * @brief Verify malformed version structs are refused before their fields are read.
 */
TEST_CASE("sdk runtime rejects malformed version queries", "[sdk][runtime]") {
  CHECK(ztick_sdk_get_version(nullptr) == ztick_status_invalid_argument);
  CHECK(ztick_sdk_is_compatible(nullptr) == ztick_status_invalid_argument);

  ztick_version version{};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  version.size = 0U;
  CHECK(ztick_sdk_get_version(&version) == ztick_status_invalid_argument);
  CHECK(ztick_sdk_is_compatible(&version) == ztick_status_invalid_argument);

  version.size = sizeof(version);
  version.version = 0U;
  CHECK(ztick_sdk_get_version(&version) == ztick_status_invalid_argument);
  CHECK(ztick_sdk_is_compatible(&version) == ztick_status_invalid_argument);

  ztick_version short_version{};
  short_version.size = 1U;
  short_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  CHECK(ztick_sdk_get_version(&short_version) == ztick_status_invalid_argument);
  CHECK(ztick_sdk_is_compatible(&short_version) == ztick_status_invalid_argument);

  // Larger-than-current sizes are forward-compatible and must be accepted.
  ztick_version oversized{};
  oversized.size = static_cast<uint32_t>(sizeof(oversized)) + 16U;
  oversized.version = ZTICK_STRUCT_LAYOUT_VERSION;
  CHECK(ztick_sdk_is_compatible(&oversized) == ztick_status_incompatible);
  CHECK(ztick_sdk_get_version(&oversized) == ztick_status_success);
}

/**
 * @brief Verify major/minor/patch compatibility rules from SDK_SPECIFICATION §2.
 */
TEST_CASE("sdk runtime applies version compatibility rules", "[sdk][runtime]") {
  ztick_version host{};
  host.size = sizeof(host);
  host.version = ZTICK_STRUCT_LAYOUT_VERSION;
  host.major = ZTICK_SDK_VERSION_MAJOR;
  host.minor = ZTICK_SDK_VERSION_MINOR;
  host.patch = ZTICK_SDK_VERSION_PATCH;

  CHECK(ztick_sdk_is_compatible(&host) == ztick_status_success);

  ztick_version candidate = host;
  candidate.major += 1U;
  CHECK(ztick_sdk_is_compatible(&candidate) == ztick_status_incompatible);

  candidate = host;
  candidate.minor += 1U;
  CHECK(ztick_sdk_is_compatible(&candidate) == ztick_status_incompatible);

  candidate = host;
  candidate.patch += 1U;
  CHECK(ztick_sdk_is_compatible(&candidate) == ztick_status_success);
}

/**
 * @brief Verify a valid descriptor is discoverable through the private observers.
 */
TEST_CASE("static adapter registration is observable", "[sdk][registry]") {
  init_descriptor(&register_descriptor, "runtime.test.register");
  const std::size_t before = ztick::sdk::detail::registered_adapter_count();

  REQUIRE(ztick_register_adapter(&register_descriptor) == ztick_status_success);
  CHECK(ztick::sdk::detail::registered_adapter_count() == before + 1U);

  const ztick_adapter_descriptor *found =
      ztick::sdk::detail::find_registered_adapter("runtime.test.register");
  REQUIRE(found != nullptr);
  CHECK(found == &register_descriptor);
  CHECK(std::strcmp(found->info.identifier, "runtime.test.register") == 0);
  CHECK(ztick::sdk::detail::find_registered_adapter(nullptr) == nullptr);
  CHECK(ztick::sdk::detail::find_registered_adapter("runtime.test.unknown") == nullptr);
}

/**
 * @brief Verify duplicate identifiers are refused without a second registry entry.
 */
TEST_CASE("duplicate adapter registration is refused", "[sdk][registry]") {
  init_descriptor(&duplicate_descriptor, "runtime.test.duplicate");
  const std::size_t before = ztick::sdk::detail::registered_adapter_count();

  REQUIRE(ztick_register_adapter(&duplicate_descriptor) == ztick_status_success);
  CHECK(ztick_register_adapter(&duplicate_descriptor) == ztick_status_invalid_argument);
  CHECK(ztick::sdk::detail::registered_adapter_count() == before + 1U);
}

/**
 * @brief Verify every descriptor validation branch refuses unsafe input with a reason.
 */
TEST_CASE("registration rejects malformed and incompatible descriptors", "[sdk][registry]") {
  CHECK(ztick_register_adapter(nullptr) == ztick_status_invalid_argument);

  ztick_adapter_descriptor descriptor{};
  init_descriptor(&descriptor, "runtime.test.bad_descriptor_size");
  descriptor.size = 0U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.bad_info_version");
  descriptor.info.version = 0U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.bad_info_size");
  descriptor.info.size = 1U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.bad_ops_size");
  descriptor.ops.size = 1U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.bad_ops_version");
  descriptor.ops.version = 0U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.bad_adapter_version_size");
  descriptor.info.adapter_version.size = 1U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.bad_adapter_version_version");
  descriptor.info.adapter_version.version = 0U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.no_identifier");
  descriptor.info.identifier = nullptr;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.empty_identifier");
  descriptor.info.identifier = "";
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.no_name");
  descriptor.info.name = nullptr;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_invalid_argument);

  init_descriptor(&descriptor, "runtime.test.incompatible");
  descriptor.info.sdk_version.major += 1U;
  CHECK(ztick_register_adapter(&descriptor) == ztick_status_incompatible);
  CHECK(ztick::sdk::detail::find_registered_adapter("runtime.test.incompatible") == nullptr);
}

/**
 * @brief Verify status messages cover every declared code and unknown codes.
 */
TEST_CASE("status messages are stable and never null", "[sdk][status]") {
  CHECK(std::strcmp(ztick_status_message(ztick_status_success), "success") == 0);
  CHECK(std::strcmp(ztick_status_message(ztick_status_unsupported), "unsupported") == 0);
  CHECK(std::strcmp(ztick_status_message(ztick_status_incompatible), "incompatible") == 0);
  CHECK(std::strcmp(ztick_status_message(ztick_status_invalid_argument), "invalid argument") == 0);
  CHECK(std::strcmp(ztick_status_message(ztick_status_processing_failure), "processing failure") ==
        0);
  CHECK(std::strcmp(ztick_status_message(ztick_status_validation_failure), "validation failure") ==
        0);
  CHECK(std::strcmp(ztick_status_message(ztick_status_internal_error), "internal error") == 0);
  CHECK(std::strcmp(ztick_status_message(-12345), "unknown status") == 0);
}
