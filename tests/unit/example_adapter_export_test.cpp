// SPDX-License-Identifier: MIT
/**
 * @file example_adapter_export_test.cpp
 * @brief Verify the Phase 3 example descriptors are inspectable without code execution.
 *
 * The static example is linked directly; the dynamic example is loaded as a
 * module and its read-only data symbol is read through dlsym. No lifecycle
 * callback is called, so this does not claim either test app is complete.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/sdk.h>

#include <cstdint>
#include <cstring>

#include <dlfcn.h>

/** @brief Descriptor published by the static example adapter. */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor;

namespace {

/**
 * @brief RAII owner for a dynamically loaded example module.
 */
struct LibraryHandle {
  /** @brief Native library handle, or NULL when no module is loaded. */
  void *handle = nullptr;

  /**
   * @brief Take ownership of an already opened library.
   * @param library Native handle returned by dlopen, or NULL.
   */
  explicit LibraryHandle(void *library) noexcept : handle(library) {}

  /** @brief Close the library when the handle goes out of scope. */
  ~LibraryHandle() {
    if (handle != nullptr) {
      dlclose(handle);
    }
  }

  /** @brief Copying a library handle is disabled. */
  LibraryHandle(const LibraryHandle &) = delete;

  /** @brief Copy assignment is disabled. */
  LibraryHandle &operator=(const LibraryHandle &) = delete;
};

/**
 * @brief Check descriptor headers, identity and SDK version without calling ops.
 * @param descriptor Descriptor to inspect; may be NULL.
 * @param identifier Expected stable identifier.
 * @return True when the descriptor is a valid metadata-only example.
 */
bool has_scaffold_identity(const ztick_adapter_descriptor *descriptor, const char *identifier) {
  if (descriptor == nullptr || descriptor->size < sizeof(*descriptor) ||
      descriptor->version != ZTICK_STRUCT_LAYOUT_VERSION ||
      descriptor->info.size < sizeof(descriptor->info) ||
      descriptor->info.version != ZTICK_STRUCT_LAYOUT_VERSION ||
      descriptor->ops.size < sizeof(descriptor->ops) ||
      descriptor->ops.version != ZTICK_STRUCT_LAYOUT_VERSION ||
      descriptor->info.identifier == nullptr ||
      std::strcmp(descriptor->info.identifier, identifier) != 0) {
    return false;
  }
  const ztick_version &adapter_version = descriptor->info.adapter_version;
  const ztick_version &sdk_version = descriptor->info.sdk_version;
  return adapter_version.size >= sizeof(adapter_version) &&
         adapter_version.version == ZTICK_STRUCT_LAYOUT_VERSION &&
         sdk_version.size >= sizeof(sdk_version) &&
         sdk_version.version == ZTICK_STRUCT_LAYOUT_VERSION &&
         sdk_version.major == ZTICK_SDK_VERSION_MAJOR &&
         sdk_version.minor == ZTICK_SDK_VERSION_MINOR &&
         sdk_version.patch == ZTICK_SDK_VERSION_PATCH;
}

/**
 * @brief Check that an example declares no executable lifecycle callbacks.
 * @param ops Lifecycle table to inspect.
 * @return True when every callback pointer is NULL.
 */
bool has_no_callbacks(const ztick_adapter_ops &ops) {
  return ops.create == nullptr && ops.destroy == nullptr && ops.describe == nullptr &&
         ops.configure == nullptr && ops.prepare == nullptr && ops.process == nullptr &&
         ops.finish == nullptr && ops.validate == nullptr && ops.report == nullptr;
}

} // namespace

/**
 * @brief Verify both examples expose read-only metadata and no lifecycle callbacks.
 */
TEST_CASE("example adapters expose metadata only", "[sdk][scaffold]") {
  CHECK(has_scaffold_identity(&ztick_test_app_static_descriptor, "ztick.test.static.minimal"));
  CHECK(has_no_callbacks(ztick_test_app_static_descriptor.ops));

  LibraryHandle library{dlopen(ZTICK_DYNAMIC_EXAMPLE_PATH, RTLD_NOW | RTLD_LOCAL)};
  REQUIRE(library.handle != nullptr);
  void *symbol = dlsym(library.handle, ZTICK_ADAPTER_EXPORT_SYMBOL);
  REQUIRE(symbol != nullptr);
  const auto *descriptor = static_cast<const ztick_adapter_descriptor *>(symbol);
  CHECK(has_scaffold_identity(descriptor, "ztick.test.dynamic.scaffold"));
  CHECK(has_no_callbacks(descriptor->ops));
}
