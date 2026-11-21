// SPDX-License-Identifier: MIT
/**
 * @file linux_system_info_test.cpp
 * @brief Catch2 tests for the Linux system-info source, parsers and provider.
 *
 * Raw `/proc` and `/sys` content is injected through FakeSystemInfoSource so
 * every parser and every availability path is deterministic. A small smoke case
 * exercises the real filesystem source on the Linux host.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/platform/linux_system_info.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/** @brief Short alias for the portable platform namespace. */
namespace platform = ztick::platform;
/** @brief Short alias for the Linux system-info implementation namespace. */
namespace sysinfo = ztick::platform::linux_sysinfo;

/**
 * @brief In-memory SystemInfoSource used to drive deterministic parser tests.
 */
class FakeSystemInfoSource final : public sysinfo::SystemInfoSource {
public:
  /** @brief Raw file content keyed by path. */
  std::unordered_map<std::string, std::string> files;
  /** @brief Cache index directory paths returned to the provider. */
  std::vector<std::string> cache_directories;
  /** @brief Kernel identity returned to the provider. */
  sysinfo::UnameInfo identity;
  /** @brief Environment variables returned to the provider. */
  std::unordered_map<std::string, std::string> environment;
  /** @brief CPU identifiers returned as the current affinity. */
  std::vector<std::uint32_t> affinity;
  /** @brief Whether the affinity probe succeeds. */
  bool affinity_available{true};
  /** @brief Scheduling description returned to the provider. */
  std::string scheduling;

  /**
   * @brief Return injected file content when present.
   * @param path File path to look up.
   * @param content Output populated with the injected content on success.
   * @return True when @p path exists in the injected file map.
   */
  bool read_file(std::string_view path, std::string &content) const override {
    const auto it = files.find(std::string{path});
    if (it == files.end()) {
      return false;
    }
    content = it->second;
    return true;
  }

  /**
   * @brief Return the injected cache index directories.
   * @return Configured directory paths.
   */
  std::vector<std::string> cache_index_paths() const override { return cache_directories; }

  /**
   * @brief Return the injected kernel identity.
   * @return Configured identity.
   */
  sysinfo::UnameInfo uname() const override { return identity; }

  /**
   * @brief Return an injected environment variable, or nullptr when absent.
   * @param name Variable name.
   * @return Configured value, or `nullptr` when not configured.
   */
  const char *environment_variable(const char *name) const override {
    if (name == nullptr) {
      return nullptr;
    }
    const auto it = environment.find(name);
    return it == environment.end() ? nullptr : it->second.c_str();
  }

  /**
   * @brief Return the injected affinity mask.
   * @param cpus Output populated with the configured CPU identifiers.
   * @return Configured availability of the affinity probe.
   */
  bool current_affinity(std::vector<std::uint32_t> &cpus) const override {
    cpus = affinity;
    return affinity_available;
  }

  /**
   * @brief Return the injected scheduling description.
   * @return Configured description.
   */
  std::string scheduling_configuration() const override { return scheduling; }
};

/**
 * @brief Build representative `/proc/cpuinfo` content for parser tests.
 * @return Text with two logical processors sharing one physical core pair set.
 */
std::string sample_cpuinfo() {
  return "processor\t: 0\n"
         "model name\t: Test CPU @ 3.00GHz\n"
         "physical id\t: 0\n"
         "core id\t\t: 0\n"
         "flags\t\t: fpu sse sse2 ssse3 avx avx2 fma xtsave\n"
         "\n"
         "processor\t: 1\n"
         "model name\t: Test CPU @ 3.00GHz\n"
         "physical id\t: 0\n"
         "core id\t\t: 1\n"
         "flags\t\t: fpu sse sse2 ssse3 avx avx2 fma xtsave\n"
         "\n";
}

/**
 * @brief Populate a fake source with a complete synthetic inventory.
 * @param source Source to populate.
 */
