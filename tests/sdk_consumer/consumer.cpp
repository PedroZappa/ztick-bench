// SPDX-License-Identifier: MIT
/**
 * @file consumer.cpp
 * @brief Scratch C++20 consumer proving the installed wrapper header is usable.
 */

#include <ztick/cpp/adapter.hpp>
#include <ztick/sdk.h>

#include <cstdio>

/**
 * @brief Entry point for the scratch C++ consumer.
 * @return Zero when the installed wrapper reports the expected SDK version.
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
  std::printf("C++ consumer: SDK %u.%u.%u\n", ztick::sdk::kSdkVersion.major,
              ztick::sdk::kSdkVersion.minor, ztick::sdk::kSdkVersion.patch);
  return 0;
}
