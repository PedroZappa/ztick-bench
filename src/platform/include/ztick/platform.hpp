// SPDX-License-Identifier: MIT
/**
 * @file platform.hpp
 * @brief Portable platform abstraction boundary used by the core and tools.
 *
 * This interface isolates OS-specific system information, runtime monitoring,
 * CPU topology/affinity, worker-thread control and scheduling priority from the
 * portable core (ARCHITECTURE §7). Every operation returns a
 * @ref ztick::platform::Result carrying
 * an explicit `ztick_availability` state, so platform-dependent data is never
 * silently omitted (BR §17). State meanings and metric mapping are owned by
 * PLATFORM_MONITORING §6.
 *
 * @note Implementations encode expected absence or failure in the returned
 *       state rather than throwing; exceptions remain reserved for programming
 *       errors. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_PLATFORM_HPP
#define ZTICK_PLATFORM_HPP

#include <ztick/types.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Portable platform abstraction types and interfaces.
 * @note OS-specific code belongs in platform implementations, never in the core
 *       or front ends (BR §15, REQ-ARCH-02).
 */
namespace ztick::platform {

/** @brief Availability state carried by every platform operation result. */
using Availability = ztick_availability;

/**
 * @brief Capability-probed result of a platform operation.
 * @tparam T Value type returned when @ref state is `ztick_availability_value`.
 * @note `T` must be default-constructible. @ref reason must refer to storage that
 *       outlives the result; use string literals for implementation diagnostics.
 */
template <typename T> struct Result {
  /** Availability state; defaults to unavailable so fields are never omitted. */
  Availability state = ztick_availability_unavailable;
  /** Value when @ref state is `ztick_availability_value`; otherwise a default value. */
  T value{};
  /** Human-readable reason for a non-value state, or NULL when not needed. */
  const char *reason = "not collected";

  /**
   * @brief Report whether a supported value was successfully read.
   * @return True only when @ref state is `ztick_availability_value`.
   */
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return state == ztick_availability_value;
  }
};

/**
 * @brief Build a successful platform result.
 * @tparam T Value type to carry.
 * @param result_value Value that was successfully read.
 * @return A result with `ztick_availability_value` state and no reason.
 */
template <typename T> inline Result<T> make_value(T result_value) {
  return Result<T>{ztick_availability_value, std::move(result_value), nullptr};
}

/**
 * @brief Build a result for a supported capability that could not be read.
 * @tparam T Value type to carry.
 * @param reason Human-readable explanation; must outlive the result.
 * @return A result with `ztick_availability_unavailable` state.
 */
template <typename T> inline Result<T> make_unavailable(const char *reason) {
  return Result<T>{ztick_availability_unavailable, T{}, reason};
}

/**
 * @brief Build a result for a capability never implemented here.
 * @tparam T Value type to carry.
 * @param reason Human-readable explanation; must outlive the result.
 * @return A result with `ztick_availability_unsupported` state.
 */
template <typename T> inline Result<T> make_unsupported(const char *reason) {
  return Result<T>{ztick_availability_unsupported, T{}, reason};
}

/**
 * @brief Build a result for a capability meaningless in the selected mode.
 * @tparam T Value type to carry.
 * @param reason Human-readable explanation; must outlive the result.
 * @return A result with `ztick_availability_not_applicable` state.
 */
template <typename T> inline Result<T> make_not_applicable(const char *reason) {
  return Result<T>{ztick_availability_not_applicable, T{}, reason};
}

/**
 * @brief Explicit set of CPU identifiers used by affinity operations.
 * @note An empty set means "use the OS default" and never implies "no CPUs".
 */
struct CpuSet {
  /** Zero-based CPU identifiers; order is not significant. */
  std::vector<std::uint32_t> ids;
};

/**
 * @brief One cache description from the system information inventory.
 * @note Size is in bytes; an unknown type is represented by an empty string.
 */