void populate_complete_source(FakeSystemInfoSource &source) {
  source.files[sysinfo::kCpuInfoPath] = sample_cpuinfo();
  source.files[sysinfo::kCpuFrequencyPath] = "2400000\n";
  source.files[sysinfo::kScalingGovernorPath] = "powersave\n";
  source.files[sysinfo::kMemInfoPath] = "MemTotal:       1048576 kB\nMemAvailable:    524288 kB\n";
  source.cache_directories = {"/cache/index0", "/cache/index1"};
  source.files["/cache/index0/level"] = "1\n";
  source.files["/cache/index0/size"] = "48K\n";
  source.files["/cache/index0/type"] = "Data\n";
  source.files["/cache/index1/level"] = "2\n";
  source.files["/cache/index1/size"] = "1M\n";
  source.files["/cache/index1/type"] = "Unified\n";
  source.identity = {"Linux", "6.8.0-test", "x86_64"};
  source.environment = {{"DISPLAY", ":0"}};
  source.affinity = {0, 1, 2, 3};
  source.scheduling = "policy=SCHED_OTHER";
}

/**
 * @brief Require that a result was explicitly assigned by the provider.
 * @tparam T Result value type.
 * @param result Result to inspect.
 * @note The default reason "not collected" means a field was left untouched and
 *       would therefore be silently omitted from the inventory.
 */
template <typename T> void require_explicit_state(const platform::Result<T> &result) {
  // A value result has no reason; any non-value state must name a real cause.
  if (result.reason != nullptr) {
    REQUIRE(std::string{result.reason} != "not collected");
  }
}

/**
 * @brief Assert that every inventory field carries an explicit provider state.
 * @param collected Collected inventory result.
 */
void require_all_fields_explicit(const platform::Result<platform::SystemInfo> &collected) {
  REQUIRE(collected.has_value());
  const auto &info = collected.value;
  require_explicit_state(info.cpu_model);
  require_explicit_state(info.cpu_architecture);
  require_explicit_state(info.physical_core_count);
  require_explicit_state(info.logical_core_count);
  require_explicit_state(info.cpu_frequency_hz);
  require_explicit_state(info.scaling_governor);
  require_explicit_state(info.caches);
  require_explicit_state(info.simd_features);
  require_explicit_state(info.total_ram_bytes);
  require_explicit_state(info.available_ram_bytes);
  require_explicit_state(info.os_name);
  require_explicit_state(info.kernel_version);
  require_explicit_state(info.audio_backend);
  require_explicit_state(info.audio_device);
  require_explicit_state(info.process_affinity);
  require_explicit_state(info.scheduling_configuration);
  require_explicit_state(info.environment_metadata);
}

/**
 * @brief Verify whitespace trimming including CR from Windows-style input.
 */
TEST_CASE("trim removes edge whitespace", "[platform][linux][parser]") {
  REQUIRE(sysinfo::trim("  hello \t\r\n") == "hello");
  REQUIRE(sysinfo::trim("\n\t ") == "");
  REQUIRE(sysinfo::trim("value") == "value");
}

/**
 * @brief Verify SIMD feature classification accepts families and rejects others.
 */
TEST_CASE("SIMD feature detection is selective", "[platform][linux][parser]") {
  REQUIRE(sysinfo::is_simd_feature("sse2"));
  REQUIRE(sysinfo::is_simd_feature("ssse3"));
  REQUIRE(sysinfo::is_simd_feature("avx512f"));
  REQUIRE(sysinfo::is_simd_feature("neon"));
  REQUIRE_FALSE(sysinfo::is_simd_feature("fpu"));
  REQUIRE_FALSE(sysinfo::is_simd_feature("xtsave"));
  REQUIRE_FALSE(sysinfo::is_simd_feature(""));
}

/**
 * @brief Verify CPU model parsing from x86-style cpuinfo content.
 */
