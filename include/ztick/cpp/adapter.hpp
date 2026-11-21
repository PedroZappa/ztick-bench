// SPDX-License-Identifier: MIT
/**
 * @file adapter.hpp
 * @brief Header-only C++20 convenience wrappers around the ztick SDK C ABI.
 *
 * This is the C++ surface named by SDK_SPECIFICATION §1.1: it includes the
 * frozen C headers first and declares only C++ types in namespace `ztick::sdk`.
 * The wrappers are not part of the stable ABI (SDK_SPECIFICATION §9); they add
 * type safety and RAII on top of the C boundary without changing it. Adapter
 * exceptions must be translated to status codes before crossing that boundary.
 *
 * @note Including this header never loads, registers or executes an adapter.
 */

#ifndef ZTICK_CPP_ADAPTER_HPP
#define ZTICK_CPP_ADAPTER_HPP

#include <ztick/sdk.h>
#include <ztick/types.h>
#include <ztick/version.h>

#include <cstdint>
#include <utility>

/**
 * @brief C++ convenience wrappers for the ztick SDK.
 * @note All names in this namespace are header-only and may evolve independently
 *       of the versioned C ABI in the global namespace.
 */
namespace ztick::sdk {

/**
 * @brief Typed view of a C ABI status code.
 * @note Numeric values match @ref ztick_status exactly. An unknown code from a
 *       newer adapter is preserved by @ref status_from_c and must be treated as
 *       unsupported by callers.
 */
enum class Status : int32_t {
  /** Operation completed successfully. */
  success = ztick_status_success,
  /** Requested format, capability or value is not supported. */
  unsupported = ztick_status_unsupported,
  /** SDK major/minor compatibility check failed. */
  incompatible = ztick_status_incompatible,
  /** One or more arguments are invalid. */
  invalid_argument = ztick_status_invalid_argument,
  /** DSP processing failed; a workload failure. */
  processing_failure = ztick_status_processing_failure,
  /** A validation check failed. */
  validation_failure = ztick_status_validation_failure,
  /** Internal error; an infrastructure failure. */
  internal_error = ztick_status_internal_error,
};

/**
 * @brief Convert a wrapper status to its C ABI integer code.
 * @param status Status value to convert.
 * @return The matching `ztick_status` integer code.
 */
constexpr int32_t to_c(Status status) noexcept { return static_cast<int32_t>(status); }

/**
 * @brief Convert a C ABI integer code to a wrapper status.
 * @param code Status code returned by a C ABI function.
 * @return The matching status; unknown values remain representable.
 */
constexpr Status status_from_c(int32_t code) noexcept { return static_cast<Status>(code); }

/**
 * @brief Report whether a status represents success.
 * @param status Status value to test.
 * @return True only for @ref Status::success.
 */
constexpr bool is_success(Status status) noexcept { return status == Status::success; }

/**
 * @brief C++ semantic version triple for adapters and the SDK.
 * @note Decomposes @ref ztick_version without relying on its `size` and
 *       `version` struct-evolution fields.
 */
struct Version {
  /** Major component; must match exactly for compatibility. */
  uint32_t major;
  /** Minor component; a newer host is backward-compatible. */
  uint32_t minor;
  /** Patch component; differences are compatible. */
  uint32_t patch;

  /**
   * @brief Construct a version from explicit components.
   * @param major_value Major component.
   * @param minor_value Minor component.
   * @param patch_value Patch component.
   */
  constexpr Version(uint32_t major_value, uint32_t minor_value, uint32_t patch_value) noexcept
      : major(major_value), minor(minor_value), patch(patch_value) {}

  /**
   * @brief Construct a version from a C ABI version struct.
   * @param value Version struct to read; its `size` and `version` fields are ignored.
   */
  constexpr explicit Version(const ztick_version &value) noexcept
      : Version(value.major, value.minor, value.patch) {}
};

/** @brief SDK ABI version this wrapper was compiled against. */
inline constexpr Version kSdkVersion{ZTICK_SDK_VERSION_MAJOR, ZTICK_SDK_VERSION_MINOR,
                                     ZTICK_SDK_VERSION_PATCH};

/**
 * @brief RAII owner for one opaque adapter instance.
 * @note Owns the handle returned by the C ABI `create` operation and calls the
 *       supplied `destroy` operation at most once on every exit path. The
 *       destroy callback must not throw; adapter exceptions are translated to
 *       status codes before the C boundary (SDK_SPECIFICATION §9). Instances
 *       do not share mutable state.
 */
class AdapterHandle {
public:
  /** @brief Construct an empty handle that owns no adapter instance. */
  AdapterHandle() noexcept = default;

  /**
   * @brief Take ownership of an existing C ABI adapter handle.
   * @param handle Handle to own, or NULL for an empty handle.
   * @param ops Lifecycle table containing the `destroy` operation to call.
   * @warning @p handle must have been created by the same adapter that supplied
   *          @p ops, and must not be released elsewhere while owned here.
   */
  AdapterHandle(ztick_adapter *handle, const ztick_adapter_ops &ops) noexcept
      : handle_(handle), ops_(ops) {}

  /** @brief Destroy the owned instance, if any. */
  ~AdapterHandle() { reset(); }

  /** @brief Copy construction is disabled; each C handle has exactly one owner. */
  AdapterHandle(const AdapterHandle &) = delete;

  /** @brief Copy assignment is disabled; each C handle has exactly one owner. */
  AdapterHandle &operator=(const AdapterHandle &) = delete;

  /**
   * @brief Move-construct a handle and transfer ownership.
   * @param other Handle to empty; it owns nothing after the move.
   */
  AdapterHandle(AdapterHandle &&other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)), ops_(other.ops_) {}

  /**
   * @brief Move-assign a handle, destroying any instance already owned.
   * @param other Handle to empty; it owns nothing after the move.
   * @return A reference to this handle.
   */
  AdapterHandle &operator=(AdapterHandle &&other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, nullptr);
      ops_ = other.ops_;
    }
    return *this;
  }

  /**
   * @brief Destroy the owned instance, if any, and leave this handle empty.
   * @note The destroy status is ignored because destruction cannot report an
   *       error to a destructor; calling this twice is safe.
   */
  void reset() noexcept {
    if (handle_ != nullptr) {
      if (ops_.destroy != nullptr) {
        static_cast<void>(ops_.destroy(handle_));
      }
      handle_ = nullptr;
    }
  }

  /**
   * @brief Relinquish ownership without destroying the instance.
   * @return The previously owned handle, or NULL when empty.
   * @note The caller becomes responsible for destroying the returned handle.
   */
  [[nodiscard]] ztick_adapter *release() noexcept { return std::exchange(handle_, nullptr); }

  /**
   * @brief Access the owned C handle.
   * @return The handle, or NULL when empty; ownership remains with this object.
   */
  [[nodiscard]] ztick_adapter *get() const noexcept { return handle_; }

  /**
   * @brief Report whether this handle owns an adapter instance.
   * @return True when @ref get would return a non-NULL handle.
   */
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

private:
  /** Owned opaque adapter handle, or nullptr when empty. */
  ztick_adapter *handle_ = nullptr;
  /** Copy of the lifecycle operations supplying the destroy callback. */
  ztick_adapter_ops ops_{};
};

} // namespace ztick::sdk

#endif // ZTICK_CPP_ADAPTER_HPP
