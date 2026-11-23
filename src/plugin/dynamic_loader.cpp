// SPDX-License-Identifier: MIT
/**
 * @file dynamic_loader.cpp
 * @brief Portable metadata-only inspection of dynamic SDK adapters.
 *
 * Resolves the read-only descriptor data symbol through a platform loader, applies
 * the shared SDK structural and version checks, and copies identity fields into
 * owning storage. The library handle is released before returning, so inspection
 * leaves no registered descriptor, running instance or executed callback behind
 * (BR §6.1, SDK_SPECIFICATION §10).
 */

#include <ztick/plugin/dynamic_loader.hpp>

#include <ztick/sdk.h>
#include <ztick/sdk_validation.hpp>

namespace ztick::plugin {
namespace {

/**
 * @brief Build a failure result with a stable explanatory string.
 * @param status `ztick_status` code to report.
 * @param reason Static, NUL-terminated explanation; must not be NULL.
 * @return Result with no metadata and the given failure details.
 */
InspectionResult failure(std::int32_t status, const char *reason) noexcept {
  return InspectionResult{status, reason, AdapterMetadata{}};
}

/**
 * @brief Copy one version triple without its ABI header fields.
 * @param version Version to copy; header validity is checked by the caller.
 * @param major Receives the major component.
 * @param minor Receives the minor component.
 * @param patch Receives the patch component.
 */
void copy_version(const ztick_version &version, std::uint32_t &major, std::uint32_t &minor,
                  std::uint32_t &patch) {
  major = version.major;
  minor = version.minor;
  patch = version.patch;
}

} // namespace

InspectionResult inspect_dynamic_adapter(const char *path,
                                         const SharedLibraryLoader &loader) noexcept {
  if (path == nullptr || path[0] == '\0') {
    return failure(ztick_status_invalid_argument, "invalid library path");
  }

  try {
    const std::unique_ptr<SharedLibrary> library = loader.open(path);
    if (library == nullptr) {
      // Load failures are infrastructure failures, not workload failures (SDK §8).
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
    const ztick_adapter_info &info = descriptor->info;
    if (ztick_sdk_is_compatible(&info.sdk_version) != ztick_status_success) {
      return failure(ztick_status_incompatible, "incompatible adapter SDK version");
    }

    InspectionResult result;
    result.status = ztick_status_success;
    result.reason = nullptr;
    result.metadata.identifier = info.identifier;
    result.metadata.name = info.name;
    copy_version(info.adapter_version, result.metadata.adapter_major, result.metadata.adapter_minor,
                 result.metadata.adapter_patch);
    copy_version(info.sdk_version, result.metadata.sdk_major, result.metadata.sdk_minor,
                 result.metadata.sdk_patch);
    result.metadata.capabilities = info.capabilities;
    if (info.metadata != nullptr) {
      result.metadata.metadata = info.metadata;
    }
    return result;
  } catch (...) {
    return failure(ztick_status_internal_error, "metadata inspection failed");
  }
}

InspectionResult inspect_dynamic_adapter(const char *path) noexcept {
  return inspect_dynamic_adapter(path, platform_library_loader());
}

} // namespace ztick::plugin