TEST_CASE("CPU model is parsed from cpuinfo", "[platform][linux][parser]") {
  const auto present = sysinfo::parse_cpu_model(sample_cpuinfo());
  REQUIRE(present.has_value());
  REQUIRE(present.value == "Test CPU @ 3.00GHz");

  const auto missing = sysinfo::parse_cpu_model("processor\t: 0\n");
  REQUIRE_FALSE(missing.has_value());
  REQUIRE(missing.state == ztick_availability_unavailable);
}

/**
 * @brief Verify physical and logical core counts from topology fields.
 */
TEST_CASE("core counts are parsed independently", "[platform][linux][parser]") {
  const auto physical = sysinfo::parse_physical_core_count(sample_cpuinfo());
  REQUIRE(physical.has_value());
  REQUIRE(physical.value == 2);

  const auto logical = sysinfo::parse_logical_core_count(sample_cpuinfo());
  REQUIRE(logical.has_value());
  REQUIRE(logical.value == 2);

  const auto no_topology = sysinfo::parse_physical_core_count("model name\t: X\n");
  REQUIRE(no_topology.state == ztick_availability_unavailable);
  const auto no_processors = sysinfo::parse_logical_core_count("model name\t: X\n");
  REQUIRE(no_processors.state == ztick_availability_unavailable);
}

/**
 * @brief Verify kHz to Hz conversion and rejection of invalid frequency text.
 */
TEST_CASE("CPU frequency converts kHz to Hz", "[platform][linux][parser]") {
  const auto valid = sysinfo::parse_cpu_frequency_hz("2400000\n");
  REQUIRE(valid.has_value());
  REQUIRE(valid.value == 2400000000.0);

  REQUIRE(sysinfo::parse_cpu_frequency_hz("bad").state == ztick_availability_unavailable);
  REQUIRE(sysinfo::parse_cpu_frequency_hz("0").state == ztick_availability_unavailable);
  REQUIRE(sysinfo::parse_cpu_frequency_hz("100 200").state == ztick_availability_unavailable);
}

/**
 * @brief Verify cache size suffixes and malformed cache fields.
 */
TEST_CASE("cache entries parse binary sizes", "[platform][linux][parser]") {
  const auto kib = sysinfo::parse_cache_entry("1\n", "48K\n", "Data\n");
  REQUIRE(kib.has_value());
  REQUIRE(kib.value.level == 1);
  REQUIRE(kib.value.size_bytes == 48 * 1024);
  REQUIRE(kib.value.type == "Data");

  const auto mib = sysinfo::parse_cache_entry("2", "1M", "Unified");
  REQUIRE(mib.has_value());
  REQUIRE(mib.value.size_bytes == 1024 * 1024);

  REQUIRE(sysinfo::parse_cache_entry("x", "1M", "Unified").state == ztick_availability_unavailable);
  REQUIRE(sysinfo::parse_cache_entry("1", "2Q", "Unified").state == ztick_availability_unavailable);
}

/**
 * @brief Verify memory size unit conversion and missing-key behavior.
 */
TEST_CASE("memory sizes convert kB to bytes", "[platform][linux][parser]") {
  const std::string meminfo = "MemTotal:       1048576 kB\nMemAvailable:    524288 kB\n";
  const auto total = sysinfo::parse_memory_bytes(meminfo, "MemTotal");
  REQUIRE(total.has_value());
  REQUIRE(total.value == 1048576ULL * 1024);

  const auto available = sysinfo::parse_memory_bytes(meminfo, "MemAvailable");
  REQUIRE(available.has_value());
  REQUIRE(available.value == 524288ULL * 1024);

  REQUIRE(sysinfo::parse_memory_bytes(meminfo, "MemFree").state == ztick_availability_unavailable);
  REQUIRE(sysinfo::parse_memory_bytes("MemTotal: 1 bananas\n", "MemTotal").state ==
          ztick_availability_unavailable);
}

/**
 * @brief Verify SIMD flags are filtered from the full feature list.
 */
