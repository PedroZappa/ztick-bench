// SPDX-License-Identifier: MIT
/**
 * @file main.cpp
 * @brief Entry point of the ztick-sysinfo developer tool.
 *
 * The tool collects the system-inventory through the Linux system-info provider
 * and prints every field with its availability state. It neither executes plugin
 * code nor runs benchmark workloads (BR §21, PLATFORM_MONITORING §2).
 */

#include <ztick/platform/linux_system_info.hpp>
#include <ztick/tools/sysinfo_report.hpp>

#include <iostream>

/**
 * @brief Collect and print the system-inventory.
 * @return Zero when the inventory was collected and printed, one when the
 *         provider itself reported a non-value state.
 */
int main() {
  const auto provider = ztick::platform::linux_sysinfo::make_linux_system_info_provider();
  const auto collected = provider->collect();
  if (!collected.has_value()) {
    std::cerr << "ztick-sysinfo: inventory unavailable: "
              << (collected.reason != nullptr ? collected.reason : "unknown reason") << '\n';
    return 1;
  }
  std::cout << ztick::tools::render_system_info(collected.value);
  return 0;
}
