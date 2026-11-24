// SPDX-License-Identifier: MIT
/**
 * @file plugin_paths.cpp
 * @brief Non-POSIX placeholder for default plugin discovery directories.
 *
 * The project-local directory is portable and still available. A user-level
 * location remains explicitly absent until the platform work lands post-v1.0,
 * instead of being silently replaced with an invented path (BR §17).
 */

#include <ztick/plugin/discovery.hpp>

namespace ztick::plugin {

PluginDiscoverySources default_plugin_discovery_sources() noexcept {
  PluginDiscoverySources sources;
  sources.project_directory = ".ztick-bench/plugins";
  return sources;
}

} // namespace ztick::plugin