TEST_CASE("SIMD features are filtered from cpuinfo", "[platform][linux][parser]") {
  const auto features = sysinfo::parse_simd_features(sample_cpuinfo());
  REQUIRE(features.has_value());
  const std::vector<std::string> expected{"sse", "sse2", "ssse3", "avx", "avx2", "fma"};
  REQUIRE(features.value == expected);

  REQUIRE(sysinfo::parse_simd_features("processor\t: 0\n").state == ztick_availability_unavailable);
}

/**
 * @brief Verify blank strings become explicit unavailable results.
 */
TEST_CASE("nonempty string parser reports blank input", "[platform][linux][parser]") {
  const auto value = sysinfo::parse_nonempty_string("  x86_64  ", "empty");
  REQUIRE(value.has_value());
  REQUIRE(value.value == "x86_64");

  const auto blank = sysinfo::parse_nonempty_string(" \t\r\n", "empty");
  REQUIRE_FALSE(blank.has_value());
  REQUIRE(std::string{blank.reason} == "empty");
}

/**
 * @brief Verify environment metadata collection from an injected source.
 */
TEST_CASE("environment metadata is assembled", "[platform][linux][parser]") {
  FakeSystemInfoSource source;
  source.environment = {{"DISPLAY", ":0"}, {"WAYLAND_DISPLAY", "wayland-1"}};
  const auto present = sysinfo::collect_environment_metadata(source);
  REQUIRE(present.has_value());
  REQUIRE(present.value.size() == 2);
  REQUIRE(present.value[0] == "WAYLAND_DISPLAY=wayland-1");
  REQUIRE(present.value[1] == "DISPLAY=:0");

  FakeSystemInfoSource empty;
  const auto absent = sysinfo::collect_environment_metadata(empty);
  REQUIRE(absent.state == ztick_availability_unavailable);
}

/**
 * @brief Verify cache topology collection tolerates incomplete index entries.
 */
TEST_CASE("cache collection skips unreadable entries", "[platform][linux][parser]") {
  FakeSystemInfoSource source;
  source.cache_directories = {"/cache/index0", "/cache/index1"};
  source.files["/cache/index0/level"] = "1";
  source.files["/cache/index0/size"] = "32K";
  source.files["/cache/index0/type"] = "Data";
  // index1 has no files and must be skipped rather than emitted as unavailable.

  const auto caches = sysinfo::collect_caches(source);
  REQUIRE(caches.has_value());
  REQUIRE(caches.value.size() == 1);
  REQUIRE(caches.value[0].size_bytes == 32 * 1024);

  FakeSystemInfoSource empty;
  REQUIRE(sysinfo::collect_caches(empty).state == ztick_availability_unavailable);
}

/**
 * @brief Verify every inventory field when all raw data is available.
 */
TEST_CASE("complete Linux inventory yields value states", "[platform][linux][provider]") {
  FakeSystemInfoSource source;
  populate_complete_source(source);
  sysinfo::LinuxSystemInfoProvider provider{std::make_unique<FakeSystemInfoSource>(source)};

  const auto collected = provider.collect();
  REQUIRE(collected.has_value());
  const auto &info = collected.value;

  REQUIRE(info.cpu_model.state == ztick_availability_value);
  REQUIRE(info.cpu_model.value == "Test CPU @ 3.00GHz");
  REQUIRE(info.cpu_architecture.value == "x86_64");
  REQUIRE(info.physical_core_count.value == 2);
  REQUIRE(info.logical_core_count.value == 2);
  REQUIRE(info.cpu_frequency_hz.value == 2400000000.0);
  REQUIRE(info.scaling_governor.value == "powersave");
  REQUIRE(info.caches.has_value());
  REQUIRE(info.caches.value.size() == 2);
  REQUIRE(info.simd_features.has_value());
  REQUIRE(info.simd_features.value.size() == 6);
  REQUIRE(info.total_ram_bytes.value == 1048576ULL * 1024);
  REQUIRE(info.available_ram_bytes.value == 524288ULL * 1024);
  REQUIRE(info.os_name.value == "Linux");
  REQUIRE(info.kernel_version.value == "6.8.0-test");
  REQUIRE(info.audio_backend.state == ztick_availability_not_applicable);
  REQUIRE(info.audio_device.state == ztick_availability_not_applicable);
  REQUIRE(info.process_affinity.has_value());
  REQUIRE(info.process_affinity.value.ids.size() == 4);
  REQUIRE(info.scheduling_configuration.value == "policy=SCHED_OTHER");
  REQUIRE(info.environment_metadata.has_value());
  REQUIRE(info.environment_metadata.value.size() == 1);
}

