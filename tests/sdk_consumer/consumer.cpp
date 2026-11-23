// SPDX-License-Identifier: MIT
/**
 * @file consumer.cpp
 * @brief Scratch C++20 consumer proving the installed wrapper and C ABI runtime link.
 */

#include <ztick/cpp/adapter.hpp>
#include <ztick/sdk.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

/**
 * @brief Entry point for the scratch C++ consumer.
 * @return Zero when the installed wrapper and every runtime symbol behave as expected.
 */
int main() {
  if (ztick::sdk::kSdkVersion.major != ZTICK_SDK_VERSION_MAJOR ||
      ztick::sdk::kSdkVersion.minor != ZTICK_SDK_VERSION_MINOR ||
      ztick::sdk::kSdkVersion.patch != ZTICK_SDK_VERSION_PATCH) {
    return 1;
  }
  ztick::sdk::AdapterHandle handle;
  if (handle) {
    return 2;
  }

  ztick_version version{};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  if (ztick_sdk_get_version(&version) != ztick::sdk::to_c(ztick::sdk::Status::success)) {
    return 3;
  }
  if (version.major != ZTICK_SDK_VERSION_MAJOR || version.minor != ZTICK_SDK_VERSION_MINOR ||
      version.patch != ZTICK_SDK_VERSION_PATCH) {
    return 4;
  }
  if (ztick_sdk_is_compatible(&version) != ztick_status_success) {
    return 5;
  }

  static ztick_adapter_descriptor descriptor{};
  descriptor.size = sizeof(descriptor);
  descriptor.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.size = sizeof(descriptor.info);
  descriptor.info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.identifier = "external.cpp.consumer";
  descriptor.info.name = "External C++ consumer";
  descriptor.info.adapter_version.size = sizeof(descriptor.info.adapter_version);
  descriptor.info.adapter_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.adapter_version.major = 0U;
  descriptor.info.adapter_version.minor = 1U;
  descriptor.info.sdk_version = version;
  descriptor.ops.size = sizeof(descriptor.ops);
  descriptor.ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
  if (ztick_register_adapter(&descriptor) != ztick_status_success) {
    return 6;
  }
  if (std::strcmp(ztick_status_message(ztick_status_invalid_argument), "invalid argument") != 0) {
    return 7;
  }

  std::printf("C++ consumer: SDK %u.%u.%u\n", ztick::sdk::kSdkVersion.major,
              ztick::sdk::kSdkVersion.minor, ztick::sdk::kSdkVersion.patch);
  return 0;
}