struct CacheInfo {
  /** Cache level, starting at one for L1. */
  std::uint32_t level{};
  /** Cache size in bytes. */
  std::uint64_t size_bytes{};
  /** Cache type such as "data", "instruction" or "unified". */
  std::string type;
};

/**
 * @brief Static system information collected before a run.
 * @note Each field carries its own availability state; no field is omitted when
 *       the platform cannot provide it (PLATFORM_MONITORING §2).
 */
struct SystemInfo {
  /** CPU model name. */
  Result<std::string> cpu_model;
  /** CPU architecture name. */
  Result<std::string> cpu_architecture;
  /** Physical core count. */
  Result<std::uint32_t> physical_core_count;
  /** Logical core count. */
  Result<std::uint32_t> logical_core_count;
  /** Current CPU frequency in Hz. */
  Result<double> cpu_frequency_hz;
  /** Frequency scaling governor or equivalent policy name. */
  Result<std::string> scaling_governor;
  /** Cache topology entries. */
  Result<std::vector<CacheInfo>> caches;
  /** Declared SIMD feature names. */
  Result<std::vector<std::string>> simd_features;
  /** Installed RAM in bytes. */
  Result<std::uint64_t> total_ram_bytes;
  /** Currently available RAM in bytes. */
  Result<std::uint64_t> available_ram_bytes;
  /** Operating system name. */
  Result<std::string> os_name;
  /** Kernel version string. */
  Result<std::string> kernel_version;
  /** Audio backend name; not applicable in pure offline mode. */
  Result<std::string> audio_backend;
  /** Audio device name; not applicable in pure offline mode. */
  Result<std::string> audio_device;
  /** Process affinity at collection time. */
  Result<CpuSet> process_affinity;
  /** Scheduler and other environment metadata. */
  Result<std::string> scheduling_configuration;
  /** Additional platform-specific environment metadata. */
  Result<std::vector<std::string>> environment_metadata;
};

/**
 * @brief Point at which a runtime monitoring signal is sampled.
 * @note The values are explicit stable identifiers; they are not array indices.
 */
enum class SamplingPhase : std::uint32_t {
  /** Sample before the measured workload. */
  before = 0,
  /** Sample while the measured workload runs. */
  during = 1,
  /** Sample after the measured workload. */
  after = 2,
};

/**
 * @brief Runtime signals collected during a run.
 * @note Every signal carries an availability state and may be not applicable for
 *       the selected benchmark mode (PLATFORM_MONITORING §3).
 */
struct RuntimeSnapshot {
  /** Current CPU frequency in Hz. */
  Result<double> cpu_frequency_hz;
  /** CPU package temperature in degrees Celsius. */
  Result<double> cpu_temperature_celsius;
  /** Thermal throttling indicator. */
  Result<bool> thermal_throttling;
  /** Frequency scaling or power-management state. */
  Result<std::string> power_state;
  /** Background CPU load as a fraction from zero to one. */
  Result<double> background_cpu_load;
};

/**
 * @brief Provides the static system information inventory.
 * @note Implementations must not execute plugin code or benchmark workloads.
 */
class SystemInfoProvider {
public:
  /** @brief Allow derived implementations to be destroyed through this interface. */
  virtual ~SystemInfoProvider() = default;

  /**
   * @brief Collect the complete system information inventory.
   * @return One result per inventory field, each with an explicit state.
   */
  virtual Result<SystemInfo> collect() const = 0;
};

/**
 * @brief Samples runtime monitoring signals around and during a run.
 * @note Missing signals are reported as unavailable or not applicable, never
 *       silently dropped (REQ-PLAT-03).
 */
class RuntimeMonitor {
public:
  /** @brief Allow derived implementations to be destroyed through this interface. */
  virtual ~RuntimeMonitor() = default;

  /**
   * @brief Sample all monitoring signals for one phase.
   * @param phase Point in the run at which the sample is taken.
   * @return A snapshot whose fields each carry an explicit state.
   */
  virtual Result<RuntimeSnapshot> sample(SamplingPhase phase) = 0;
};