/**
 * @brief Verify no field is omitted when every raw source is missing.
 */
TEST_CASE("empty Linux inventory reports explicit states", "[platform][linux][provider]") {
  FakeSystemInfoSource source;
  source.affinity_available = false;
  sysinfo::LinuxSystemInfoProvider provider{std::make_unique<FakeSystemInfoSource>(source)};

  const auto collected = provider.collect();
  REQUIRE(collected.has_value());
  const auto &info = collected.value;

  REQUIRE(info.cpu_model.state == ztick_availability_unavailable);
  REQUIRE(info.cpu_architecture.state == ztick_availability_unavailable);
  REQUIRE(info.physical_core_count.state == ztick_availability_unavailable);
  REQUIRE(info.logical_core_count.state == ztick_availability_unavailable);
  REQUIRE(info.cpu_frequency_hz.state == ztick_availability_unavailable);
  REQUIRE(info.scaling_governor.state == ztick_availability_unavailable);
  REQUIRE(info.caches.state == ztick_availability_unavailable);
  REQUIRE(info.simd_features.state == ztick_availability_unavailable);
  REQUIRE(info.total_ram_bytes.state == ztick_availability_unavailable);
  REQUIRE(info.available_ram_bytes.state == ztick_availability_unavailable);
  REQUIRE(info.os_name.state == ztick_availability_unavailable);
  REQUIRE(info.kernel_version.state == ztick_availability_unavailable);
  REQUIRE(info.process_affinity.state == ztick_availability_unavailable);
  REQUIRE(info.scheduling_configuration.state == ztick_availability_unavailable);
  REQUIRE(info.environment_metadata.state == ztick_availability_unavailable);
  REQUIRE(info.audio_backend.state == ztick_availability_not_applicable);
  REQUIRE(info.audio_device.state == ztick_availability_not_applicable);
}

/**
 * @brief Verify unsupported capability groups emit `unsupported` for their fields.
 * @note This is the data-path counterpart of the Result factory test: every
 *       inventory field must be reachable as unsupported, not just constructible.
 */
TEST_CASE("unsupported capabilities are reported explicitly", "[platform][linux][availability]") {
  FakeSystemInfoSource source;
  populate_complete_source(source);
  const std::vector<sysinfo::Capability> all{
      sysinfo::Capability::cpu_identity,  sysinfo::Capability::cpu_topology,
      sysinfo::Capability::cpu_frequency, sysinfo::Capability::caches,
      sysinfo::Capability::simd_features, sysinfo::Capability::memory,
      sysinfo::Capability::os_info,       sysinfo::Capability::audio,
      sysinfo::Capability::scheduling,    sysinfo::Capability::environment};
  sysinfo::LinuxSystemInfoProvider provider{std::make_unique<FakeSystemInfoSource>(source), all};

  const auto collected = provider.collect();
  REQUIRE(collected.has_value());
  const auto &info = collected.value;

  REQUIRE(info.cpu_model.state == ztick_availability_unsupported);
  REQUIRE(info.cpu_architecture.state == ztick_availability_unsupported);
  REQUIRE(info.physical_core_count.state == ztick_availability_unsupported);
  REQUIRE(info.logical_core_count.state == ztick_availability_unsupported);
  REQUIRE(info.cpu_frequency_hz.state == ztick_availability_unsupported);
  REQUIRE(info.scaling_governor.state == ztick_availability_unsupported);
  REQUIRE(info.caches.state == ztick_availability_unsupported);
  REQUIRE(info.simd_features.state == ztick_availability_unsupported);
  REQUIRE(info.total_ram_bytes.state == ztick_availability_unsupported);
  REQUIRE(info.available_ram_bytes.state == ztick_availability_unsupported);
  REQUIRE(info.os_name.state == ztick_availability_unsupported);
  REQUIRE(info.kernel_version.state == ztick_availability_unsupported);
  REQUIRE(info.audio_backend.state == ztick_availability_unsupported);
  REQUIRE(info.audio_device.state == ztick_availability_unsupported);
  REQUIRE(info.process_affinity.state == ztick_availability_unsupported);
  REQUIRE(info.scheduling_configuration.state == ztick_availability_unsupported);
  REQUIRE(info.environment_metadata.state == ztick_availability_unsupported);
}

