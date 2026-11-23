// SPDX-License-Identifier: MIT
/**
 * @file dynamic_library.cpp
 * @brief Non-POSIX fallback for the plugin host's shared-library abstraction.
 *
 * Dynamic loading is platform work deferred beyond v1.0 outside POSIX systems
 * (BR §1, §16). The fallback reports failure through
 * @ref ztick::plugin::InspectionResult rather than omitting the capability; static
 * adapters remain available everywhere.
 */

#include <ztick/plugin/dynamic_loader.hpp>

#include <memory>

/**
 * @brief Private fallback implementation details of the dynamic loader.
 */
namespace ztick::plugin::unsupported {

/**
 * @brief Loader that cannot open modules on this platform.
 */
class UnsupportedSharedLibraryLoader final : public SharedLibraryLoader {
public:
  /**
   * @brief Refuse to open a shared library.
   * @param path NUL-terminated library path; unused.
   * @return Always NULL; callers report an infrastructure failure with a reason.
   */
  std::unique_ptr<SharedLibrary> open(const char *path) const noexcept override {
    (void)path;
    return nullptr;
  }
};

} // namespace ztick::plugin::unsupported

namespace ztick::plugin {

const SharedLibraryLoader &platform_library_loader() noexcept {
  static const unsupported::UnsupportedSharedLibraryLoader loader;
  return loader;
}

} // namespace ztick::plugin
