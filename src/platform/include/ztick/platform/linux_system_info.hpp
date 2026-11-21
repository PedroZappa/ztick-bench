// SPDX-License-Identifier: MIT
/**
 * @file linux_system_info.hpp
 * @brief Linux implementation of the static system information inventory.
 *
 * This header declares the Linux data source, the pure parsers used to decode
 * `/proc` and `/sys` text, and the concrete @ref ztick::platform::SystemInfoProvider
 * implementation. Raw data access is behind
 * @ref ztick::platform::linux_sysinfo::SystemInfoSource so tests can
 * inject deterministic content and simulate missing or unsupported data
 * (PLATFORM_MONITORING §2, REQ-PLAT-03). This header is private to ztick-bench
 * and is not installed.
 */

#ifndef ZTICK_LINUX_SYSTEM_INFO_HPP
#define ZTICK_LINUX_SYSTEM_INFO_HPP

#include <ztick/platform.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Linux-specific implementation of the platform inventory.
 * @note Every field is produced with an explicit availability state; no missing
 *       data is silently omitted.
 */
namespace ztick::platform::linux_sysinfo {

/** @brief Canonical path to the per-CPU description file. */
inline constexpr const char *kCpuInfoPath = "/proc/cpuinfo";
/** @brief Canonical path to the memory description file. */
inline constexpr const char *kMemInfoPath = "/proc/meminfo";
/** @brief Path to the current CPU frequency in kHz. */
inline constexpr const char *kCpuFrequencyPath =
    "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq";
/** @brief Path to the active CPU frequency scaling governor. */
inline constexpr const char *kScalingGovernorPath =
    "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
/** @brief Root directory containing `indexN` cache description directories. */
inline constexpr const char *kCacheRootPath = "/sys/devices/system/cpu/cpu0/cache";

/**
 * @brief Kernel identification triple returned by `uname`.
 */
struct UnameInfo {
  /** Kernel name, for example `Linux`. */
  std::string sysname;
  /** Kernel release, for example `6.8.0`. */
  std::string release;
  /** Machine architecture, for example `x86_64`. */
  std::string machine;
};

/**
 * @brief Injectable source of raw Linux system data.
 * @note Every method reports absence through an empty string, an empty vector,
 *       `nullptr`, or `false`; the provider converts that into an explicit
 *       availability state. Implementations must not throw for expected
 *       absence.
 */
class SystemInfoSource {
public:
  /** @brief Allow derived sources to be destroyed through this interface. */
  virtual ~SystemInfoSource() = default;

  /**
   * @brief Read the whole text content of a file.
   * @param path File path to read.
   * @param content Output populated with the file content on success.
   * @return True when the file was read, false when it is absent or unreadable.
   */
  virtual bool read_file(std::string_view path, std::string &content) const = 0;

  /**
   * @brief Enumerate cache index directories under @ref kCacheRootPath.
   * @return Sorted directory paths, or an empty vector when unavailable.
   */
  virtual std::vector<std::string> cache_index_paths() const = 0;

  /**
   * @brief Read kernel identification fields.
   * @return The `uname` triple; individual empty strings mean unavailable.
   */
  virtual UnameInfo uname() const = 0;

  /**
   * @brief Look up an environment variable.
   * @param name Variable name.
   * @return Its value, or `nullptr` when absent. Storage is owned by the source.
   */
  virtual const char *environment_variable(const char *name) const = 0;

  /**
   * @brief Read the current process CPU affinity.
   * @param cpus Output populated with zero-based CPU identifiers on success.
   * @return True when the affinity mask was read, false when unavailable.
   */
  virtual bool current_affinity(std::vector<std::uint32_t> &cpus) const = 0;

  /**
   * @brief Describe the active scheduling configuration.
   * @return Human-readable description, or an empty string when unavailable.
   */
  virtual std::string scheduling_configuration() const = 0;
};

/**
 * @brief SystemInfoSource backed by the live Linux filesystem and syscalls.
 */
class FilesystemSystemInfoSource final : public SystemInfoSource {
public:
  /**
   * @brief Read a file from the real filesystem.
   * @param path File path to read.
   * @param content Output populated with the file content on success.
   * @return True when the file was read, false when absent or unreadable.
   */
  bool read_file(std::string_view path, std::string &content) const override;

  /**
   * @brief Enumerate real sysfs cache index directories.
   * @return Sorted directory paths, or an empty vector when unavailable.
   */
  std::vector<std::string> cache_index_paths() const override;

  /**
   * @brief Read the real kernel identification via `uname`.
   * @return The `uname` triple; empty fields mean unavailable.
   */
  UnameInfo uname() const override;

  /**
   * @brief Look up a real environment variable via `std::getenv`.
   * @param name Variable name.
   * @return Its value, or `nullptr` when absent. Storage is owned by the process.
   */
  const char *environment_variable(const char *name) const override;

  /**
   * @brief Read the real process affinity via `sched_getaffinity`.
   * @param cpus Output populated with zero-based CPU identifiers on success.
   * @return True when the syscall succeeded, false when unavailable.
   */
  bool current_affinity(std::vector<std::uint32_t> &cpus) const override;

