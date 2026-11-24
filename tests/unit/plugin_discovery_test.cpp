// SPDX-License-Identifier: MIT
/**
 * @file plugin_discovery_test.cpp
 * @brief Deterministic tests for metadata-only plugin discovery enumeration.
 *
 * All directory reads and library opens are injected. The fake descriptor owns a
 * lifecycle callback that counts invocations; discovery must leave that counter at
 * zero (BR §6.1).
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/discovery.hpp>
#include <ztick/sdk.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ztick::plugin::DiscoveredPlugin;
using ztick::plugin::DiscoveryResult;
using ztick::plugin::PluginDirectoryEnumerator;
using ztick::plugin::PluginDiscoverySources;
using ztick::plugin::PluginSource;
using ztick::plugin::SharedLibrary;
using ztick::plugin::SharedLibraryLoader;
using ztick::plugin::StoreStatus;

/** @brief Counts calls into the fake plugin's observable lifecycle callback. */
std::uint32_t g_discovery_create_calls = 0;

/**
 * @brief Fake adapter callback used to detect accidental plugin execution.
 * @param out_adapter Receives a hypothetical instance; unused.
 * @return Success; the increment is the observable failure signal.
 */
std::int32_t fake_discovery_create(ztick_adapter **out_adapter) noexcept {
  (void)out_adapter;
  ++g_discovery_create_calls;
  return ztick_status_success;
}

/** @brief String storage referenced by the fake descriptor. */
struct FakeDiscoveryIdentity {
  /** @brief Stable identifier. */
  std::string identifier{"fake.discovery.adapter"};
  /** @brief Human-readable name. */
  std::string name{"Fake discovery adapter"};
  /** @brief Optional metadata blob. */
  std::string metadata{"discovery metadata"};
};

/**
 * @brief Build a structurally valid descriptor backed by the supplied identity.
 * @param identity Storage that must outlive the returned descriptor.
 * @return Descriptor with one observable lifecycle callback.
 */
ztick_adapter_descriptor make_discovery_descriptor(FakeDiscoveryIdentity &identity) noexcept {
  ztick_adapter_descriptor descriptor{};
  descriptor.size = sizeof(descriptor);
  descriptor.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.size = sizeof(descriptor.info);
  descriptor.info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.identifier = identity.identifier.c_str();
  descriptor.info.name = identity.name.c_str();
  descriptor.info.adapter_version.size = sizeof(descriptor.info.adapter_version);
  descriptor.info.adapter_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.adapter_version.major = 2U;
  descriptor.info.adapter_version.minor = 3U;
  descriptor.info.adapter_version.patch = 4U;
  descriptor.info.sdk_version.size = sizeof(descriptor.info.sdk_version);
  descriptor.info.sdk_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.sdk_version.major = ZTICK_SDK_VERSION_MAJOR;
  descriptor.info.sdk_version.minor = ZTICK_SDK_VERSION_MINOR;
  descriptor.info.sdk_version.patch = ZTICK_SDK_VERSION_PATCH;
  descriptor.info.capabilities = 0x1234U;
  descriptor.info.metadata = identity.metadata.c_str();
  descriptor.ops.size = sizeof(descriptor.ops);
  descriptor.ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.ops.create = &fake_discovery_create;
  return descriptor;
}

/** @brief Fake shared library exposing one descriptor as a data symbol. */
class FakeDiscoveryLibrary final : public SharedLibrary {
public:
  /**
   * @brief Construct a library exposing one descriptor.
   * @param descriptor Descriptor to expose, or NULL for no symbol.
   */
  explicit FakeDiscoveryLibrary(const ztick_adapter_descriptor *descriptor) noexcept
      : descriptor_(descriptor) {}

  /**
   * @brief Resolve the descriptor export symbol without calling anything.
   * @param name NUL-terminated symbol name.
   * @return Descriptor address for the export symbol, otherwise NULL.
   */
  const void *resolve(const char *name) noexcept override {
    if (name != nullptr && descriptor_ != nullptr &&
        std::string(name) == ZTICK_ADAPTER_EXPORT_SYMBOL) {
      return descriptor_;
    }
    return nullptr;
  }

private:
  /** @brief Descriptor exposed by this fake module. */
  const ztick_adapter_descriptor *descriptor_;
};