/**
 * @brief Exposes CPU topology and affinity controls.
 * @note Rejected affinity requests are reported as unavailable with a reason;
 *       they never silently change the requested configuration (PLATFORM_MONITORING §4).
 */
class CpuControl {
public:
  /** @brief Allow derived implementations to be destroyed through this interface. */
  virtual ~CpuControl() = default;

  /**
   * @brief Read the physical core count.
   * @return Physical cores, or an explicit non-value state.
   */
  virtual Result<std::uint32_t> physical_core_count() const = 0;

  /**
   * @brief Read the logical core count.
   * @return Logical cores, or an explicit non-value state.
   */
  virtual Result<std::uint32_t> logical_core_count() const = 0;

  /**
   * @brief Read the current process affinity.
   * @return CPU set, or an explicit non-value state.
   */
  virtual Result<CpuSet> affinity() const = 0;

  /**
   * @brief Request a new process affinity.
   * @param cpus Requested CPU set; an empty set selects the OS default.
   * @return The applied CPU set, or an explicit non-value state with a reason.
   */
  virtual Result<CpuSet> set_affinity(const CpuSet &cpus) = 0;
};

/**
 * @brief Exposes worker-thread count control.
 * @note The reported count is the actual count used, which may differ from the
 *       requested count (PLATFORM_MONITORING §4).
 */
class WorkerControl {
public:
  /** @brief Allow derived implementations to be destroyed through this interface. */
  virtual ~WorkerControl() = default;

  /**
   * @brief Read the worker-thread count actually in use.
   * @return Worker count, or an explicit non-value state.
   */
  virtual Result<std::uint32_t> worker_count() const = 0;

  /**
   * @brief Request a worker-thread count.
   * @param requested Requested worker count; zero selects automatic behavior.
   * @return The applied count, or an explicit non-value state with a reason.
   */
  virtual Result<std::uint32_t> set_worker_count(std::uint32_t requested) = 0;
};

/**
 * @brief Exposes scheduling priority inspection and optional control.
 * @note Raising priority requires explicit user opt-in; implementations must
 *       warn about reduced system responsiveness and measurement bias before
 *       applying it (REQ-PLAT-06).
 */
class SchedulingPriority {
public:
  /** @brief Allow derived implementations to be destroyed through this interface. */
  virtual ~SchedulingPriority() = default;

  /**
   * @brief Read the inherited or current scheduling priority.
   * @return Priority value, or an explicit non-value state.
   */
  virtual Result<std::int32_t> current_priority() const = 0;

  /**
   * @brief Request a scheduling priority after user consent.
   * @param requested Requested priority value.
   * @return The applied priority, or an explicit non-value state with a reason.
   */
  virtual Result<std::int32_t> set_priority(std::int32_t requested) = 0;
};

/**
 * @brief Aggregate entry point for all platform capabilities.
 * @note Implementations are selected per OS; unsupported capabilities must be
 *       reported through @ref Result states rather than omitted (ARCHITECTURE §7).
 */
class PlatformAbstraction {
public:
  /** @brief Allow derived implementations to be destroyed through this interface. */
  virtual ~PlatformAbstraction() = default;

  /**
   * @brief Access the system information provider.
   * @return Reference owned by this abstraction.
   */
  virtual const SystemInfoProvider &system_info() const = 0;

  /**
   * @brief Access the runtime monitor.
   * @return Reference owned by this abstraction.
   */
  virtual RuntimeMonitor &runtime_monitor() = 0;

  /**
   * @brief Access the CPU topology and affinity control.
   * @return Reference owned by this abstraction.
   */
  virtual CpuControl &cpu_control() = 0;

  /**
   * @brief Access the worker-thread control.
   * @return Reference owned by this abstraction.
   */
  virtual WorkerControl &worker_control() = 0;

  /**
   * @brief Access the scheduling priority control.
   * @return Reference owned by this abstraction.
   */
  virtual SchedulingPriority &scheduling_priority() = 0;
};

} // namespace ztick::platform

#endif // ZTICK_PLATFORM_HPP