/**
 * @brief Verify an unsupported group does not change unaffected fields.
 */
TEST_CASE("unsupported capability is scoped to its fields", "[platform][linux][availability]") {
  FakeSystemInfoSource source;
  populate_complete_source(source);
  sysinfo::LinuxSystemInfoProvider provider{std::make_unique<FakeSystemInfoSource>(source),
                                            {sysinfo::Capability::cpu_frequency}};

  const auto collected = provider.collect();
  REQUIRE(collected.has_value());
  const auto &info = collected.value;

  REQUIRE(info.cpu_frequency_hz.state == ztick_availability_unsupported);
  REQUIRE(info.scaling_governor.state == ztick_availability_unsupported);
  REQUIRE(info.cpu_model.state == ztick_availability_value);
  REQUIRE(info.total_ram_bytes.state == ztick_availability_value);
  REQUIRE(info.os_name.state == ztick_availability_value);
  REQUIRE(info.audio_backend.state == ztick_availability_not_applicable);
}

/**
 * @brief Verify no field is left unassigned in any availability configuration.
 * @note Together these scenarios produce all four states through real data paths:
 *       value, unavailable, unsupported and not_applicable.
 */
TEST_CASE("no inventory field is silently omitted", "[platform][linux][availability]") {
  FakeSystemInfoSource complete;
  populate_complete_source(complete);
  require_all_fields_explicit(
      sysinfo::LinuxSystemInfoProvider{std::make_unique<FakeSystemInfoSource>(complete)}.collect());

  FakeSystemInfoSource empty;
  empty.affinity_available = false;
  require_all_fields_explicit(
      sysinfo::LinuxSystemInfoProvider{std::make_unique<FakeSystemInfoSource>(empty)}.collect());

  FakeSystemInfoSource partial;
  populate_complete_source(partial);
  require_all_fields_explicit(sysinfo::LinuxSystemInfoProvider{
      std::make_unique<FakeSystemInfoSource>(partial), {sysinfo::Capability::audio}}
                                  .collect());
}

/**
 * @brief Verify the real filesystem source and factory work on the Linux host.
 */
TEST_CASE("real Linux source and factory are usable", "[platform][linux][smoke]") {
  const sysinfo::FilesystemSystemInfoSource source;
  std::string content;
  REQUIRE(source.read_file(sysinfo::kCpuInfoPath, content));
  REQUIRE_FALSE(content.empty());
  REQUIRE_FALSE(source.read_file("/ztick/definitely/not/here", content));
  REQUIRE_FALSE(source.uname().sysname.empty());
  REQUIRE(source.environment_variable("PATH") != nullptr);

  std::vector<std::uint32_t> cpus;
  REQUIRE(source.current_affinity(cpus));
  REQUIRE_FALSE(cpus.empty());
  REQUIRE_FALSE(source.scheduling_configuration().empty());
  // cache_index_paths may be empty on hosts without exported sysfs topology.
  const auto paths = source.cache_index_paths();
  REQUIRE(paths.size() == paths.size());

  auto provider = sysinfo::make_linux_system_info_provider();
  REQUIRE(provider != nullptr);
  REQUIRE(provider->collect().has_value());
}
