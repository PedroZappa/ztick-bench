// SPDX-License-Identifier: MIT
/**
 * @file dynamic_runtime_test.cpp
 * @brief Deterministic tests for the host's dynamic adapter runtime loader.
 *
 * Injected fake libraries cover success, invalid arguments, open and symbol failures,
 * malformed descriptors and SDK mismatch. A live-library counter proves that the
 * runtime handle keeps the module mapped until it is released and that no lifecycle
 * callback runs during loading.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/dynamic_runtime.hpp>

#include <ztick/sdk.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

/** @brief Number of fake libraries currently open. */
std::uint32_t g_live_libraries = 0U;
/** @brief Number of times the fake adapter's create callback was called. */
std::uint32_t g_create_calls = 0U;

/**
 * @brief Fake lifecycle callback that reveals accidental plugin execution.
 * @param out_adapter Receives a hypothetical instance; unused.
 * @return A success status; the call itself is the observable failure signal.
 */
std::int32_t fake_create(ztick_adapter **out_adapter) noexcept {
  (void)out_adapter;
  ++g_create_calls;
  return ztick_status_success;
}

/** @brief Storage for identity strings referenced by fake descriptors. */
struct FakeIdentity {
  /** @brief Stable identifier. */
  std::string identifier{"fake.runtime.adapter"};
  /** @brief Human-readable name. */
  std::string name{"Fake runtime adapter"};
};

/**
 * @brief Build a structurally valid descriptor backed by the given identity.
 * @param identity Storage that must outlive the returned descriptor.
 * @return A valid descriptor with one observable lifecycle callback.
 */
ztick_adapter_descriptor make_descriptor(FakeIdentity &identity) noexcept {
  ztick_adapter_descriptor descriptor{};
  descriptor.size = static_cast<std::uint32_t>(sizeof(descriptor));
  descriptor.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.size = static_cast<std::uint32_t>(sizeof(descriptor.info));
  descriptor.info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.identifier = identity.identifier.c_str();
  descriptor.info.name = identity.name.c_str();
  descriptor.info.adapter_version.size =
      static_cast<std::uint32_t>(sizeof(descriptor.info.adapter_version));
  descriptor.info.adapter_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.sdk_version.size =
      static_cast<std::uint32_t>(sizeof(descriptor.info.sdk_version));
  descriptor.info.sdk_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.sdk_version.major = ZTICK_SDK_VERSION_MAJOR;
  descriptor.info.sdk_version.minor = ZTICK_SDK_VERSION_MINOR;
  descriptor.info.sdk_version.patch = ZTICK_SDK_VERSION_PATCH;
  descriptor.ops.size = static_cast<std::uint32_t>(sizeof(descriptor.ops));
  descriptor.ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.ops.create = &fake_create;
  return descriptor;
}

/**
 * @brief Fake shared library that optionally exposes the descriptor data symbol.
 */
class FakeSharedLibrary final : public ztick::plugin::SharedLibrary {
public:
  /**
   * @brief Construct one fake library.
   * @param descriptor Descriptor to expose, or NULL.
   * @param expose_symbol Whether to resolve the standard export symbol.
   */
  FakeSharedLibrary(const ztick_adapter_descriptor *descriptor, bool expose_symbol) noexcept
      : descriptor_(descriptor), expose_symbol_(expose_symbol) {
    ++g_live_libraries;
  }

  /** @brief Record that the owning handle released the module. */
  ~FakeSharedLibrary() override { --g_live_libraries; }

  /**
   * @brief Resolve the descriptor data symbol without calling anything.
   * @param name NUL-terminated symbol name.
   * @return Descriptor address when exposed, otherwise NULL.
   */
  const void *resolve(const char *name) noexcept override {
    if (expose_symbol_ && name != nullptr && descriptor_ != nullptr &&
        std::string(name) == ZTICK_ADAPTER_EXPORT_SYMBOL) {
      return descriptor_;
    }
    return nullptr;
  }

private:
  /** @brief Descriptor exposed by this fake module. */
  const ztick_adapter_descriptor *descriptor_;
  /** @brief Whether the export symbol is present. */
  bool expose_symbol_;
};

/**
 * @brief Configurable fake factory for shared libraries.
 */
class FakeSharedLibraryLoader final : public ztick::plugin::SharedLibraryLoader {
public:
  /**
   * @brief Construct one fake loader.
   * @param descriptor Descriptor exposed by opened libraries.
   * @param open_fails Whether open attempts fail.
   * @param expose_symbol Whether the export symbol is present.
   */
  FakeSharedLibraryLoader(const ztick_adapter_descriptor *descriptor, bool open_fails,
                          bool expose_symbol) noexcept
      : descriptor_(descriptor), open_fails_(open_fails), expose_symbol_(expose_symbol) {}

