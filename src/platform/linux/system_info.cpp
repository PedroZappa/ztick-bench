// SPDX-License-Identifier: MIT
/**
 * @file system_info.cpp
 * @brief Linux system information source, parsers and provider implementation.
 *
 * Parsers are pure functions over raw text so they can be tested with fixed
 * inputs. All filesystem and syscall access is isolated in
 * FilesystemSystemInfoSource, which keeps the provider deterministic under test.
 */

#include <ztick/platform/linux_system_info.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(__linux__)
#include <sched.h>
#include <sys/utsname.h>
#endif

/**
 * @brief Internal helpers shared by the Linux system-info parsers.
 */
namespace {

/**
 * @brief Parse an unsigned decimal integer from a whole string view.
 * @tparam T Unsigned integer target type.
 * @param text Text to parse.
 * @return Parsed value, or nullopt when the text is empty or malformed.
 */
template <typename T> std::optional<T> parse_unsigned(std::string_view text) {
  T value{};
  const char *begin = text.data();
  const char *end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

/**
 * @brief Parse a sysfs size string with an optional binary unit suffix.
 * @param text Text such as `48K`, `1M` or `32768`.
 * @return Size in bytes, or nullopt when the format is unsupported.
 */
std::optional<std::uint64_t> parse_size_bytes(std::string_view text) {
  std::uint64_t number = 0;
  const char *begin = text.data();
  const char *end = begin + text.size();
  const auto result = std::from_chars(begin, end, number);
  if (result.ec != std::errc{} || result.ptr == begin) {
    return std::nullopt;
  }
  std::uint64_t multiplier = 1;
  for (const char suffix : std::string_view(result.ptr, end)) {
    switch (suffix) {
    case 'K':
    case 'k':
      multiplier *= 1024;
      break;
    case 'M':
    case 'm':
      multiplier *= 1024 * 1024;
      break;
    case 'G':
    case 'g':
      multiplier *= 1024ULL * 1024 * 1024;
      break;
    default:
      return std::nullopt;
    }
  }
  return number * multiplier;
}

/**
 * @brief Read a key/value line field from colon-separated text.
 * @param line Line to inspect.
 * @param key Key to match after trimming.
 * @return Trimmed value, or nullopt when the key does not match.
 */
std::optional<std::string> field_value(const std::string &line, std::string_view key) {
  const auto colon = line.find(':');
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  std::string_view line_key{line.data(), colon};
  using ztick::platform::linux_sysinfo::trim;
  if (trim(std::string{line_key}) != key) {
    return std::nullopt;
  }
  return trim(line.substr(colon + 1));
}

/**
 * @brief Override a parsed result with `unsupported` for an absent capability.
 * @tparam T Result value type.
 * @param is_unsupported Whether the capability is unimplemented here.
 * @param reason Reason used when the capability is unsupported.
 * @param result Parsed result retained when the capability is supported.
 * @return Unsupported result or the original parsed result.
 */
template <typename T>
ztick::platform::Result<T> apply_capability(bool is_unsupported, const char *reason,
                                            ztick::platform::Result<T> result) {
  return is_unsupported ? ztick::platform::make_unsupported<T>(reason) : std::move(result);
}

} // namespace

namespace ztick::platform::linux_sysinfo {

bool FilesystemSystemInfoSource::read_file(std::string_view path, std::string &content) const {
  if (path.empty()) {
    return false;
  }
  std::ifstream stream{std::string{path}, std::ios::binary};
  if (!stream) {
    return false;
  }
  content.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
  return !stream.bad();
}

std::vector<std::string> FilesystemSystemInfoSource::cache_index_paths() const {
  std::vector<std::string> paths;
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator{kCacheRootPath, error}) {
    const auto name = entry.path().filename().string();
    if (name.rfind("index", 0) == 0) {
      paths.push_back(entry.path().string());
    }
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

UnameInfo FilesystemSystemInfoSource::uname() const {
  UnameInfo info;
#if defined(__linux__)
  utsname data{};
  if (::uname(&data) == 0) {
    info.sysname = data.sysname;
    info.release = data.release;
    info.machine = data.machine;
  }
#endif
  return info;
}

const char *FilesystemSystemInfoSource::environment_variable(const char *name) const {
  if (name == nullptr) {
    return nullptr;
  }
  return std::getenv(name);
}

bool FilesystemSystemInfoSource::current_affinity(std::vector<std::uint32_t> &cpus) const {
  cpus.clear();
#if defined(__linux__)
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {
    return false;
  }
  for (std::uint32_t cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(static_cast<int>(cpu), &mask)) {
      cpus.push_back(cpu);
    }
  }
  return true;
#else
  return false;
#endif
}

std::string FilesystemSystemInfoSource::scheduling_configuration() const {
#if defined(__linux__)
  const int policy = ::sched_getscheduler(0);
  if (policy < 0) {
    return {};
  }
  switch (policy) {
  case SCHED_OTHER:
    return "policy=SCHED_OTHER";
  case SCHED_FIFO:
    return "policy=SCHED_FIFO";
  case SCHED_RR:
    return "policy=SCHED_RR";
  case SCHED_BATCH:
    return "policy=SCHED_BATCH";
  case SCHED_IDLE:
    return "policy=SCHED_IDLE";
  default:
    return "policy=unknown";
  }
#else
  return {};
#endif
}

std::string trim(const std::string &text) {
  constexpr std::string_view whitespace = " \t\r\n";
  const auto first = text.find_first_not_of(whitespace);
  if (first == std::string::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(whitespace);
  return text.substr(first, last - first + 1);
}

bool is_simd_feature(std::string_view name) {
  static constexpr std::array<std::string_view, 8> prefixes{"mmx", "sse", "ssse", "avx",
                                                            "fma", "xop", "neon", "asimd"};
  return std::any_of(prefixes.begin(), prefixes.end(),
                     [name](const std::string_view prefix) { return name.starts_with(prefix); });
}

Result<std::string> parse_cpu_model(const std::string &cpuinfo) {
  std::istringstream stream{cpuinfo};
  std::string line;
  while (std::getline(stream, line)) {
    const auto model = field_value(line, "model name");
    if (model && !model->empty()) {
      return make_value(*model);
    }
  }
  return make_unavailable<std::string>("CPU model name not exposed");
}

Result<std::uint32_t> parse_physical_core_count(const std::string &cpuinfo) {
  std::istringstream stream{cpuinfo};
  std::string line;
  std::optional<std::uint32_t> physical;
  std::optional<std::uint32_t> core;
  std::set<std::uint64_t> pairs;

  const auto finalize = [&]() {
    if (physical && core) {
      const auto key = (static_cast<std::uint64_t>(*physical) << 32U) | *core;
      pairs.insert(key);
    }
    physical.reset();
    core.reset();
  };

  while (std::getline(stream, line)) {
    if (trim(line).empty()) {
      finalize();
      continue;
    }
    if (const auto value = field_value(line, "physical id")) {
      physical = parse_unsigned<std::uint32_t>(*value);
    } else if (const auto value = field_value(line, "core id")) {
      core = parse_unsigned<std::uint32_t>(*value);
    }
  }
  finalize();

  if (pairs.empty() || pairs.size() > std::numeric_limits<std::uint32_t>::max()) {
    return make_unavailable<std::uint32_t>("CPU topology not exposed");
  }
  return make_value(static_cast<std::uint32_t>(pairs.size()));
}

Result<std::uint32_t> parse_logical_core_count(const std::string &cpuinfo) {
  std::uint64_t count = 0;
  std::istringstream stream{cpuinfo};
  std::string line;
  while (std::getline(stream, line)) {
    if (field_value(line, "processor")) {
      ++count;
    }
  }
  if (count == 0 || count > std::numeric_limits<std::uint32_t>::max()) {
    return make_unavailable<std::uint32_t>("logical core count not exposed");
  }
  return make_value(static_cast<std::uint32_t>(count));
}

Result<double> parse_cpu_frequency_hz(const std::string &frequency_khz) {
  const auto text = trim(frequency_khz);
  double kilohertz = 0.0;
  const char *begin = text.data();
  const char *end = begin + text.size();
  const auto result = std::from_chars(begin, end, kilohertz);
  if (result.ec != std::errc{} || result.ptr != end || !(kilohertz > 0.0)) {
    return make_unavailable<double>("CPU frequency not exposed");
  }
  return make_value(kilohertz * 1000.0);
}

Result<CacheInfo> parse_cache_entry(const std::string &level, const std::string &size,
                                    const std::string &type) {
  const auto level_value = parse_unsigned<std::uint32_t>(trim(level));
  if (!level_value || *level_value == 0) {
    return make_unavailable<CacheInfo>("cache level not exposed");
  }
  const auto bytes = parse_size_bytes(trim(size));
  if (!bytes) {
    return make_unavailable<CacheInfo>("cache size not exposed");
  }
  CacheInfo info;
  info.level = *level_value;
  info.size_bytes = *bytes;
  info.type = trim(type);
  return make_value(std::move(info));
}

Result<std::uint64_t> parse_memory_bytes(const std::string &meminfo, std::string_view key) {
  std::istringstream stream{meminfo};
  std::string line;
  while (std::getline(stream, line)) {
    const auto value = field_value(line, key);
    if (!value) {
      continue;
    }
    std::istringstream parts{*value};
    std::uint64_t amount = 0;
    std::string unit;
    if (!(parts >> amount >> unit)) {
      return make_unavailable<std::uint64_t>("memory size malformed");
    }
    std::uint64_t multiplier = 1;
    if (unit == "kB" || unit == "KB") {
      multiplier = 1024;
    } else if (unit == "MB") {
      multiplier = 1024ULL * 1024;
    } else if (unit == "GB") {
      multiplier = 1024ULL * 1024 * 1024;
    } else if (unit != "B") {
      return make_unavailable<std::uint64_t>("memory size has unknown unit");
    }
    return make_value(amount * multiplier);
  }
  return make_unavailable<std::uint64_t>("memory size not exposed");
}

Result<std::vector<std::string>> parse_simd_features(const std::string &cpuinfo) {
  std::istringstream stream{cpuinfo};
  std::string line;
  while (std::getline(stream, line)) {
    const auto flags = field_value(line, "flags");
    const auto arm_flags = flags ? flags : field_value(line, "Features");
    if (!arm_flags) {
      continue;
    }
    std::vector<std::string> features;
    std::istringstream values{*arm_flags};
    std::string name;
    while (values >> name) {
      if (is_simd_feature(name)) {
        features.push_back(name);
      }
    }
    if (!features.empty()) {
      return make_value(std::move(features));
    }
  }
  return make_unavailable<std::vector<std::string>>("CPU feature flags not exposed");
}

Result<std::string> parse_nonempty_string(const std::string &value, const char *reason) {
  const auto text = trim(value);
  if (text.empty()) {
    return make_unavailable<std::string>(reason);
  }
  return make_value(text);
}

Result<std::vector<std::string>> collect_environment_metadata(const SystemInfoSource &source) {
  static constexpr const char *names[] = {"XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "DISPLAY",
                                          "XDG_CURRENT_DESKTOP"};
  std::vector<std::string> entries;
  for (const char *name : names) {
    const char *value = source.environment_variable(name);
    if (value == nullptr || *value == '\0') {
      continue;
    }
    entries.push_back(std::string{name} + "=" + value);
  }
  if (entries.empty()) {
    return make_unavailable<std::vector<std::string>>("no relevant environment variables set");
  }
  return make_value(std::move(entries));
}

Result<std::vector<CacheInfo>> collect_caches(const SystemInfoSource &source) {
  std::vector<CacheInfo> caches;
  for (const auto &path : source.cache_index_paths()) {
    std::string level;
    std::string size;
    std::string type;
    if (!source.read_file(path + "/level", level) || !source.read_file(path + "/size", size) ||
        !source.read_file(path + "/type", type)) {
      continue;
    }
    auto entry = parse_cache_entry(level, size, type);
    if (entry.has_value()) {
      caches.push_back(std::move(entry.value));
    }
  }
  if (caches.empty()) {
    return make_unavailable<std::vector<CacheInfo>>("cache topology not exposed");
  }
  return make_value(std::move(caches));
}

LinuxSystemInfoProvider::LinuxSystemInfoProvider(std::unique_ptr<const SystemInfoSource> source,
                                                 std::vector<Capability> unsupported_capabilities)
    : source_(std::move(source)), unsupported_capabilities_(std::move(unsupported_capabilities)) {}

bool LinuxSystemInfoProvider::unsupported(Capability capability) const {
  return std::find(unsupported_capabilities_.begin(), unsupported_capabilities_.end(),
                   capability) != unsupported_capabilities_.end();
}

Result<SystemInfo> LinuxSystemInfoProvider::collect() const {
  SystemInfo info;

  std::string cpuinfo;
  const bool cpuinfo_ok = source_->read_file(kCpuInfoPath, cpuinfo);
  const UnameInfo identity = source_->uname();

  constexpr const char *identity_reason = "CPU identity not implemented on this platform";
  info.cpu_model =
      apply_capability(unsupported(Capability::cpu_identity), identity_reason,
                       cpuinfo_ok ? parse_cpu_model(cpuinfo)
                                  : make_unavailable<std::string>("/proc/cpuinfo unreadable"));
  info.cpu_architecture =
      apply_capability(unsupported(Capability::cpu_identity), identity_reason,
                       parse_nonempty_string(identity.machine, "CPU architecture not exposed"));

  constexpr const char *topology_reason = "CPU topology not implemented on this platform";
  info.physical_core_count =
      apply_capability(unsupported(Capability::cpu_topology), topology_reason,
                       cpuinfo_ok ? parse_physical_core_count(cpuinfo)
                                  : make_unavailable<std::uint32_t>("/proc/cpuinfo unreadable"));
  info.logical_core_count =
      apply_capability(unsupported(Capability::cpu_topology), topology_reason,
                       cpuinfo_ok ? parse_logical_core_count(cpuinfo)
                                  : make_unavailable<std::uint32_t>("/proc/cpuinfo unreadable"));

  constexpr const char *frequency_reason = "CPU frequency control not implemented on this platform";
  std::string frequency;
  info.cpu_frequency_hz =
      apply_capability(unsupported(Capability::cpu_frequency), frequency_reason,
                       source_->read_file(kCpuFrequencyPath, frequency)
                           ? parse_cpu_frequency_hz(frequency)
                           : make_unavailable<double>("CPU frequency source unreadable"));

  std::string governor;
  info.scaling_governor =
      apply_capability(unsupported(Capability::cpu_frequency), frequency_reason,
                       source_->read_file(kScalingGovernorPath, governor)
                           ? parse_nonempty_string(governor, "scaling governor empty")
                           : make_unavailable<std::string>("scaling governor source unreadable"));

  info.caches =
      apply_capability(unsupported(Capability::caches),
                       "cache topology not implemented on this platform", collect_caches(*source_));

  info.simd_features = apply_capability(
      unsupported(Capability::simd_features), "SIMD detection not implemented on this platform",
      cpuinfo_ok ? parse_simd_features(cpuinfo)
                 : make_unavailable<std::vector<std::string>>("/proc/cpuinfo unreadable"));

  constexpr const char *memory_reason = "memory reporting not implemented on this platform";
  std::string meminfo;
  const bool meminfo_ok = source_->read_file(kMemInfoPath, meminfo);
  info.total_ram_bytes =
      apply_capability(unsupported(Capability::memory), memory_reason,
                       meminfo_ok ? parse_memory_bytes(meminfo, "MemTotal")
                                  : make_unavailable<std::uint64_t>("/proc/meminfo unreadable"));
  info.available_ram_bytes =
      apply_capability(unsupported(Capability::memory), memory_reason,
                       meminfo_ok ? parse_memory_bytes(meminfo, "MemAvailable")
                                  : make_unavailable<std::uint64_t>("/proc/meminfo unreadable"));

  constexpr const char *os_reason = "OS identification not implemented on this platform";
  info.os_name = apply_capability(unsupported(Capability::os_info), os_reason,
                                  parse_nonempty_string(identity.sysname, "OS name not exposed"));
  info.kernel_version =
      apply_capability(unsupported(Capability::os_info), os_reason,
                       parse_nonempty_string(identity.release, "kernel version not exposed"));

  constexpr const char *audio_reason = "audio discovery not implemented on this platform";
  constexpr const char *audio_mode_reason = "audio discovery requires a selected benchmark mode";
  info.audio_backend = unsupported(Capability::audio)
                           ? make_unsupported<std::string>(audio_reason)
                           : make_not_applicable<std::string>(audio_mode_reason);
  info.audio_device = unsupported(Capability::audio)
                          ? make_unsupported<std::string>(audio_reason)
                          : make_not_applicable<std::string>(audio_mode_reason);

  constexpr const char *scheduling_reason = "scheduling control not implemented on this platform";
  std::vector<std::uint32_t> cpus;
  info.process_affinity =
      apply_capability(unsupported(Capability::scheduling), scheduling_reason,
                       source_->current_affinity(cpus) && !cpus.empty()
                           ? make_value(CpuSet{std::move(cpus)})
                           : make_unavailable<CpuSet>("process affinity not exposed"));
  info.scheduling_configuration =
      apply_capability(unsupported(Capability::scheduling), scheduling_reason,
                       parse_nonempty_string(source_->scheduling_configuration(),
                                             "scheduling configuration not exposed"));

  info.environment_metadata = apply_capability(
      unsupported(Capability::environment), "environment metadata not implemented on this platform",
      collect_environment_metadata(*source_));

  return make_value(std::move(info));
}

} // namespace ztick::platform::linux_sysinfo
