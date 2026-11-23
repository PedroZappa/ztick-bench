// SPDX-License-Identifier: MIT
/**
 * @file dynamic_loader_test.cpp
 * @brief Deterministic tests for metadata-only dynamic adapter inspection.
 *
 * An injected fake loader drives every success and failure path, so the tests do
 * not depend on the host filesystem. One additional case uses the real platform
 * loader on the in-tree example module when that target is available.
 *
 * The fake descriptor's `create` callback records calls in a global counter.
 * Inspection must leave it at zero: that is the executable proof that no plugin
 * callback is invoked (BR §6.1).
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/dynamic_loader.hpp>

#include <ztick/sdk.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

/** @brief Counts calls into the fake plugin's lifecycle callback. */
std::uint32_t g_fake_create_calls = 0;

/**
 * @brief Fake adapter callback that would reveal accidental plugin execution.
 * @param out_adapter Receives a hypothetical instance; unused.
 * @return A success status; the call itself is the observable failure signal.
 */
std::int32_t fake_create(ztick_adapter **out_adapter) noexcept {
  (void)out_adapter;
  ++g_fake_create_calls;
  return ztick_status_success;
}

/**
 * @brief Minimal fake shared library exposing one resolvable data symbol.
 */
class FakeSharedLibrary final : public ztick::plugin::SharedLibrary {
public:
  /**
   * @brief Construct a library with a descriptor at the export symbol.
   * @param descriptor Descriptor to expose, or NULL to expose no symbol.
   */
  explicit FakeSharedLibrary(const ztick_adapter_descriptor *descriptor) noexcept
      : descriptor_(descriptor) {}

  /**
   * @brief Resolve the descriptor data symbol without calling anything.
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

/**
 * @brief Configurable fake factory for shared libraries.
 */
class FakeSharedLibraryLoader final : public ztick::plugin::SharedLibraryLoader {
public:
  /**
   * @brief Construct a loader with a descriptor and an optional open failure.
   * @param descriptor Descriptor the opened library should expose.
   * @param open_fails When true, every open attempt fails.
   */
  FakeSharedLibraryLoader(const ztick_adapter_descriptor *descriptor, bool open_fails) noexcept
      : descriptor_(descriptor), open_fails_(open_fails) {}

  /**
   * @brief Open the configured fake library.
   * @param path Requested library path; recorded for assertions.
   * @return A fake library, or NULL when configured to fail.
   */
  std::unique_ptr<ztick::plugin::SharedLibrary> open(const char *path) const noexcept override {
    if (path != nullptr) {
      last_path_ = path;
    }
    if (open_fails_) {
      return nullptr;
    }
    return std::make_unique<FakeSharedLibrary>(descriptor_);
  }

  /** @brief Last path passed to @ref open, or an empty string. */
  const std::string &last_path() const noexcept { return last_path_; }

private:
  /** @brief Descriptor exposed by opened libraries. */
  const ztick_adapter_descriptor *descriptor_;
  /** @brief Whether to simulate a platform open failure. */
  bool open_fails_;
  /** @brief Most recently requested path; mutable for the const open contract. */
  mutable std::string last_path_;
};

/**
 * @brief Storage for identity strings referenced by a fake descriptor.
 */
struct FakeIdentity {
  /** @brief Stable identifier. */
  std::string identifier{"fake.dynamic.adapter"};
  /** @brief Human-readable name. */
  std::string name{"Fake dynamic adapter"};
  /** @brief Optional metadata blob. */
  std::string metadata{"fake metadata"};
};

/**
 * @brief Build a structurally valid descriptor backed by the given identity.
 * @param identity Storage that must outlive the returned descriptor.
 * @return A valid descriptor with one observable lifecycle callback.
 */
ztick_adapter_descriptor make_descriptor(FakeIdentity &identity) noexcept {
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
  descriptor.info.capabilities = 0x5A5AU;
  descriptor.info.metadata = identity.metadata.c_str();
  descriptor.ops.size = sizeof(descriptor.ops);
  descriptor.ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.ops.create = &fake_create;
  return descriptor;
}

} // namespace

/**
 * @brief Verify metadata is copied into owning storage and no callback runs.
 */
TEST_CASE("dynamic loader inspects metadata without executing plugin code", "[plugin][loader]") {
  g_fake_create_calls = 0;
  FakeIdentity identity;
  ztick_adapter_descriptor descriptor = make_descriptor(identity);
  FakeSharedLibraryLoader loader(&descriptor, false);

  const ztick::plugin::InspectionResult result =
      ztick::plugin::inspect_dynamic_adapter("fake-module.so", loader);

  REQUIRE(result.status == ztick_status_success);
  REQUIRE(result.has_metadata());
  CHECK(result.reason == nullptr);
  CHECK(loader.last_path() == "fake-module.so");
  CHECK(result.metadata.identifier == "fake.dynamic.adapter");
  CHECK(result.metadata.name == "Fake dynamic adapter");
  CHECK(result.metadata.adapter_major == 2U);
  CHECK(result.metadata.adapter_minor == 3U);
  CHECK(result.metadata.adapter_patch == 4U);
  CHECK(result.metadata.sdk_major == ZTICK_SDK_VERSION_MAJOR);
  CHECK(result.metadata.sdk_minor == ZTICK_SDK_VERSION_MINOR);
  CHECK(result.metadata.sdk_patch == ZTICK_SDK_VERSION_PATCH);
  CHECK(result.metadata.capabilities == 0x5A5AU);
  CHECK(result.metadata.metadata == "fake metadata");
  CHECK(g_fake_create_calls == 0U);

  // Owning copy: later changes to the module's data cannot alter the result.
  identity.identifier = "changed.after.inspection";
  CHECK(result.metadata.identifier == "fake.dynamic.adapter");
}

