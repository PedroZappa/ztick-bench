// SPDX-License-Identifier: MIT
/**
 * @file sdk_registry.hpp
 * @brief Private host-side observers for the SDK static adapter registry.
 *
 * This header is deliberately not installed: it lets in-tree integration code and
 * tests inspect descriptors registered through `ztick_register_adapter` without
 * adding a second public C ABI surface (SDK_SPECIFICATION §1.1). Registration and
 * observation never create an adapter instance or execute adapter code.
 */

#ifndef ZTICK_SDK_REGISTRY_HPP
#define ZTICK_SDK_REGISTRY_HPP

#include <ztick/sdk.h>

#include <cstddef>

/**
 * @brief Private implementation details of the compiled SDK runtime.
 * @note Not part of the installed public surface; names may change between
 *       development phases without an ABI version bump.
 */
namespace ztick::sdk::detail {

/**
 * @brief Count descriptors currently held by the static adapter registry.
 * @return Number of successfully registered descriptors.
 * @note Thread-safe. The count reflects all registrations visible at the time of
 *       the call; it is not a license to execute any registered adapter.
 */
std::size_t registered_adapter_count() noexcept;

/**
 * @brief Look up a registered descriptor by its stable identifier.
 * @param identifier NUL-terminated identifier to find; may be NULL.
 * @return The registered descriptor, or NULL when @p identifier is NULL or unknown.
 * @note Thread-safe. The returned pointer is non-owning and remains valid only while
 *       the registering adapter keeps its descriptor alive.
 */
const ztick_adapter_descriptor *find_registered_adapter(const char *identifier) noexcept;

} // namespace ztick::sdk::detail

#endif // ZTICK_SDK_REGISTRY_HPP
