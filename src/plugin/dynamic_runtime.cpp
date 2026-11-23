// SPDX-License-Identifier: MIT
/**
 * @file dynamic_runtime.cpp
 * @brief Implementation of the host's dynamic adapter runtime loader.
 *
 * The loader returns an owning handle that keeps the module mapped while a harness
 * calls lifecycle operations. Structural validation and SDK compatibility are shared
 * with metadata inspection so the two paths cannot disagree about descriptor safety
 * (SDK_SPECIFICATION §2, §10).
 */

#include <ztick/plugin/dynamic_runtime.hpp>

#include <ztick/sdk_validation.hpp>

#include <utility>

namespace ztick::plugin {

namespace {

/**
 * @brief Build a failed runtime-load result with a stable reason.
 * @param status `ztick_status` code describing the failure.
 * @param reason Static, NUL-terminated explanation; must not be NULL.
 * @return Result with no loaded module and the supplied failure details.
 */
RuntimeLoadResult failure(std::int32_t status, const char *reason) noexcept {
  return RuntimeLoadResult{status, reason, LoadedAdapter{}};
}

} // namespace

LoadedAdapter::LoadedAdapter(std::unique_ptr<SharedLibrary> library,
                             const ztick_adapter_descriptor *descriptor) noexcept
    : library_(std::move(library)), descriptor_(descriptor) {}

bool LoadedAdapter::valid() const noexcept { return library_ != nullptr && descriptor_ != nullptr; }

const ztick_adapter_descriptor *LoadedAdapter::get() const noexcept { return descriptor_; }

// Consumed by in-tree harnesses and tests; cppcheck scans src/ but not those targets.
// cppcheck-suppress unusedFunction
bool RuntimeLoadResult::loaded() const noexcept {
  return status == ztick_status_success && adapter.valid();
}

RuntimeLoadResult load_dynamic_adapter_runtime(const char *path,
                                               const SharedLibraryLoader &loader) noexcept {
  if (path == nullptr || path[0] == '\0') {
    return failure(ztick_status_invalid_argument, "invalid library path");
  }

  try {
    std::unique_ptr<SharedLibrary> library = loader.open(path);
    if (library == nullptr) {
      return failure(ztick_status_internal_error, "cannot open shared library");
    }
    const void *symbol = library->resolve(ZTICK_ADAPTER_EXPORT_SYMBOL);
    if (symbol == nullptr) {
      return failure(ztick_status_internal_error, "adapter descriptor symbol not found");
    }
    const auto *descriptor = static_cast<const ztick_adapter_descriptor *>(symbol);
    if (!sdk::detail::has_valid_descriptor_layout(descriptor)) {
      return failure(ztick_status_invalid_argument, "malformed adapter descriptor");
    }
    if (ztick_sdk_is_compatible(&descriptor->info.sdk_version) != ztick_status_success) {
      return failure(ztick_status_incompatible, "incompatible adapter SDK version");
    }

    RuntimeLoadResult result;
    result.status = ztick_status_success;
    result.reason = nullptr;
    result.adapter = LoadedAdapter{std::move(library), descriptor};
    return result;
  } catch (...) {
    return failure(ztick_status_internal_error, "runtime load failed");
  }
}

RuntimeLoadResult load_dynamic_adapter_runtime(const char *path) noexcept {
  return load_dynamic_adapter_runtime(path, platform_library_loader());
}

} // namespace ztick::plugin
