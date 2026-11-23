// SPDX-License-Identifier: MIT
/**
 * @file sdk_validation.hpp
 * @brief Shared private validation of read-only SDK adapter descriptors.
 *
 * Both the static registry and the dynamic loader must apply the same structural
 * checks before reading a descriptor supplied outside the compiled host. This
 * header keeps that rule in one place without adding a second public C ABI
 * surface (SDK_SPECIFICATION §1.1).
 *
 * @note Compatibility is a separate decision: callers apply the version rules
 *       after the descriptor has been proven safe to read (SDK_SPECIFICATION §2).
 */

#ifndef ZTICK_SDK_VALIDATION_HPP
#define ZTICK_SDK_VALIDATION_HPP

#include <ztick/sdk.h>

/**
 * @brief Private implementation details of the compiled SDK runtime.
 * @note Not part of the installed public surface; names may change between
 *       development phases without an ABI version bump.
 */
namespace ztick::sdk::detail {

/**
 * @brief Check that a descriptor and its nested structs can be read safely.
 * @param descriptor Read-only descriptor to check; may be NULL.
 * @return True when every size/layout-version header and the identity strings are
 *         valid for this ABI; false otherwise.
 * @note Validates the descriptor, info, ops and both nested version structs, plus
 *       a non-empty identifier and a non-NULL name. It does not check SDK
 *       compatibility, register the descriptor or execute adapter code.
 */
bool has_valid_descriptor_layout(const ztick_adapter_descriptor *descriptor) noexcept;

} // namespace ztick::sdk::detail

#endif // ZTICK_SDK_VALIDATION_HPP
