// SPDX-License-Identifier: MIT
/**
 * @file compatibility_test.cpp
 * @brief Deterministic tests for host-side SDK version and capability negotiation.
 *
 * Every expected value is an independent literal or version macro, never a value
 * returned by the code under test. Failed negotiations are asserted to carry a
 * stable reason and the exact rejected capability bits so callers can explain why
 * an integration was refused (BR §6.2, SDK_SPECIFICATION §2).
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/compatibility.hpp>

#include <ztick/sdk.h>
#include <ztick/types.h>
#include <ztick/version.h>

#include <cstdint>
#include <string>

namespace {

/**
 * @brief Build SDK-compatible metadata with a chosen capability mask.
 * @param capabilities Capability bits to declare.
 * @return Owning metadata shaped like a successful dynamic inspection.
 */
ztick::plugin::AdapterMetadata make_metadata(std::uint64_t capabilities) noexcept {
  ztick::plugin::AdapterMetadata metadata;
  metadata.identifier = "negotiation.test.adapter";
  metadata.name = "Negotiation test adapter";
  metadata.adapter_major = 1U;
  metadata.adapter_minor = 0U;
  metadata.adapter_patch = 0U;
  metadata.sdk_major = ZTICK_SDK_VERSION_MAJOR;
  metadata.sdk_minor = ZTICK_SDK_VERSION_MINOR;
  metadata.sdk_patch = ZTICK_SDK_VERSION_PATCH;
  metadata.capabilities = capabilities;
  return metadata;
}

/**
 * @brief Build a version struct with valid ABI headers.
 * @return Version matching this host's SDK ABI.
 */
ztick_version host_version() noexcept {
  ztick_version version{};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  version.major = ZTICK_SDK_VERSION_MAJOR;
  version.minor = ZTICK_SDK_VERSION_MINOR;
  version.patch = ZTICK_SDK_VERSION_PATCH;
  return version;
}

/**
 * @brief Build a structurally valid static descriptor with chosen metadata.
 * @param sdk_version SDK version to publish.
 * @param capabilities Capability bits to declare.
 * @return Descriptor whose storage is owned by the caller.
 */
ztick_adapter_descriptor make_descriptor(const ztick_version &sdk_version,
                                         std::uint64_t capabilities) noexcept {
  ztick_adapter_descriptor descriptor{};
  descriptor.size = sizeof(descriptor);
  descriptor.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.size = sizeof(descriptor.info);
  descriptor.info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.identifier = "negotiation.test.static";
  descriptor.info.name = "Negotiation test static adapter";
  descriptor.info.adapter_version.size = sizeof(descriptor.info.adapter_version);
  descriptor.info.adapter_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.adapter_version.major = 1U;
  descriptor.info.sdk_version = sdk_version;
  descriptor.info.capabilities = capabilities;
  descriptor.ops.size = sizeof(descriptor.ops);
  descriptor.ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
  return descriptor;
}

} // namespace

/**
 * @brief Verify the SDK version rule is delegated to the C ABI runtime.
 */
TEST_CASE("negotiation applies SDK version rules", "[plugin][compatibility]") {
  ztick_version version = host_version();
  REQUIRE(ztick::plugin::negotiate_sdk_version(version).accepted());
  CHECK(ztick::plugin::negotiate_sdk_version(version).reason == nullptr);

  version.patch += 1U;
  CHECK(ztick::plugin::negotiate_sdk_version(version).accepted());

  version = host_version();
  version.major += 1U;
  const ztick::plugin::NegotiationResult wrong_major =
      ztick::plugin::negotiate_sdk_version(version);
  CHECK(wrong_major.status == ztick_status_incompatible);
  CHECK_FALSE(wrong_major.accepted());
  CHECK(std::string(wrong_major.reason) == "incompatible SDK version");

  version = host_version();
  version.minor += 1U;
  CHECK(ztick::plugin::negotiate_sdk_version(version).status == ztick_status_incompatible);

  version = host_version();
  version.version = 0U;
  const ztick::plugin::NegotiationResult malformed = ztick::plugin::negotiate_sdk_version(version);
  CHECK(malformed.status == ztick_status_invalid_argument);
  CHECK(std::string(malformed.reason) == "malformed SDK version header");
}

/**
 * @brief Verify required capability bits are checked against known declarations.
 */
