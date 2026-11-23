// SPDX-License-Identifier: MIT
/**
 * @file dynamic_library.cpp
 * @brief POSIX implementation of the plugin host's shared-library abstraction.
 *
 * Uses `dlopen`/`dlsym`/`dlclose` behind @ref ztick::plugin::SharedLibraryLoader so
 * no platform loader call appears in portable inspection code (REQ-ARCH-02). Only
 * data symbols are resolved; this file never calls a function from a loaded module.
 */

#include <ztick/plugin/dynamic_loader.hpp>

#include <dlfcn.h>

#include <memory>
#include <utility>

/**
 * @brief Private POSIX implementation details of the dynamic loader.
 */
namespace ztick::plugin::posix {

/**
 * @brief RAII handle for one `dlopen` result.
 */
class PosixSharedLibrary final : public SharedLibrary {
public:
  /**
   * @brief Take ownership of an open library handle.
   * @param handle Handle returned by `dlopen`; must not be NULL.
   */
  explicit PosixSharedLibrary(void *handle) noexcept : handle_(handle) {}

  /** @brief Release the handle with `dlclose`. */
  ~PosixSharedLibrary() override {
    if (handle_ != nullptr) {
      dlclose(handle_);
    }
  }

  /** @brief Copies would duplicate ownership of one `dlopen` handle. */
  PosixSharedLibrary(const PosixSharedLibrary &) = delete;
  /** @brief Copies would duplicate ownership of one `dlopen` handle. */
  PosixSharedLibrary &operator=(const PosixSharedLibrary &) = delete;

  /**
   * @brief Resolve a data symbol without calling it.
   * @param name NUL-terminated symbol name; must not be NULL.
   * @return Symbol address, or NULL when it is absent or `dlsym` reports an error.
   */
  const void *resolve(const char *name) noexcept override {
    if (name == nullptr || handle_ == nullptr) {
      return nullptr;
    }
    dlerror();
    void *symbol = dlsym(handle_, name);
    return dlerror() == nullptr ? symbol : nullptr;
  }

private:
  /** @brief Owned native library handle. */
  void *handle_;
};

/**
 * @brief POSIX factory for shared-library handles.
 */
class PosixSharedLibraryLoader final : public SharedLibraryLoader {
public:
  /**
   * @brief Open a shared library with local, immediate symbol resolution.
   * @param path NUL-terminated library path; must not be NULL.
   * @return Owning handle, or NULL when `dlopen` fails.
   * @note `RTLD_LOCAL` keeps module symbols out of the global namespace; loading
   *       does not resolve or call any adapter entry point.
   */
  std::unique_ptr<SharedLibrary> open(const char *path) const noexcept override {
    if (path == nullptr) {
      return nullptr;
    }
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
      return nullptr;
    }
    return std::make_unique<PosixSharedLibrary>(handle);
  }
};

} // namespace ztick::plugin::posix

namespace ztick::plugin {

const SharedLibraryLoader &platform_library_loader() noexcept {
  // Function-local static avoids order-of-initialization dependencies (SDK §2).
  static const posix::PosixSharedLibraryLoader loader;
  return loader;
}

} // namespace ztick::plugin
