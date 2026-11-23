// SPDX-License-Identifier: MIT
/**
 * @file trust_path.cpp
 * @brief POSIX resolution of the default trusted-plugin list location.
 *
 * Follows the XDG base directory convention so the trusted list lives under the
 * user's configuration directory and no platform call leaks into portable trust
 * policy (REQ-ARCH-02). Absence is reported instead of inventing a location when no
 * usable environment is set (BR §17).
 */

#include <ztick/plugin/trust.hpp>

#include <cstdlib>
#include <optional>
#include <string>

/**
 * @brief Private POSIX implementation details of the trusted-list path.
 */
namespace ztick::plugin::posix {
namespace {

/**
 * @brief Read one environment variable that must hold an absolute path.
 * @param name Environment variable name; must not be NULL.
 * @return The value with trailing slashes removed, or `std::nullopt` when the
 *         variable is unset, empty or not absolute.
 * @note XDG requires relative values to be ignored, so they are treated as unset.
 */
std::optional<std::string> absolute_environment_path(const char *name) noexcept {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] != '/') {
    return std::nullopt;
  }
  try {
    std::string path(value);
    while (path.size() > 1U && path.back() == '/') {
      path.pop_back();
    }
    return path;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace
} // namespace ztick::plugin::posix

namespace ztick::plugin {

std::optional<std::string> default_trust_list_path() noexcept {
  try {
    std::optional<std::string> base = posix::absolute_environment_path("XDG_CONFIG_HOME");
    if (!base.has_value()) {
      const std::optional<std::string> home = posix::absolute_environment_path("HOME");
      if (!home.has_value()) {
        return std::nullopt;
      }
      base = *home + "/.config";
    }
    return *base + "/ztick-bench/trusted-plugins.txt";
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace ztick::plugin