TEST_CASE("negotiation refuses undeclared and unknown capabilities", "[plugin][compatibility]") {
  const std::uint64_t declared = ZTICK_CAPABILITY_PROCESSOR | ZTICK_CAPABILITY_ENGINE;
  REQUIRE(ztick::plugin::negotiate_capabilities(declared, 0U).accepted());

  const ztick::plugin::NegotiationResult exact =
      ztick::plugin::negotiate_capabilities(declared, ZTICK_CAPABILITY_ENGINE);
  CHECK(exact.accepted());
  CHECK(exact.rejected_capabilities == 0U);

  const std::uint64_t required = ZTICK_CAPABILITY_ENGINE | ZTICK_CAPABILITY_CHAIN;
  const ztick::plugin::NegotiationResult missing =
      ztick::plugin::negotiate_capabilities(declared, required);
  CHECK(missing.status == ztick_status_unsupported);
  CHECK_FALSE(missing.accepted());
  CHECK(std::string(missing.reason) == "required capability not declared");
  CHECK(missing.rejected_capabilities == ZTICK_CAPABILITY_CHAIN);

  const std::uint64_t unknown = UINT64_C(1) << 63;
  const ztick::plugin::NegotiationResult unknown_result =
      ztick::plugin::negotiate_capabilities(unknown, unknown);
  CHECK(unknown_result.status == ztick_status_unsupported);
  CHECK(std::string(unknown_result.reason) == "unknown capability required");
  CHECK(unknown_result.rejected_capabilities == unknown);
}

/**
 * @brief Verify inspected metadata is refused before use when it is incompatible.
 */
TEST_CASE("negotiation refuses incompatible dynamic adapters", "[plugin][compatibility]") {
  ztick::plugin::AdapterMetadata metadata = make_metadata(ZTICK_CAPABILITY_PROCESSOR);
  REQUIRE(ztick::plugin::negotiate_adapter(metadata, ZTICK_CAPABILITY_PROCESSOR).accepted());

  metadata.sdk_minor = ZTICK_SDK_VERSION_MINOR + 1U;
  metadata.capabilities = 0U;
  const ztick::plugin::NegotiationResult incompatible =
      ztick::plugin::negotiate_adapter(metadata, ZTICK_CAPABILITY_PROCESSOR);
  CHECK(incompatible.status == ztick_status_incompatible);
  CHECK_FALSE(incompatible.accepted());
  CHECK(std::string(incompatible.reason) == "incompatible SDK version");
  CHECK(incompatible.rejected_capabilities == 0U);

  metadata.sdk_minor = ZTICK_SDK_VERSION_MINOR;
  const ztick::plugin::NegotiationResult unsupported =
      ztick::plugin::negotiate_adapter(metadata, ZTICK_CAPABILITY_PROCESSOR);
  CHECK(unsupported.status == ztick_status_unsupported);
  CHECK(std::string(unsupported.reason) == "required capability not declared");
  CHECK(unsupported.rejected_capabilities == ZTICK_CAPABILITY_PROCESSOR);
}

/**
 * @brief Verify registered static descriptors use the same negotiation rules.
 */
TEST_CASE("negotiation refuses incompatible static adapters", "[plugin][compatibility]") {
  const ztick_version version = host_version();
  const ztick_adapter_descriptor descriptor = make_descriptor(version, ZTICK_CAPABILITY_CHAIN);
  REQUIRE(ztick::plugin::negotiate_adapter(descriptor, ZTICK_CAPABILITY_CHAIN).accepted());

  const ztick::plugin::NegotiationResult missing =
      ztick::plugin::negotiate_adapter(descriptor, ZTICK_CAPABILITY_ENGINE);
  CHECK(missing.status == ztick_status_unsupported);
  CHECK(missing.rejected_capabilities == ZTICK_CAPABILITY_ENGINE);

  ztick_version wrong_version = version;
  wrong_version.major += 1U;
  const ztick_adapter_descriptor incompatible_descriptor =
      make_descriptor(wrong_version, ZTICK_CAPABILITY_CHAIN);
  CHECK(ztick::plugin::negotiate_adapter(incompatible_descriptor, ZTICK_CAPABILITY_CHAIN).status ==
        ztick_status_incompatible);

  ztick_adapter_descriptor malformed = descriptor;
  malformed.info.size = 1U;
  const ztick::plugin::NegotiationResult malformed_result =
      ztick::plugin::negotiate_adapter(malformed, ZTICK_CAPABILITY_CHAIN);
  CHECK(malformed_result.status == ztick_status_invalid_argument);
  CHECK(std::string(malformed_result.reason) == "malformed adapter descriptor");
}
