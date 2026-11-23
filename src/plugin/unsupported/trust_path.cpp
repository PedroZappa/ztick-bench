// SPDX-License-Identifier: MIT
/**
 * @file trust_path.cpp
 * @brief Non-POSIX fallback for the default trusted-plugin list location.
 *
 * Platform-specific configuration paths are post-v1.0 work outside POSIX systems
 * (BR §1, §16). Reporting absence keeps the capability explicit instead of writing
 * the trusted list to an arbitrary location.
 */

#include <ztick/plugin/trust.hpp>

#include <optional>
#include <string>

namespace ztick::plugin {

std::optional<std::string> default_trust_list_path() noexcept { return std::nullopt; }

} // namespace ztick::plugin
