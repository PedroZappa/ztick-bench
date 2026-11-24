// SPDX-License-Identifier: MIT
/**
 * @file discovery.hpp
 * @brief Metadata-only discovery of dynamic plugin adapters.
 *
 * REQ-ARCH-07 requires explicit paths, a user plugin directory and a project-local
 * plugin directory, and forbids executing a plugin during discovery. This module
 * enumerates candidate files with an injected directory reader and reuses
 * @ref ztick::plugin::inspect_dynamic_adapter so only the read-only descriptor data
 * symbol is resolved. Invalid modules are returned with an explicit reason instead
 * of being silently skipped (BR §17).
 *
 * @note This header is private to ztick-bench and is not installed. Platform path
 *       resolution lives in @ref ztick::plugin::default_plugin_discovery_sources
 *       so the enumeration policy stays portable and testable.
 */

#ifndef ZTICK_PLUGIN_DISCOVERY_HPP
#define ZTICK_PLUGIN_DISCOVERY_HPP

#include <ztick/plugin/dynamic_loader.hpp>
#include <ztick/plugin/trust.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/** @brief Host-side discovery, trust and execution support. */
namespace ztick::plugin {

/** @brief Origin of one discovered plugin candidate. */
enum class PluginSource : std::uint8_t {
  /** @brief Candidate supplied directly on the command line. */
  explicit_path = 0,
  /** @brief Candidate found in the user plugin directory. */
  user_directory = 1,
  /** @brief Candidate found in the project-local plugin directory. */
  project_directory = 2,
};

/**
 * @brief Render one discovery source as a stable token.
 * @param source Source to encode.
 * @return `explicit_path`, `user_directory` or `project_directory`.
 */
std::string_view plugin_source_token(PluginSource source) noexcept;

/** @brief Directories and explicit paths used by one discovery pass. */
struct PluginDiscoverySources {
  /** @brief Explicit candidates; empty when only directories are searched. */
  std::vector<std::string> explicit_paths;
  /** @brief User plugin directory, or empty when not configured. */
  std::string user_directory;
  /** @brief Project-local plugin directory, or empty when not configured. */
  std::string project_directory;
};

/** @brief Availability of one searched plugin directory. */
struct PluginDirectoryStatus {
  /** @brief Directory path that was inspected. */
  std::string path;
  /** @brief True when the directory existed and could be enumerated. */
  bool available{false};
  /** @brief Explanation when @ref available is false; empty otherwise. */
  std::string reason;
};

/** @brief One candidate found by discovery, with its inspection outcome. */
struct DiscoveredPlugin {
  /** @brief Path of the candidate module. */
  std::string path;
  /** @brief Source that produced the candidate. */
  PluginSource source{PluginSource::explicit_path};
  /** @brief Metadata-only inspection result; may carry a failure reason. */
  InspectionResult inspection;
};

/** @brief Complete outcome of one discovery pass. */
struct DiscoveryResult {
  /** @brief Candidates in source order; invalid modules are retained. */
  std::vector<DiscoveredPlugin> plugins;
  /** @brief Explicit status for every configured directory. */
  std::vector<PluginDirectoryStatus> directories;
};

/**
 * @brief Readable, testable enumeration of one plugin directory.
 * @note Implementations return paths to candidate files and report inaccessible
 *       directories explicitly. They must never open or execute a plugin module.
 */
class PluginDirectoryEnumerator {
public:
  /** @brief Allow derived enumerators to be destroyed through this interface. */
  virtual ~PluginDirectoryEnumerator() = default;

  /**
   * @brief Enumerate candidate files in one directory.
   * @param directory Directory path to inspect.
   * @param error Optional receiver for a human-readable failure explanation.
   * @return Candidate paths in a deterministic order; empty when none are found.
   */
  virtual std::vector<std::string> list(const std::string &directory, std::string *error) const = 0;
};

/**
 * @brief Access the platform-selected directory enumerator.
 * @return Process-wide enumerator with static storage duration.
 * @note Thread-safe; the returned object is owned by the implementation.
 */
const PluginDirectoryEnumerator &platform_plugin_directory_enumerator() noexcept;

/**
 * @brief Resolve the default user and project-local plugin directories.
 * @return Sources with platform-resolved directories on POSIX and explicit
 *         absence elsewhere; no candidate paths are added.
 * @note POSIX uses `XDG_DATA_HOME` or `$HOME/.local/share` for the user directory
 *       and `./.ztick-bench/plugins` for the project directory.
 */
PluginDiscoverySources default_plugin_discovery_sources() noexcept;

/**
 * @brief Discover plugins through injected enumeration and inspection services.
 * @param sources Paths to search; empty entries are reported as not configured.
 * @param enumerator Directory reader used for the two configured directories.
 * @param loader Shared-library loader used for metadata-only inspection.
 * @return Candidates and directory states in deterministic order.
 * @note A candidate found through more than one source is retained once, using
 *       the first source in explicit, user, project order. A missing or unreadable
 *       directory is reported as unavailable and does not fail the pass.
 */
DiscoveryResult discover_plugins(const PluginDiscoverySources &sources,
                                 const PluginDirectoryEnumerator &enumerator,
                                 const SharedLibraryLoader &loader) noexcept;

/**
 * @brief Discover plugins through the platform enumerator and loader.
 * @param sources Paths to search.
 * @return Candidates and directory states in deterministic order.
 * @note Convenience overload; see the injected form for guarantees.
 */
DiscoveryResult discover_plugins(const PluginDiscoverySources &sources) noexcept;

/**
 * @brief Read the persisted trusted-plugin list without prompting or loading code.
 * @param store Store to read; must outlive this call.
 * @return Store status plus trusted identities.
 * @note An unreadable list is an explicit failure; callers must not treat it as an
 *       empty trust list (BR §6.2).
 */
TrustListResult read_trust_list(const TrustStore &store) noexcept;

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_DISCOVERY_HPP