/**
 * @brief Verify bad arguments and load/ABI failures are classified and explained.
 */
TEST_CASE("dynamic loader reports argument and infrastructure failures", "[plugin][loader]") {
  FakeIdentity identity;
  ztick_adapter_descriptor descriptor = make_descriptor(identity);
  FakeSharedLibraryLoader working_loader(&descriptor, false);
  FakeSharedLibraryLoader failing_loader(&descriptor, true);

  const ztick::plugin::InspectionResult null_path =
      ztick::plugin::inspect_dynamic_adapter(nullptr, working_loader);
  CHECK(null_path.status == ztick_status_invalid_argument);
  CHECK_FALSE(null_path.has_metadata());
  CHECK(null_path.reason != nullptr);

  const ztick::plugin::InspectionResult empty_path =
      ztick::plugin::inspect_dynamic_adapter("", working_loader);
  CHECK(empty_path.status == ztick_status_invalid_argument);
  CHECK_FALSE(empty_path.has_metadata());

  const ztick::plugin::InspectionResult open_failure =
      ztick::plugin::inspect_dynamic_adapter("missing.so", failing_loader);
  CHECK(open_failure.status == ztick_status_internal_error);
  CHECK_FALSE(open_failure.has_metadata());
  CHECK(std::string(open_failure.reason) == "cannot open shared library");
  CHECK(failing_loader.last_path() == "missing.so");

  FakeSharedLibraryLoader no_symbol_loader(nullptr, false);
  const ztick::plugin::InspectionResult missing_symbol =
      ztick::plugin::inspect_dynamic_adapter("no-symbol.so", no_symbol_loader);
  CHECK(missing_symbol.status == ztick_status_internal_error);
  CHECK_FALSE(missing_symbol.has_metadata());
  CHECK(std::string(missing_symbol.reason) == "adapter descriptor symbol not found");
}

/**
 * @brief Verify structural descriptor checks are shared with static registration.
 */
TEST_CASE("dynamic loader rejects malformed descriptors", "[plugin][loader]") {
  FakeIdentity identity;
  const ztick_adapter_descriptor valid = make_descriptor(identity);

  ztick_adapter_descriptor descriptor = valid;
  SECTION("descriptor size") { descriptor.size = 1U; }
  SECTION("descriptor layout version") { descriptor.version = 0U; }
  SECTION("info header") { descriptor.info.size = 1U; }
  SECTION("ops header") { descriptor.ops.version = 0U; }
  SECTION("adapter version header") { descriptor.info.adapter_version.size = 1U; }
  SECTION("sdk version header") { descriptor.info.sdk_version.version = 0U; }
  SECTION("empty identifier") { descriptor.info.identifier = ""; }
  SECTION("missing name") { descriptor.info.name = nullptr; }

  FakeSharedLibraryLoader loader(&descriptor, false);
  const ztick::plugin::InspectionResult result =
      ztick::plugin::inspect_dynamic_adapter("malformed.so", loader);
  CHECK(result.status == ztick_status_invalid_argument);
  CHECK_FALSE(result.has_metadata());
  CHECK(std::string(result.reason) == "malformed adapter descriptor");
}

/**
 * @brief Verify SDK compatibility is checked before metadata is accepted.
 */
TEST_CASE("dynamic loader refuses incompatible SDK versions", "[plugin][loader]") {
  FakeIdentity identity;
  ztick_adapter_descriptor descriptor = make_descriptor(identity);
  descriptor.info.sdk_version.major = ZTICK_SDK_VERSION_MAJOR + 1U;
  FakeSharedLibraryLoader loader(&descriptor, false);

  const ztick::plugin::InspectionResult result =
      ztick::plugin::inspect_dynamic_adapter("incompatible.so", loader);
  CHECK(result.status == ztick_status_incompatible);
  CHECK_FALSE(result.has_metadata());
  CHECK(std::string(result.reason) == "incompatible adapter SDK version");
}

/**
 * @brief Verify the platform loader is available in every build configuration.
 */
TEST_CASE("platform shared-library loader is available", "[plugin][loader]") {
  const ztick::plugin::SharedLibraryLoader &loader = ztick::plugin::platform_library_loader();
  CHECK(loader.open("ztick-no-such-library.so") == nullptr);

  // The convenience overload delegates to the same validation with no injection.
  CHECK(ztick::plugin::inspect_dynamic_adapter(nullptr).status == ztick_status_invalid_argument);
  CHECK(ztick::plugin::inspect_dynamic_adapter("").status == ztick_status_invalid_argument);
}

#ifdef ZTICK_DYNAMIC_EXAMPLE_PATH
/**
 * @brief Verify the real platform loader inspects the in-tree example module.
 */
TEST_CASE("platform loader inspects the dynamic example module", "[plugin][loader]") {
  const ztick::plugin::InspectionResult result =
      ztick::plugin::inspect_dynamic_adapter(ZTICK_DYNAMIC_EXAMPLE_PATH);
  REQUIRE(result.status == ztick_status_success);
  REQUIRE(result.has_metadata());
  CHECK(result.metadata.identifier == "ztick.test.dynamic.app_b");
  CHECK(result.metadata.name == "Dynamic engine SDK test adapter");
  CHECK((result.metadata.capabilities & ZTICK_CAPABILITY_ENGINE) != 0U);
  CHECK((result.metadata.capabilities & ZTICK_CAPABILITY_INTERNAL_THREADING) != 0U);
  CHECK(result.metadata.sdk_major == ZTICK_SDK_VERSION_MAJOR);
  CHECK(result.metadata.sdk_minor == ZTICK_SDK_VERSION_MINOR);
  CHECK(result.metadata.sdk_patch == ZTICK_SDK_VERSION_PATCH);
}
#endif
