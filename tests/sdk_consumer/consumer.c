// SPDX-License-Identifier: MIT
/**
 * @file consumer.c
 * @brief Scratch C11 consumer proving the installed SDK headers and runtime link.
 *
 * Calls every exported runtime symbol through the installed package so a missing
 * definition cannot be hidden by header-only compilation.
 */

#include <ztick/sdk.h>
#include <ztick/types.h>
#include <ztick/version.h>

#include <stdio.h>
#include <string.h>

/**
 * @brief Entry point for the scratch C consumer.
 * @return Zero when the installed SDK reports the expected ABI and links every symbol.
 */
int main(void) {
  if (ZTICK_SDK_VERSION_MAJOR != 1 || ZTICK_SDK_VERSION_MINOR != 0 ||
      ZTICK_SDK_VERSION_PATCH != 0) {
    return 1;
  }
  if (ztick_status_success != 0) {
    return 2;
  }

  ztick_version version = {0};
  version.size = sizeof(version);
  version.version = ZTICK_STRUCT_LAYOUT_VERSION;
  if (ztick_sdk_get_version(&version) != ztick_status_success) {
    return 3;
  }
  if (version.major != ZTICK_SDK_VERSION_MAJOR || version.minor != ZTICK_SDK_VERSION_MINOR ||
      version.patch != ZTICK_SDK_VERSION_PATCH) {
    return 4;
  }
  if (ztick_sdk_is_compatible(&version) != ztick_status_success) {
    return 5;
  }

  static ztick_adapter_descriptor descriptor = {0};
  descriptor.size = sizeof(descriptor);
  descriptor.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.size = sizeof(descriptor.info);
  descriptor.info.version = ZTICK_STRUCT_LAYOUT_VERSION;
  descriptor.info.identifier = "external.c.consumer";
  descriptor.info.name = "External C consumer";
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
  if (strcmp(ztick_status_message(ztick_status_success), "success") != 0) {
    return 7;
  }

  printf("C consumer: SDK %d.%d.%d\n", ZTICK_SDK_VERSION_MAJOR, ZTICK_SDK_VERSION_MINOR,
         ZTICK_SDK_VERSION_PATCH);
  return 0;
}
