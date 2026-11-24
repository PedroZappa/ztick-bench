// SPDX-License-Identifier: MIT
/**
 * @file discovery.cpp
 * @brief Metadata-only plugin enumeration over explicit paths and directories.
 *
 * Discovery deliberately stops at candidate paths and descriptor inspection. It
 * never registers, configures or executes an adapter, and it keeps unavailable
 * directories and malformed modules in the result as explicit states.
 */

#include <ztick/plugin/discovery.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace ztick::plugin {

namespace {

/**
 * @brief Portable filename filter for modules found in plugin directories.
 * @param path Candidate path.
 * @return True for shared-library extensions used by the platforms in scope.
 */
bool is_plugin_candidate(const std::filesystem::path &path) noexcept {
  const std::filesystem::path extension = path.extension();
  return extension == ".so" || extension == ".dylib" || extension == ".dll";
}

/** @brief Directory reader backed by the C++ filesystem library. */
class FilesystemDirectoryEnumerator final : public PluginDirectoryEnumerator {
public:
  /**
   * @brief Enumerate regular shared-library files in deterministic path order.
   * @param directory Directory to inspect.
   * @param error Optional receiver for a human-readable failure explanation.
   * @return Candidate paths, or an empty list with an explicit reason.
   */
  std::vector<std::string> list(const std::string &directory, std::string *error) const override {
    std::vector<std::string> candidates;
    if (directory.empty()) {
      if (error != nullptr) {
        *error = "plugin directory is not configured";
      }
      return candidates;
    }
    std::error_code code;
    const std::filesystem::path root(directory);
    if (!std::filesystem::is_directory(root, code)) {
      if (error != nullptr) {
        *error = code ? "cannot inspect plugin directory: " + code.message()
                      : "plugin directory does not exist";
      }
      return candidates;
    }
    std::filesystem::directory_iterator iterator(root, code);
    const std::filesystem::directory_iterator end;
    while (!code && iterator != end) {
      const std::filesystem::directory_entry &entry = *iterator;
      const bool regular = entry.is_regular_file(code);
      if (!code && regular && is_plugin_candidate(entry.path())) {
        candidates.push_back(entry.path().string());
      }
      iterator.increment(code);
    }
    if (code) {
      if (error != nullptr) {
        *error = "cannot enumerate plugin directory: " + code.message();
      }
      return {};
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates;
  }
};

/**
 * @brief Add one inspected candidate to a discovery result.
 * @param result Result to extend.
 * @param seen Paths already added by an earlier source.
 * @param path Candidate path.
 * @param source Origin of the candidate.
 * @param loader Loader used for metadata-only inspection.
 */
void add_candidate(DiscoveryResult &result, std::set<std::string> &seen, std::string path,
                   PluginSource source, const SharedLibraryLoader &loader) {
  if (!seen.insert(path).second) {
    return;
  }
  DiscoveredPlugin discovered;
  discovered.inspection = inspect_dynamic_adapter(path.c_str(), loader);
  discovered.source = source;
  discovered.path = std::move(path);
  result.plugins.push_back(std::move(discovered));
}

/**
 * @brief Add all candidates from one configured directory.
 * @param result Result to extend.
 * @param seen Paths already added by an earlier source.
 * @param directory Directory to enumerate.
 * @param source Origin used for its candidates.
 * @param enumerator Directory reader to call.
 * @param loader Loader used for metadata-only inspection.
 */
void add_directory(DiscoveryResult &result, std::set<std::string> &seen,
                   const std::string &directory, PluginSource source,
                   const PluginDirectoryEnumerator &enumerator, const SharedLibraryLoader &loader) {
  PluginDirectoryStatus status;
  status.path = directory;
  std::string error;
  const std::vector<std::string> candidates = enumerator.list(directory, &error);
  status.available = error.empty();
  status.reason = error;
  const bool available = status.available;
  result.directories.push_back(std::move(status));
  if (!available) {
    return;
  }
  for (const std::string &candidate : candidates) {
    add_candidate(result, seen, candidate, source, loader);
  }
}

/**
 * @brief Discovery implementation that may allocate and throw.
 * @param sources Paths to search.
 * @param enumerator Directory reader to call.
 * @param loader Loader used for metadata-only inspection.
 * @return Complete discovery result.
 */
DiscoveryResult discover_plugins_impl(const PluginDiscoverySources &sources,
                                      const PluginDirectoryEnumerator &enumerator,
                                      const SharedLibraryLoader &loader) {
  DiscoveryResult result;
  std::set<std::string> seen;
  for (const std::string &path : sources.explicit_paths) {
    add_candidate(result, seen, path, PluginSource::explicit_path, loader);
  }
  add_directory(result, seen, sources.user_directory, PluginSource::user_directory, enumerator,
                loader);
  add_directory(result, seen, sources.project_directory, PluginSource::project_directory,
                enumerator, loader);
  return result;
}

} // namespace

std::string_view plugin_source_token(PluginSource source) noexcept {
  switch (source) {
  case PluginSource::explicit_path:
    return "explicit_path";
  case PluginSource::user_directory:
    return "user_directory";
  case PluginSource::project_directory:
    return "project_directory";
  }
  return "explicit_path";
}

const PluginDirectoryEnumerator &platform_plugin_directory_enumerator() noexcept {
  static const FilesystemDirectoryEnumerator enumerator;
  return enumerator;
}

DiscoveryResult discover_plugins(const PluginDiscoverySources &sources,
                                 const PluginDirectoryEnumerator &enumerator,
                                 const SharedLibraryLoader &loader) noexcept {
  try {
    return discover_plugins_impl(sources, enumerator, loader);
  } catch (...) {
    return {};
  }
}

DiscoveryResult discover_plugins(const PluginDiscoverySources &sources) noexcept {
  return discover_plugins(sources, platform_plugin_directory_enumerator(),
                          platform_library_loader());
}

TrustListResult read_trust_list(const TrustStore &store) noexcept {
  TrustListResult result;
  try {
    result.status = store.load(result.entries);
  } catch (...) {
    result.status = StoreStatus{ztick_status_internal_error, "trust list read threw"};
    result.entries.clear();
  }
  return result;
}

} // namespace ztick::plugin