  /**
   * @brief Open one fake library.
   * @param path Requested path; recorded for assertions.
   * @return Owning fake library, or NULL when configured to fail.
   */
  std::unique_ptr<ztick::plugin::SharedLibrary> open(const char *path) const noexcept override {
    if (path != nullptr) {
      last_path = path;
    }
    if (open_fails_) {
      return nullptr;
    }
    return std::make_unique<FakeSharedLibrary>(descriptor_, expose_symbol_);
  }

  /** @brief Last requested path; mutable for the const open contract. */
  mutable std::string last_path;

private:
  /** @brief Descriptor exposed by opened libraries. */
  const ztick_adapter_descriptor *descriptor_;
  /** @brief Whether to simulate an open failure. */
  bool open_fails_;
  /** @brief Whether the export symbol is present. */
  bool expose_symbol_;
};

} // namespace

/**
 * @brief Verify a successful load retains the module and never calls plugin code.
 */
TEST_CASE("dynamic runtime load keeps the module mapped without plugin execution",
          "[plugin][loader][runtime]") {
  g_live_libraries = 0U;
  g_create_calls = 0U;
  FakeIdentity identity;
  const ztick_adapter_descriptor descriptor = make_descriptor(identity);
  FakeSharedLibraryLoader loader(&descriptor, false, true);

  ztick::plugin::RuntimeLoadResult result =
      ztick::plugin::load_dynamic_adapter_runtime("runtime-module.so", loader);

  REQUIRE(result.loaded());
  CHECK(result.status == ztick_status_success);
  CHECK(result.reason == nullptr);
  CHECK(loader.last_path == "runtime-module.so");
  CHECK(g_live_libraries == 1U);
  CHECK(g_create_calls == 0U);
  CHECK(result.adapter.valid());
  CHECK(result.adapter.get() == &descriptor);

  result.adapter = ztick::plugin::LoadedAdapter{};
  CHECK(g_live_libraries == 0U);
  CHECK_FALSE(result.adapter.valid());
  CHECK(result.adapter.get() == nullptr);
}

/**
 * @brief Verify every load failure is explicit and leaves no module open.
 */
TEST_CASE("dynamic runtime load reports load and validation failures",
          "[plugin][loader][runtime]") {
  g_live_libraries = 0U;
  FakeIdentity identity;
  ztick_adapter_descriptor descriptor = make_descriptor(identity);

  SECTION("invalid path") {
    FakeSharedLibraryLoader loader(&descriptor, false, true);
    const ztick::plugin::RuntimeLoadResult result =
        ztick::plugin::load_dynamic_adapter_runtime("", loader);
    CHECK(result.status == ztick_status_invalid_argument);
    CHECK_FALSE(result.loaded());
    CHECK(g_live_libraries == 0U);
  }

  SECTION("open failure") {
    FakeSharedLibraryLoader loader(&descriptor, true, true);
    const ztick::plugin::RuntimeLoadResult result =
        ztick::plugin::load_dynamic_adapter_runtime("module.so", loader);
    CHECK(result.status == ztick_status_internal_error);
    CHECK_FALSE(result.loaded());
  }

  SECTION("missing export symbol") {
    FakeSharedLibraryLoader loader(&descriptor, false, false);
    const ztick::plugin::RuntimeLoadResult result =
        ztick::plugin::load_dynamic_adapter_runtime("module.so", loader);
    CHECK(result.status == ztick_status_internal_error);
    CHECK_FALSE(result.loaded());
    CHECK(g_live_libraries == 0U);
  }

  SECTION("malformed descriptor") {
    descriptor.size = 0U;
    FakeSharedLibraryLoader loader(&descriptor, false, true);
    const ztick::plugin::RuntimeLoadResult result =
        ztick::plugin::load_dynamic_adapter_runtime("module.so", loader);
    CHECK(result.status == ztick_status_invalid_argument);
    CHECK_FALSE(result.loaded());
    CHECK(g_live_libraries == 0U);
  }

  SECTION("incompatible SDK version") {
    descriptor.info.sdk_version.major += 1U;
    FakeSharedLibraryLoader loader(&descriptor, false, true);
    const ztick::plugin::RuntimeLoadResult result =
        ztick::plugin::load_dynamic_adapter_runtime("module.so", loader);
    CHECK(result.status == ztick_status_incompatible);
    CHECK_FALSE(result.loaded());
    CHECK(g_live_libraries == 0U);
  }
}