  /**
   * @brief Describe the real scheduler policy via `sched_getscheduler`.
   * @return Policy description, or an empty string when unavailable.
   */
  std::string scheduling_configuration() const override;
};

/**
 * @brief Remove ASCII whitespace and carriage returns from both ends.
 * @param text Text to trim.
 * @return Trimmed copy, empty when the input is only whitespace.
 */
std::string trim(const std::string &text);

/**
 * @brief Report whether a CPU feature flag denotes a SIMD capability.
 * @param name Feature flag name.
 * @return True for recognised SIMD families (MMX, SSE, AVX, FMA and NEON).
 */
bool is_simd_feature(std::string_view name);

/**
 * @brief Parse the first `model name` entry from `/proc/cpuinfo` content.
 * @param cpuinfo Raw `/proc/cpuinfo` content.
 * @return CPU model, or unavailable when no model name is exposed.
 */
Result<std::string> parse_cpu_model(const std::string &cpuinfo);

/**
 * @brief Count distinct physical-core `(physical id, core id)` pairs.
 * @param cpuinfo Raw `/proc/cpuinfo` content.
 * @return Physical core count, or unavailable when topology is not exposed.
 */
Result<std::uint32_t> parse_physical_core_count(const std::string &cpuinfo);

/**
 * @brief Count `processor` entries from `/proc/cpuinfo` content.
 * @param cpuinfo Raw `/proc/cpuinfo` content.
 * @return Logical core count, or unavailable when no processors are listed.
 */
Result<std::uint32_t> parse_logical_core_count(const std::string &cpuinfo);

/**
 * @brief Convert a scaling frequency value from kHz to Hz.
 * @param frequency_khz Raw frequency text in kHz.
 * @return Frequency in Hz, or unavailable when the value is missing or invalid.
 */
Result<double> parse_cpu_frequency_hz(const std::string &frequency_khz);

/**
 * @brief Assemble one cache description from its sysfs text files.
 * @param level Raw `level` file content.
 * @param size Raw `size` file content, such as `48K` or `1M`.
 * @param type Raw `type` file content.
 * @return Cache description, or unavailable when any field is invalid.
 */
Result<CacheInfo> parse_cache_entry(const std::string &level, const std::string &size,
                                    const std::string &type);

/**
 * @brief Parse one memory amount from `/proc/meminfo` content.
 * @param meminfo Raw `/proc/meminfo` content.
 * @param key Field name such as `MemTotal`.
 * @return Amount in bytes, or unavailable when the key or unit is unknown.
 */
Result<std::uint64_t> parse_memory_bytes(const std::string &meminfo, std::string_view key);

/**
 * @brief Extract declared SIMD feature names from `/proc/cpuinfo` content.
 * @param cpuinfo Raw `/proc/cpuinfo` content.
 * @return SIMD feature names, or unavailable when no feature flags are exposed.
 */
Result<std::vector<std::string>> parse_simd_features(const std::string &cpuinfo);

/**
 * @brief Convert raw text into a value when it is not blank.
 * @param value Raw text.
 * @param reason Reason used when the text is blank; must outlive the result.
 * @return Trimmed text, or unavailable with @p reason when blank.
 */
Result<std::string> parse_nonempty_string(const std::string &value, const char *reason);

/**
 * @brief Collect selected environment metadata as `NAME=VALUE` strings.
 * @param source Source providing environment variables.
 * @return Environment entries, or unavailable when none of the known names is set.
 */
Result<std::vector<std::string>> collect_environment_metadata(const SystemInfoSource &source);

/**
 * @brief Collect the cache topology from a data source.
 * @param source Source providing cache index paths and files.
 * @return Parsed cache entries, or unavailable when none can be read.
 */
Result<std::vector<CacheInfo>> collect_caches(const SystemInfoSource &source);

/**
 * @brief Individually reported groups of system-information capabilities.
 * @note A group marked unsupported yields `ztick_availability_unsupported` for
 *       its fields rather than an omission (PLATFORM_MONITORING §6).
 */
enum class Capability : std::uint32_t {
  /** CPU model and architecture. */
  cpu_identity = 0,
  /** Physical and logical core counts. */
  cpu_topology,
  /** CPU frequency and scaling governor. */
  cpu_frequency,
  /** Cache topology. */
  caches,
  /** Declared SIMD features. */
  simd_features,
  /** Installed and available RAM. */
  memory,
  /** OS name and kernel version. */
  os_info,
  /** Audio backend and device discovery. */
  audio,
  /** Process affinity and scheduling configuration. */
  scheduling,
  /** Environment metadata. */
  environment,
};

/**
 * @brief Linux implementation of the static system information provider.
 */
class LinuxSystemInfoProvider final : public SystemInfoProvider {
public:
  /**
   * @brief Construct a provider over an owned data source.
   * @param source Source used for every subsequent collection; must not be null.
   * @param unsupported_capabilities Capability groups this platform does not
   *        implement; their fields are reported as `unsupported`. Defaults to
   *        an empty set, meaning every group is implemented.
   */
  explicit LinuxSystemInfoProvider(std::unique_ptr<const SystemInfoSource> source,
                                   std::vector<Capability> unsupported_capabilities = {});

  /**
   * @brief Collect the complete inventory from the configured source.
   * @return A value result containing explicit per-field states.
   */
  Result<SystemInfo> collect() const override;

private:
  /**
   * @brief Report whether a capability group is unimplemented on this platform.
   * @param capability Capability to check.
   * @return True when @p capability is in the configured unsupported set.
   */
  bool unsupported(Capability capability) const;

  /** @brief Owned source of raw system data. */
  std::unique_ptr<const SystemInfoSource> source_;
  /** @brief Capability groups reported as unsupported rather than collected. */
  std::vector<Capability> unsupported_capabilities_;
};

/**
 * @brief Create a provider backed by the live Linux system.
 * @return Provider using @ref FilesystemSystemInfoSource.
 */
std::unique_ptr<SystemInfoProvider> make_linux_system_info_provider();

} // namespace ztick::platform::linux_sysinfo

#endif // ZTICK_LINUX_SYSTEM_INFO_HPP
