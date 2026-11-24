// SPDX-License-Identifier: MIT
/**
 * @file compatibility.cpp
 * @brief Host-side SDK version and capability negotiation implementation.
 *
 * Refuses an integration before configuration or execution when its SDK version is
 * incompatible or its declared capabilities do not cover the request. Every failure
 * carries a status and a stable reason; no adapter code is called (BR §6.1, §6.2).
 */

#include <ztick/plugin/compatibility.hpp>

#include <ztick/sdk.h>
#include <ztick/sdk_validation.hpp>
#include <ztick/types.h>
#include <ztick/version.h>

namespace ztick::plugin {
namespace {

/**
 * @brief Build a failure result with a stable explanatory string.
 * @param status `ztick_status` code to report.
 * @param reason Static, NUL-terminated explanation; must not be NULL.
 * @param rejected Requested capability bits that caused the refusal.
 * @return Result with the given failure details.
 */
NegotiationResult failure(std::int32_t status, const char *reason,
                          std::uint64_t rejected = 0U) noexcept {
  return NegotiationResult{status, reason, rejected};
}

/**
 * @brief Assemble a version struct from an owning metadata copy.
 * @param metadata Metadata returned by inspection.
 * @return A version struct with valid ABI headers and the adapter's version triple.
 */
ztick_version version_from_metadata(const AdapterMetadata &metadata) noexcept {
  ztick_version version{};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  version.major = metadata.sdk_major;
  version.minor = metadata.sdk_minor;
  version.patch = metadata.sdk_patch;
  return version;
}

} // namespace

NegotiationResult negotiate_sdk_version(const ztick_version &adapter_sdk_version) noexcept {
  const std::int32_t status = ztick_sdk_is_compatible(&adapter_sdk_version);
  if (status == ztick_status_success) {
    return NegotiationResult{ztick_status_success, nullptr, 0U};
  }
  if (status == ztick_status_incompatible) {
    return failure(ztick_status_incompatible, "incompatible SDK version");
  }
  return failure(ztick_status_invalid_argument, "malformed SDK version header");
}

NegotiationResult negotiate_capabilities(std::uint64_t declared, std::uint64_t required) noexcept {
  const std::uint64_t unknown = required & ~ZTICK_CAPABILITY_KNOWN_MASK;
  const std::uint64_t missing = required & ~declared & ZTICK_CAPABILITY_KNOWN_MASK;
  const std::uint64_t rejected = unknown | missing;
  if (rejected == 0U) {
    return NegotiationResult{ztick_status_success, nullptr, 0U};
  }
  if (unknown != 0U) {
    return failure(ztick_status_unsupported, "unknown capability required", rejected);
  }
  return failure(ztick_status_unsupported, "required capability not declared", rejected);
}

NegotiationResult negotiate_adapter(const AdapterMetadata &metadata,
                                    std::uint64_t required) noexcept {
  const NegotiationResult version = negotiate_sdk_version(version_from_metadata(metadata));
  if (!version.accepted()) {
    return version;
  }
  return negotiate_capabilities(metadata.capabilities, required);
}

NegotiationResult negotiate_adapter(const ztick_adapter_descriptor &descriptor,
                                    std::uint64_t required) noexcept {
  if (!sdk::detail::has_valid_descriptor_layout(&descriptor)) {
    return failure(ztick_status_invalid_argument, "malformed adapter descriptor");
  }
  const NegotiationResult version = negotiate_sdk_version(descriptor.info.sdk_version);
  if (!version.accepted()) {
    return version;
  }
  return negotiate_capabilities(descriptor.info.capabilities, required);
}

} // namespace ztick::plugin
