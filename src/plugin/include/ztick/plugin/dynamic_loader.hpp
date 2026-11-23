// SPDX-License-Identifier: MIT
/**
 * @file dynamic_loader.hpp
 * @brief Host-side dynamic adapter loader with metadata-only inspection.
 *
 * A dynamic adapter exports a read-only @ref ztick_adapter_descriptor data symbol
 * named by @ref ZTICK_ADAPTER_EXPORT_SYMBOL. The host resolves that symbol, copies
 * the identity fields into owning storage and releases the library. No lifecycle
 * callback, entry point or other adapter code is invoked, so discovery alone never
 * executes a plugin (BR §6.1, SDK_SPECIFICATION §10).
 *
 * @note This header is private to ztick-bench and is not installed. Platform code
 *       lives behind @ref ztick::plugin::SharedLibraryLoader so the inspection
 *       rules stay portable and testable (REQ-ARCH-02).
 */

#ifndef ZTICK_PLUGIN_DYNAMIC_LOADER_HPP
#define ZTICK_PLUGIN_DYNAMIC_LOADER_HPP

#include <ztick/sdk.h>

#include <cstdint>
#include <memory>
#include <string>

/**
 * @brief Host-side discovery, trust and execution support.
 * @note Metadata inspection is implemented in P3.2; trust and the in-process runner
 *       are implemented in P3.4/P3.5, while out-of-process IPC remains later Phase 3
 *       work (ARCHITECTURE §5).
 */
namespace ztick::plugin {

/**
 * @brief Owning copy of the identity metadata published by a dynamic adapter.
 * @note Function pointers from @ref ztick_adapter_ops are deliberately excluded:
 *       inspection reports data, never executable capabilities.
 */
struct AdapterMetadata {
  /** @brief Stable machine-readable integration identifier. */
  std::string identifier;
  /** @brief Human-readable integration name. */
  std::string name;
  /** @brief Major version of the integration itself. */
  std::uint32_t adapter_major{};
  /** @brief Minor version of the integration itself. */
  std::uint32_t adapter_minor{};
  /** @brief Patch version of the integration itself. */
  std::uint32_t adapter_patch{};
  /** @brief Major SDK ABI version the adapter was built against. */
  std::uint32_t sdk_major{};
  /** @brief Minor SDK ABI version the adapter was built against. */
  std::uint32_t sdk_minor{};
  /** @brief Patch SDK ABI version the adapter was built against. */
  std::uint32_t sdk_patch{};
  /** @brief Additive bitmask of declared capabilities; unknown bits are unsupported. */
  std::uint64_t capabilities{};
  /** @brief Optional extra metadata, empty when the adapter provides none. */
  std::string metadata;
};

/**
 * @brief Outcome of one metadata-only inspection attempt.
 * @note Failure statuses follow the SDK status model: bad arguments are
 *       `invalid_argument`, SDK mismatch is `incompatible`, and load/ABI failures
 *       are infrastructure failures reported as `internal_error` (SDK §8).
 */
struct InspectionResult {
  /** @brief `ztick_status` code describing the attempt. */
  std::int32_t status{ztick_status_internal_error};
  /** @brief Static, NUL-terminated failure reason, or NULL on success. */
  const char *reason{"not inspected"};
  /** @brief Owning metadata; meaningful only when @ref status is success. */
  AdapterMetadata metadata{};

  /**
   * @brief Report whether this result carries usable metadata.
   * @return True only when @ref status is `ztick_status_success`.
   */
  // Consumed by in-tree tests; cppcheck scans src/ but not tests/.
  // cppcheck-suppress unusedFunction
  [[nodiscard]] bool has_metadata() const noexcept { return status == ztick_status_success; }
};

/**
 * @brief Platform-specific shared-library handle used by the loader.
 * @note Implementations must resolve data only; they never call a resolved symbol.
 */
class SharedLibrary {
public:
  /** @brief Release the platform handle on every exit path. */
  virtual ~SharedLibrary() = default;

  /**
   * @brief Resolve a data symbol in this library without calling it.
   * @param name NUL-terminated symbol name; must not be NULL.
   * @return Address of the data symbol, or NULL when absent or on error.
   */
  virtual const void *resolve(const char *name) noexcept = 0;
};

/**
 * @brief Factory that opens shared libraries for the inspection loader.
 * @note Injected into @ref inspect_dynamic_adapter so failure paths can be tested
 *       deterministically without depending on the host filesystem.
 */
class SharedLibraryLoader {
public:
  /** @brief Allow derived loaders to be destroyed through this interface. */
  virtual ~SharedLibraryLoader() = default;

  /**
   * @brief Open one shared library by path.
   * @param path NUL-terminated library path; must not be NULL.
   * @return Owning handle, or NULL when the platform cannot open the library.
   * @note Opening a module must not resolve or call any adapter entry point.
   */
  virtual std::unique_ptr<SharedLibrary> open(const char *path) const noexcept = 0;
};

/**
 * @brief Access the shared-library loader selected for this platform.
 * @return Reference to a process-wide loader with static storage duration.
 * @note Thread-safe; the returned object is owned by the implementation.
 */
const SharedLibraryLoader &platform_library_loader() noexcept;

/**
 * @brief Inspect a dynamic adapter through an injected platform loader.
 * @param path NUL-terminated path to the shared library; must not be NULL or empty.
 * @param loader Factory used to open the library; must not be a null object.
 * @return Inspection result with an owning metadata copy on success.
 * @note Resolves @ref ZTICK_ADAPTER_EXPORT_SYMBOL as a data symbol and calls no
 *       function in the module. Exceptions from owning storage are contained and
 *       reported as `internal_error`. No descriptor is registered and no trust is
 *       granted by inspection (BR §6.1, §6.2).
 */
InspectionResult inspect_dynamic_adapter(const char *path,
                                         const SharedLibraryLoader &loader) noexcept;

/**
 * @brief Inspect a dynamic adapter using the platform loader.
 * @param path NUL-terminated path to the shared library; must not be NULL or empty.
 * @return Inspection result with an owning metadata copy on success.
 * @note Convenience overload; see the injected-loader form for guarantees.
 */
InspectionResult inspect_dynamic_adapter(const char *path) noexcept;

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_DYNAMIC_LOADER_HPP
