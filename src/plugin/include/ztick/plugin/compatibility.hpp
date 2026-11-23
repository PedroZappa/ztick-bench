// SPDX-License-Identifier: MIT
/**
 * @file compatibility.hpp
 * @brief Host-side SDK version and capability negotiation before adapter use.
 *
 * An integration may be executed only after its SDK version is accepted and every
 * capability required by the planned workload is declared (REQ-SDK-03). This module
 * consumes metadata from both static registration and dynamic inspection; it never
 * creates an instance or calls adapter code, so refusal happens before any plugin
 * execution (BR §6.1, §6.2).
 *
 * @note Declarations here are the coarse bitmask published in `info.capabilities`.
 *       Detailed format/parameter checks through the adapter's `describe` callback
 *       belong to the lifecycle work in P3.7/P3.8.
 */

#ifndef ZTICK_PLUGIN_COMPATIBILITY_HPP
#define ZTICK_PLUGIN_COMPATIBILITY_HPP

#include <ztick/plugin/dynamic_loader.hpp>

#include <ztick/sdk.h>
#include <ztick/types.h>

#include <cstdint>

/**
 * @brief Host-side discovery, trust and execution support.
 * @note Metadata negotiation is implemented in P3.3; trust and the in-process runner
 *       are implemented in P3.4/P3.5, while out-of-process IPC remains later Phase 3
 *       work (ARCHITECTURE §5).
 */
namespace ztick::plugin {

/**
 * @brief Outcome of one version/capability negotiation attempt.
 * @note Failures follow the SDK status model: incompatible SDK is `incompatible`,
 *       an undeclared or unknown capability is `unsupported`, and malformed input is
 *       `invalid_argument` (SDK_SPECIFICATION §8).
 */
struct NegotiationResult {
  /** @brief `ztick_status` code describing the attempt. */
  std::int32_t status{ztick_status_internal_error};
  /** @brief Static, NUL-terminated failure reason, or NULL on success. */
  const char *reason{"not negotiated"};
  /** @brief Requested bits that were unknown or not declared; zero on success. */
  std::uint64_t rejected_capabilities{};

  /**
   * @brief Report whether the integration may be configured and used.
   * @return True only when @ref status is `ztick_status_success`.
   */
  [[nodiscard]] bool accepted() const noexcept { return status == ztick_status_success; }
};

/**
 * @brief Apply the host's SDK version rules to one adapter version.
 * @param adapter_sdk_version Version the adapter was built against; its `size` and
 *        `version` headers must be initialized for the C ABI check.
 * @return Success, `incompatible` on a major/minor mismatch, or `invalid_argument`
 *         when the version struct header is malformed.
 * @note Delegates to @ref ztick_sdk_is_compatible so the rule has one definition.
 */
NegotiationResult negotiate_sdk_version(const ztick_version &adapter_sdk_version) noexcept;

/**
 * @brief Check that every requested capability bit is known and declared.
 * @param declared Additive capability bitmask published by the adapter.
 * @param required Capability bits the planned workload needs.
 * @return Success when all required bits are known and declared; otherwise
 *         `unsupported` with the rejected bits in @ref NegotiationResult::rejected_capabilities.
 * @note Unknown required bits are refused even when an adapter declares them: a host
 *       cannot interpret a capability it does not know (SDK_SPECIFICATION §2).
 */
NegotiationResult negotiate_capabilities(std::uint64_t declared, std::uint64_t required) noexcept;

/**
 * @brief Negotiate an inspected dynamic adapter before first use.
 * @param metadata Owning metadata returned by metadata-only inspection.
 * @param required Capability bits the planned workload needs.
 * @return Success only when both the SDK version and all capabilities are accepted.
 * @note The SDK version is checked first so an incompatible integration is refused
 *       before its capability mask is interpreted.
 */
NegotiationResult negotiate_adapter(const AdapterMetadata &metadata,
                                    std::uint64_t required) noexcept;

/**
 * @brief Negotiate a registered static adapter before first use.
 * @param descriptor Read-only descriptor published by a static integration.
 * @param required Capability bits the planned workload needs.
 * @return Success only when both the SDK version and all capabilities are accepted.
 * @note Malformed descriptors are refused before any field is interpreted; no
 *       instance is created and no lifecycle callback is executed.
 */
NegotiationResult negotiate_adapter(const ztick_adapter_descriptor &descriptor,
                                    std::uint64_t required) noexcept;

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_COMPATIBILITY_HPP
