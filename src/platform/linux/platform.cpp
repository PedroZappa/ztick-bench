// SPDX-License-Identifier: MIT
/**
 * @file platform.cpp
 * @brief Linux platform factory wiring.
 *
 * Selects the concrete Linux providers for the platform abstraction. Static
 * system information is implemented in P1.11; runtime monitoring and controls
 * remain later-phase work.
 */

#include <ztick/platform/linux_system_info.hpp>

#include <memory>
#include <utility>

namespace ztick::platform::linux_sysinfo {

std::unique_ptr<SystemInfoProvider> make_linux_system_info_provider() {
  auto source = std::make_unique<FilesystemSystemInfoSource>();
  return std::make_unique<LinuxSystemInfoProvider>(std::move(source));
}

} // namespace ztick::platform::linux_sysinfo
