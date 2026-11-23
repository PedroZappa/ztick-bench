// SPDX-License-Identifier: MIT
/**
 * @file sdk_runtime_c11_test.c
 * @brief Standalone C11 link-and-behaviour check for the compiled SDK runtime.
 *
 * Unlike the header-only compile check, this translation unit links the SDK and
 * calls every exported runtime symbol. It proves the C ABI is usable from C11
 * without C++ wrappers and that malformed input is refused deterministically.
 */

#include <ztick/sdk.h>

#include <string.h>

/**
 * @brief Entry point for the C11 SDK runtime check.
 * @return Zero when every call returns its expected status, otherwise a call index.
 */
int main(void) {
  ztick_version version = {0};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  if (ztick_sdk_get_version(&version) != ztick_status_success) {
    return 1;
  }
  if (version.major != ZTICK_SDK_VERSION_MAJOR || version.minor != ZTICK_SDK_VERSION_MINOR ||
      version.patch != ZTICK_SDK_VERSION_PATCH) {
    return 2;
  }
  if (ztick_sdk_is_compatible(&version) != ztick_status_success) {
    return 3;
  }

  ztick_version future = version;
  future.minor = ZTICK_SDK_VERSION_MINOR + 1U;
  if (ztick_sdk_is_compatible(&future) != ztick_status_incompatible) {
    return 4;
  }
  if (ztick_sdk_get_version(NULL) != ztick_status_invalid_argument) {
    return 5;
  }

  if (strcmp(ztick_status_message(ztick_status_success), "success") != 0) {
    return 6;
  }
  if (strcmp(ztick_status_message(-12345), "unknown status") != 0) {
    return 7;
  }

  static ztick_adapter_descriptor descriptor = {0};
  descriptor.size = sizeof(descriptor);
  descriptor.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.size = sizeof(descriptor.info);
  descriptor.info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.identifier = "c11.runtime.link.scaffold";
  descriptor.info.name = "C11 runtime link scaffold";
  descriptor.info.adapter_version.size = sizeof(descriptor.info.adapter_version);
  descriptor.info.adapter_version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.adapter_version.major = 0U;
  descriptor.info.adapter_version.minor = 1U;
  descriptor.info.sdk_version = version;
  descriptor.ops.size = sizeof(descriptor.ops);
  descriptor.ops.version = ZTICK_STRUCT_LAYOUT_VERSION;
  if (ztick_register_adapter(&descriptor) != ztick_status_success) {
    return 8;
  }
  if (ztick_register_adapter(&descriptor) != ztick_status_invalid_argument) {
    return 9;
  }
  return 0;
}
