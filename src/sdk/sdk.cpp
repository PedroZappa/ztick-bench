// SPDX-License-Identifier: MIT
/**
 * @file sdk.cpp
 * @brief Compiled implementation of the host-side C ABI runtime symbols.
 *
 * Defines the four functions declared by <ztick/sdk.h> and the private registry
 * observers declared by <ztick/sdk_registry.hpp>. Follows the versioning rules in
 * SDK_SPECIFICATION §2 and contains every exception so none crosses the C boundary
 * (SDK_SPECIFICATION §8).
 */

#include <ztick/sdk.h>
#include <ztick/sdk_registry.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

/**
 * @brief Validate the size and layout version fields of a public struct.
 * @param size Declared size reported by the caller.
 * @param version Declared layout version reported by the caller.
 * @param expected Minimum size of the struct in this ABI.
 * @return True when the struct can be read safely with this ABI.
 */
bool has_valid_layout(uint32_t size, uint32_t version, size_t expected) noexcept {
  return size >= expected && version == ZTICK_STRUCT_LAYOUT_VERSION;
}

/**
 * @brief Process-wide storage for descriptors registered by static integrations.
 */
struct AdapterRegistry {
  /** @brief Guards every access to @ref entries. */
  std::mutex mutex;
  /** @brief Non-owning descriptor pointers in registration order. */
  std::vector<const ztick_adapter_descriptor *> entries;
};

/**
 * @brief Access the registry without relying on static initialization order.
 * @return Reference to the lazily constructed, process-wide registry.
 */
AdapterRegistry &registry() noexcept {
  static AdapterRegistry instance;
  return instance;
}

} // namespace

extern "C" {

ZTICK_API int32_t ztick_sdk_get_version(ztick_version *out_version) {
  if (out_version == nullptr ||
      !has_valid_layout(out_version->size, out_version->version, sizeof(ztick_version))) {
    return ztick_status_invalid_argument;
  }
  out_version->major = ZTICK_SDK_VERSION_MAJOR;
  out_version->minor = ZTICK_SDK_VERSION_MINOR;
  out_version->patch = ZTICK_SDK_VERSION_PATCH;
  return ztick_status_success;
}

ZTICK_API int32_t ztick_sdk_is_compatible(const ztick_version *adapter_sdk_version) {
  if (adapter_sdk_version == nullptr ||
      !has_valid_layout(adapter_sdk_version->size, adapter_sdk_version->version,
                        sizeof(ztick_version))) {
    return ztick_status_invalid_argument;
  }
  if (adapter_sdk_version->major != ZTICK_SDK_VERSION_MAJOR) {
    return ztick_status_incompatible;
  }
  if (adapter_sdk_version->minor > ZTICK_SDK_VERSION_MINOR) {
    return ztick_status_incompatible;
  }
  return ztick_status_success;
}

ZTICK_API int32_t ztick_register_adapter(const ztick_adapter_descriptor *descriptor) {
  if (descriptor == nullptr ||
      !has_valid_layout(descriptor->size, descriptor->version, sizeof(ztick_adapter_descriptor))) {
    return ztick_status_invalid_argument;
  }
  const ztick_adapter_info &info = descriptor->info;
  const ztick_adapter_ops &ops = descriptor->ops;
  if (!has_valid_layout(info.size, info.version, sizeof(ztick_adapter_info)) ||
      !has_valid_layout(ops.size, ops.version, sizeof(ztick_adapter_ops)) ||
      !has_valid_layout(info.adapter_version.size, info.adapter_version.version,
                        sizeof(ztick_version))) {
    return ztick_status_invalid_argument;
  }
  if (info.identifier == nullptr || info.identifier[0] == '\0' || info.name == nullptr) {
    return ztick_status_invalid_argument;
  }
  const int32_t compatibility = ztick_sdk_is_compatible(&info.sdk_version);
  if (compatibility != ztick_status_success) {
    return compatibility;
  }
  try {
    AdapterRegistry &reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const bool duplicate = std::any_of(
        reg.entries.begin(), reg.entries.end(), [&info](const ztick_adapter_descriptor *entry) {
          return std::strcmp(entry->info.identifier, info.identifier) == 0;
        });
    if (duplicate) {
      return ztick_status_invalid_argument;
    }
    reg.entries.push_back(descriptor);
    return ztick_status_success;
  } catch (...) {
    return ztick_status_internal_error;
  }
}

ZTICK_API const char *ztick_status_message(int32_t status) {
  switch (status) {
  case ztick_status_success:
    return "success";
  case ztick_status_unsupported:
    return "unsupported";
  case ztick_status_incompatible:
    return "incompatible";
  case ztick_status_invalid_argument:
    return "invalid argument";
  case ztick_status_processing_failure:
    return "processing failure";
  case ztick_status_validation_failure:
    return "validation failure";
  case ztick_status_internal_error:
    return "internal error";
  default:
    return "unknown status";
  }
}

} // extern "C"

namespace ztick::sdk::detail {

// Consumed by in-tree tests and Phase 3 host code; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
std::size_t registered_adapter_count() noexcept {
  try {
    AdapterRegistry &reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.entries.size();
  } catch (...) {
    return 0;
  }
}

// Consumed by in-tree tests and Phase 3 host code; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
const ztick_adapter_descriptor *find_registered_adapter(const char *identifier) noexcept {
  if (identifier == nullptr) {
    return nullptr;
  }
  try {
    AdapterRegistry &reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto match = std::find_if(reg.entries.begin(), reg.entries.end(),
                                    [identifier](const ztick_adapter_descriptor *entry) {
                                      return std::strcmp(entry->info.identifier, identifier) == 0;
                                    });
    if (match != reg.entries.end()) {
      return *match;
    }
  } catch (...) {
    return nullptr;
  }
  return nullptr;
}

} // namespace ztick::sdk::detail
