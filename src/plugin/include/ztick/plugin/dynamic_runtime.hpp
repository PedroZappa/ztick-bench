// SPDX-License-Identifier: MIT
/**
 * @file dynamic_runtime.hpp
 * @brief Host-side runtime loading of a dynamic SDK adapter descriptor.
 *
 * Metadata-only inspection closes the module after copying identity data, so it
 * cannot execute a lifecycle. This loader keeps the platform library open while a
 * caller uses the read-only descriptor and its function table. It applies the same
 * structural and SDK-version validation as inspection, but it does not register,
 * trust, configure or execute the adapter (SDK_SPECIFICATION §10).
 *
 * @note This header is private to ztick-bench and is not installed. The dynamic
 *       App B harness uses it for in-process lifecycle execution and child mode.
 */

#ifndef ZTICK_PLUGIN_DYNAMIC_RUNTIME_HPP
#define ZTICK_PLUGIN_DYNAMIC_RUNTIME_HPP

#include <ztick/plugin/dynamic_loader.hpp>

#include <ztick/sdk.h>

#include <cstdint>
#include <memory>

/** @brief Host-side discovery, trust and execution support. */
namespace ztick::plugin {

/**
 * @brief Owning handle for a loaded dynamic adapter and its descriptor.
 * @note The descriptor pointer is valid only while this object owns the library.
 *       Instances created through its ops must be destroyed before the handle is
 *       released. The type is move-only so a library cannot be closed twice.
 */
class LoadedAdapter final {
public:
  /** @brief Construct an empty handle with no library or descriptor. */
  LoadedAdapter() noexcept = default;

  /**
   * @brief Take ownership of an open library and its validated descriptor.
   * @param library Owning platform library handle; may be NULL.
   * @param descriptor Descriptor resolved from @p library; may be NULL.
   * @note The caller must ensure both arguments refer to the same loaded module.
   */
  LoadedAdapter(std::unique_ptr<SharedLibrary> library,
                const ztick_adapter_descriptor *descriptor) noexcept;

  /** @brief Release the library when the handle goes out of scope. */
  ~LoadedAdapter() = default;

  /** @brief Move ownership from another handle. */
  LoadedAdapter(LoadedAdapter &&) noexcept = default;

  /**
   * @brief Move assignment replaces any currently owned library.
   * @return Reference to this handle.
   */
  auto operator=(LoadedAdapter &&) noexcept -> LoadedAdapter & = default;

  /** @brief Copying an owning library handle is disabled. */
  LoadedAdapter(const LoadedAdapter &) = delete;

  /** @brief Copy assignment of an owning library handle is disabled. */
  auto operator=(const LoadedAdapter &) -> LoadedAdapter & = delete;

  /**
   * @brief Report whether this handle owns a usable descriptor.
   * @return True only when both the library and descriptor are present.
   */
  [[nodiscard]] bool valid() const noexcept;

  /**
   * @brief Access the loaded descriptor.
   * @return Descriptor pointer, or NULL when @ref valid is false.
   * @note The descriptor and all callback pointers remain readable until this
   *       handle is moved from or destroyed.
   */
  [[nodiscard]] const ztick_adapter_descriptor *get() const noexcept;

private:
  /** @brief Platform library handle that keeps descriptor code loaded. */
  std::unique_ptr<SharedLibrary> library_;
  /** @brief Read-only descriptor resolved from @ref library_. */
  const ztick_adapter_descriptor *descriptor_{nullptr};
};

/**
 * @brief Outcome of one dynamic adapter runtime-load attempt.
 * @note Failure statuses follow the SDK status model: bad arguments are
 *       `invalid_argument`, SDK mismatch is `incompatible`, and load or symbol
 *       failures are infrastructure failures reported as `internal_error`.
 */
struct RuntimeLoadResult {
  /** @brief `ztick_status` code describing the attempt. */
  std::int32_t status{ztick_status_internal_error};
  /** @brief Static, NUL-terminated failure reason, or NULL on success. */
  const char *reason{"not loaded"};
  /** @brief Owning runtime handle; meaningful only when @ref status is success. */
  LoadedAdapter adapter{};

  /**
   * @brief Report whether the adapter can be used through this result.
   * @return True only when @ref status is `ztick_status_success`.
   */
  [[nodiscard]] bool loaded() const noexcept;
};

/**
 * @brief Load a dynamic adapter for lifecycle use through an injected loader.
 * @param path NUL-terminated path to the shared library; must not be NULL or empty.
 * @param loader Factory used to open the library; must not be a null object.
 * @return Result retaining the open module on success and an explicit reason otherwise.
 * @note Resolves @ref ZTICK_ADAPTER_EXPORT_SYMBOL as read-only data, validates its
 *       layout with the shared SDK check, and applies the SDK version rules. No
 *       callback is invoked, no instance is created, and no trust is granted.
 */
RuntimeLoadResult load_dynamic_adapter_runtime(const char *path,
                                               const SharedLibraryLoader &loader) noexcept;

/**
 * @brief Load a dynamic adapter for lifecycle use with the platform loader.
 * @param path NUL-terminated path to the shared library; must not be NULL or empty.
 * @return Result retaining the open module on success and an explicit reason otherwise.
 * @note Convenience overload; see the injected-loader form for guarantees.
 */
RuntimeLoadResult load_dynamic_adapter_runtime(const char *path) noexcept;

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_DYNAMIC_RUNTIME_HPP
