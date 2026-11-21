// SPDX-License-Identifier: MIT
/**
 * @file consumer.c
 * @brief Scratch C11 consumer proving the installed C headers are usable.
 */

#include <ztick/sdk.h>
#include <ztick/types.h>
#include <ztick/version.h>

#include <stdio.h>

/**
 * @brief Entry point for the scratch C consumer.
 * @return Zero when the installed SDK headers report the expected ABI version.
 */
int main(void) {
  if (ZTICK_SDK_VERSION_MAJOR != 1 || ZTICK_SDK_VERSION_MINOR != 0 ||
      ZTICK_SDK_VERSION_PATCH != 0) {
    return 1;
  }
  if (ztick_status_success != 0) {
    return 2;
  }
  printf("C consumer: SDK %d.%d.%d\n", ZTICK_SDK_VERSION_MAJOR, ZTICK_SDK_VERSION_MINOR,
         ZTICK_SDK_VERSION_PATCH);
  return 0;
}