/** @brief Configurable fake loader for deterministic discovery results. */
class FakeDiscoveryLoader final : public SharedLibraryLoader {
public:
  /**
   * @brief Construct a loader with one descriptor and one failing path.
   * @param descriptor Descriptor exposed by successful opens.
   * @param failing_path Path whose open attempt should fail.
   */
  FakeDiscoveryLoader(const ztick_adapter_descriptor *descriptor, std::string failing_path) noexcept
      : descriptor_(descriptor), failing_path_(std::move(failing_path)) {}

  /**
   * @brief Open one fake module and record the request.
   * @param path Requested library path.
   * @return Fake library, or NULL for the configured failing path.
   */
  std::unique_ptr<SharedLibrary> open(const char *path) const noexcept override {
    const std::string requested = path == nullptr ? std::string() : std::string(path);
    opened.push_back(requested);
    if (requested == failing_path_) {
      return nullptr;
    }
    return std::make_unique<FakeDiscoveryLibrary>(descriptor_);
  }

  /** @brief Paths passed to @ref open, in call order. */
  mutable std::vector<std::string> opened;

private:
  /** @brief Descriptor exposed by successful opens. */
  const ztick_adapter_descriptor *descriptor_;
  /** @brief Path that simulates an infrastructure failure. */
  std::string failing_path_;
};

/** @brief Fake directory reader with deterministic contents. */
class FakeDirectoryEnumerator final : public PluginDirectoryEnumerator {
public:
  /**
   * @brief Construct an enumerator from directory contents.
   * @param contents Mapping from directory path to candidate paths.
   */
  explicit FakeDirectoryEnumerator(std::map<std::string, std::vector<std::string>> contents)
      : contents_(std::move(contents)) {}

  /**
   * @brief Return the configured candidates for one directory.
   * @param directory Directory to inspect.
   * @param error Receiver for an explicit failure reason.
   * @return Configured candidate paths, or an empty list for an absent directory.
   */
  std::vector<std::string> list(const std::string &directory, std::string *error) const override {
    const auto found = contents_.find(directory);
    if (found == contents_.end()) {
      if (error != nullptr) {
        *error = "directory is not present";
      }
      return {};
    }
    if (error != nullptr) {
      error->clear();
    }
    return found->second;
  }

private:
  /** @brief Configured directory contents. */
  std::map<std::string, std::vector<std::string>> contents_;
};

/** @brief Trust store whose read always fails explicitly. */
class FailingTrustStore final : public ztick::plugin::TrustStore {
public:
  /**
   * @brief Report a storage failure without modifying the entries.
   * @param entries Receives no data.
   * @return Explicit internal error.
   */
  StoreStatus load(std::vector<ztick::plugin::PluginIdentity> &entries) const noexcept override {
    entries.clear();
    return StoreStatus{ztick_status_internal_error, "trust list unavailable"};
  }

  /**
   * @brief Refuse to save.
   * @param entries Entries to write; unused.
   * @return Explicit internal error.
   */
  StoreStatus
  save(const std::vector<ztick::plugin::PluginIdentity> &entries) const noexcept override {
    (void)entries;
    return StoreStatus{ztick_status_internal_error, "trust list unavailable"};
  }
};

} // namespace

/**
 * @brief Verify discovery source tokens are stable.
 */
TEST_CASE("plugin discovery source tokens are stable", "[plugin][discovery][p410]") {
  using ztick::plugin::plugin_source_token;
  REQUIRE(plugin_source_token(PluginSource::explicit_path) == "explicit_path");
  REQUIRE(plugin_source_token(PluginSource::user_directory) == "user_directory");
  REQUIRE(plugin_source_token(PluginSource::project_directory) == "project_directory");
  REQUIRE(plugin_source_token(static_cast<PluginSource>(99)) == "explicit_path");
}

/**
 * @brief Verify the POSIX project-local directory is present in default sources.
 */
TEST_CASE("default plugin sources include the project directory", "[plugin][discovery][p410]") {
  const PluginDiscoverySources sources = ztick::plugin::default_plugin_discovery_sources();
  REQUIRE(sources.project_directory == ".ztick-bench/plugins");
  REQUIRE(sources.explicit_paths.empty());
}

