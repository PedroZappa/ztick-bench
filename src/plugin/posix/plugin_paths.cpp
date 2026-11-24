// SPDX-License-Identifier: MIT
/**
 * @file plugin_paths.cpp
 * @brief POSIX resolution of the default plugin discovery directories.
 *
 * The user directory follows the XDG base directory convention; the project
 * directory is a fixed relative path. An absent or relative XDG/HOME value is
 * reported as an unconfigured user directory instead of inventing a location
 * (BR §17, ARCHITECTURE §5).
 */

#include <ztick/plugin/discovery.hpp>

#include <cstdlib>
#include <optional>
#include <string>

namespace ztick::plugin {

namespace {

/**
 * @brief Read one environment variable that must hold an absolute path.
 * @param name Environment variable name; must not be NULL.
 * @return Trimmed value, or `std::nullopt` when unset, empty or relative.
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

PluginDiscoverySources default_plugin_discovery_sources() noexcept {
  PluginDiscoverySources sources;
  sources.project_directory = ".ztick-bench/plugins";
  try {
    std::optional<std::string> base = absolute_environment_path("XDG_DATA_HOME");
    if (!base.has_value()) {
      const std::optional<std::string> home = absolute_environment_path("HOME");
      if (!home.has_value()) {
        return sources;
      }
      base = *home + "/.local/share";
    }
    sources.user_directory = *base + "/ztick-bench/plugins";
  } catch (...) {
    sources.user_directory.clear();
  }
  return sources;
}

} // namespace ztick::plugin