/**
 * @brief Verify three sources are inspected in order without executing plugins.
 */
TEST_CASE("plugin discovery enumerates three sources without execution",
          "[plugin][discovery][p410]") {
  g_discovery_create_calls = 0;
  FakeDiscoveryIdentity identity;
  const ztick_adapter_descriptor descriptor = make_discovery_descriptor(identity);
  FakeDiscoveryLoader loader(&descriptor, "user/bad.so");
  const FakeDirectoryEnumerator enumerator(
      {{"user", {"user/user.so", "user/bad.so"}}, {"project", {"project/project.so"}}});

  PluginDiscoverySources sources;
  sources.explicit_paths = {"explicit.so", "user/user.so"};
  sources.user_directory = "user";
  sources.project_directory = "project";

  const DiscoveryResult result = ztick::plugin::discover_plugins(sources, enumerator, loader);
  REQUIRE(result.plugins.size() == 4U);
  REQUIRE(result.plugins[0U].path == "explicit.so");
  REQUIRE(result.plugins[0U].source == PluginSource::explicit_path);
  REQUIRE(result.plugins[1U].path == "user/user.so");
  REQUIRE(result.plugins[1U].source == PluginSource::explicit_path);
  REQUIRE(result.plugins[2U].path == "user/bad.so");
  REQUIRE_FALSE(result.plugins[2U].inspection.has_metadata());
  REQUIRE(result.plugins[2U].inspection.reason != nullptr);
  REQUIRE(result.plugins[3U].path == "project/project.so");
  REQUIRE(result.plugins[3U].source == PluginSource::project_directory);
  REQUIRE(result.plugins[3U].inspection.has_metadata());

  REQUIRE(result.directories.size() == 2U);
  REQUIRE(result.directories[0U].available);
  REQUIRE(result.directories[0U].reason.empty());
  REQUIRE(result.directories[1U].available);
  REQUIRE(loader.opened.size() == 4U);
  REQUIRE(g_discovery_create_calls == 0U);
}

/**
 * @brief Verify unreadable directories are reported rather than silently omitted.
 */
TEST_CASE("plugin discovery reports unavailable directories", "[plugin][discovery][p410]") {
  FakeDiscoveryIdentity identity;
  const ztick_adapter_descriptor descriptor = make_discovery_descriptor(identity);
  FakeDiscoveryLoader loader(&descriptor, "");
  const FakeDirectoryEnumerator enumerator({{"project", {"project/project.so"}}});
  PluginDiscoverySources sources;
  sources.user_directory = "absent";
  sources.project_directory = "project";

  const DiscoveryResult result = ztick::plugin::discover_plugins(sources, enumerator, loader);
  REQUIRE(result.directories.size() == 2U);
  REQUIRE_FALSE(result.directories[0U].available);
  REQUIRE(result.directories[0U].path == "absent");
  REQUIRE(result.directories[0U].reason == "directory is not present");
  REQUIRE(result.directories[1U].available);
  REQUIRE(result.plugins.size() == 1U);
  REQUIRE(result.plugins.front().path == "project/project.so");
}

/**
 * @brief Verify trust-list reads never treat a storage failure as an empty list.
 */
TEST_CASE("read_trust_list preserves store status", "[plugin][discovery][p410]") {
  ztick::plugin::MemoryTrustStore memory;
  ztick::plugin::PluginIdentity entry;
  entry.identifier = "trusted.plugin";
  entry.name = "Trusted plugin";
  REQUIRE(memory.save({entry}).ok());

  const ztick::plugin::TrustListResult loaded = ztick::plugin::read_trust_list(memory);
  REQUIRE(loaded.status.ok());
  REQUIRE(loaded.entries.size() == 1U);
  REQUIRE(loaded.entries.front().identifier == "trusted.plugin");

  const FailingTrustStore failing;
  const ztick::plugin::TrustListResult failed = ztick::plugin::read_trust_list(failing);
  REQUIRE_FALSE(failed.status.ok());
  REQUIRE(failed.entries.empty());
}
