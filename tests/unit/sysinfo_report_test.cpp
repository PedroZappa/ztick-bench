// SPDX-License-Identifier: MIT
/**
 * @file sysinfo_report_test.cpp
 * @brief Catch2 unit tests for the ztick-sysinfo report renderer.
 *
 * Expected strings are written as literals so the report format is frozen by
 * the tests rather than derived from the renderer under test.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/platform.hpp>
#include <ztick/tools/sysinfo_report.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/** @brief Short alias for the report renderer namespace. */
namespace tools = ztick::tools;
/** @brief Short alias for the platform namespace. */
namespace platform = ztick::platform;

namespace {

/**
 * @brief Build an unavailable result with a fixed diagnostic reason.
 * @tparam T Result value type.
 * @return An unavailable result carrying `test reason`.
 */
template <typename T> platform::Result<T> unavailable_field() {
  return platform::make_unavailable<T>("test reason");
}

/**
 * @brief Build an inventory whose fields all hold known values.
 * @return Fully populated inventory used to freeze the rendered format.
 */
platform::SystemInfo value_inventory() {
  platform::SystemInfo info;
  info.cpu_model = platform::make_value(std::string{"Test CPU"});
  info.cpu_architecture = platform::make_value(std::string{"x86_64"});
  info.physical_core_count = platform::make_value(std::uint32_t{4});
  info.logical_core_count = platform::make_value(std::uint32_t{8});
  info.cpu_frequency_hz = platform::make_value(2400000000.0);
  info.scaling_governor = platform::make_value(std::string{"schedutil"});
  info.caches = platform::make_value(
      std::vector<platform::CacheInfo>{{1, 32768, "data"}, {2, 1048576, "unified"}});
  info.simd_features = platform::make_value(std::vector<std::string>{"sse", "avx2"});
  info.total_ram_bytes = platform::make_value(std::uint64_t{17179869184});
  info.available_ram_bytes = platform::make_value(std::uint64_t{2147483648});
  info.os_name = platform::make_value(std::string{"Linux"});
  info.kernel_version = platform::make_value(std::string{"6.8.0"});
  info.audio_backend = platform::make_value(std::string{"test-backend"});
  info.audio_device = platform::make_value(std::string{"test-device"});
  info.process_affinity = platform::make_value(platform::CpuSet{{0, 2, 4}});
  info.scheduling_configuration = platform::make_value(std::string{"policy=SCHED_OTHER"});
  info.environment_metadata =
      platform::make_value(std::vector<std::string>{"SESSION=wayland", "DESKTOP=test"});
  return info;
}

/**
 * @brief Count how many lines of a report start with a given field label.
 * @param report Rendered report.
 * @param label Field label to find.
 * @return Number of matching lines.
 */
std::size_t count_lines_with_label(const std::string &report, std::string_view label) {
  const std::string prefix{label};
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = report.find(prefix, position)) != std::string::npos) {
    if (position == 0 || report[position - 1] == '\n') {
      ++count;
    }
    position += prefix.size();
  }
  return count;
}

} // namespace

/**
 * @brief Verify the four declared states and the fallback token.
 */
TEST_CASE("availability states map to stable tokens", "[tools][sysinfo]") {
  REQUIRE(std::string{tools::availability_name(ztick_availability_value)} == "value");
  REQUIRE(std::string{tools::availability_name(ztick_availability_unavailable)} == "unavailable");
  REQUIRE(std::string{tools::availability_name(ztick_availability_unsupported)} == "unsupported");
  REQUIRE(std::string{tools::availability_name(ztick_availability_not_applicable)} ==
          "not_applicable");
  REQUIRE(std::string{tools::availability_name(static_cast<ztick_availability>(99))} == "unknown");
}

/**
 * @brief Verify binary-unit byte rendering and its rounding boundaries.
 */
TEST_CASE("byte counts use binary units", "[tools][sysinfo]") {
  REQUIRE(tools::format_bytes(0) == "0 B");
  REQUIRE(tools::format_bytes(1023) == "1023 B");
  REQUIRE(tools::format_bytes(1024) == "1.00 KiB");
  REQUIRE(tools::format_bytes(1536) == "1.50 KiB");
  REQUIRE(tools::format_bytes(1048576) == "1.00 MiB");
  REQUIRE(tools::format_bytes(1073741824) == "1.00 GiB");
}

/**
 * @brief Verify every value formatter used by the inventory.
 */
TEST_CASE("inventory value types render deterministically", "[tools][sysinfo]") {
  REQUIRE(tools::format_value(std::string{"x86_64"}) == "x86_64");
  REQUIRE(tools::format_value(std::uint32_t{8}) == "8");
  REQUIRE(tools::format_value(std::uint64_t{2048}) == "2.00 KiB");
  REQUIRE(tools::format_value(2400000000.0) == "2400000000 Hz");
  REQUIRE(tools::format_value(platform::CpuSet{{0, 2, 4}}) == "0, 2, 4");
  REQUIRE(tools::format_value(platform::CpuSet{}) == "<default>");
  REQUIRE(tools::format_value(platform::CacheInfo{1, 32768, "data"}) == "L1 32.00 KiB data");
  REQUIRE(tools::format_value(platform::CacheInfo{2, 1048576, ""}) == "L2 1.00 MiB");
  REQUIRE(tools::format_value(
              std::vector<platform::CacheInfo>{{1, 32768, "data"}, {2, 1048576, "unified"}}) ==
          "L1 32.00 KiB data; L2 1.00 MiB unified");
  REQUIRE(tools::format_value(std::vector<platform::CacheInfo>{}) == "<none>");
  REQUIRE(tools::format_value(std::vector<std::string>{"sse", "avx2"}) == "sse, avx2");
  REQUIRE(tools::format_value(std::vector<std::string>{}) == "<none>");
}

/**
 * @brief Verify labelled fields show the state and the matching detail.
 */
TEST_CASE("field lines include state and detail", "[tools][sysinfo]") {
  REQUIRE(tools::format_result("Cores", platform::make_value(std::uint32_t{8})) ==
          "Cores: value (8)");
  REQUIRE(tools::format_result("Model", platform::make_unavailable<std::string>("no cpuinfo")) ==
          "Model: unavailable (no cpuinfo)");
  REQUIRE(tools::format_result("Cache", platform::make_unsupported<std::vector<std::string>>(
                                            "no sysfs")) == "Cache: unsupported (no sysfs)");
  REQUIRE(tools::format_result("Audio", platform::make_not_applicable<std::string>("offline")) ==
          "Audio: not_applicable (offline)");
  REQUIRE(tools::format_result("Model", platform::Result<std::string>{}) ==
          "Model: unavailable (not collected)");
}

/**
 * @brief Verify the complete value report lists every inventory field once.
 */
TEST_CASE("rendered inventory contains every field", "[tools][sysinfo]") {
  constexpr std::array<std::string_view, 17> kLabels{"CPU model",
                                                     "CPU architecture",
                                                     "Physical core count",
                                                     "Logical core count",
                                                     "CPU frequency",
                                                     "Scaling governor",
                                                     "Caches",
                                                     "SIMD features",
                                                     "Total RAM",
                                                     "Available RAM",
                                                     "OS name",
                                                     "Kernel version",
                                                     "Audio backend",
                                                     "Audio device",
                                                     "Process affinity",
                                                     "Scheduling configuration",
                                                     "Environment metadata"};

  const std::string report = tools::render_system_info(value_inventory());
  for (const std::string_view label : kLabels) {
    REQUIRE(count_lines_with_label(report, label) == 1);
  }
  REQUIRE(std::count(report.begin(), report.end(), '\n') == 17);
  REQUIRE(report.back() == '\n');

  REQUIRE(report.find("CPU model: value (Test CPU)") != std::string::npos);
  REQUIRE(report.find("CPU frequency: value (2400000000 Hz)") != std::string::npos);
  REQUIRE(report.find("Total RAM: value (16.00 GiB)") != std::string::npos);
  REQUIRE(report.find("Process affinity: value (0, 2, 4)") != std::string::npos);
  REQUIRE(report.find("Environment metadata: value (SESSION=wayland, DESKTOP=test)") !=
          std::string::npos);
}

/**
 * @brief Verify non-value fields are rendered with their state and reason.
 * @note This proves the tool cannot silently turn an unavailable capability into
 *       an empty or missing line.
 */
TEST_CASE("rendered inventory keeps every non-value state", "[tools][sysinfo]") {
  platform::SystemInfo info;
  info.cpu_model = unavailable_field<std::string>();
  info.cpu_architecture = platform::make_unsupported<std::string>("no uname");
  info.physical_core_count = unavailable_field<std::uint32_t>();
  info.logical_core_count = unavailable_field<std::uint32_t>();
  info.cpu_frequency_hz = unavailable_field<double>();
  info.scaling_governor = unavailable_field<std::string>();
  info.caches = unavailable_field<std::vector<platform::CacheInfo>>();
  info.simd_features = unavailable_field<std::vector<std::string>>();
  info.total_ram_bytes = unavailable_field<std::uint64_t>();
  info.available_ram_bytes = unavailable_field<std::uint64_t>();
  info.os_name = unavailable_field<std::string>();
  info.kernel_version = unavailable_field<std::string>();
  info.audio_backend = platform::make_not_applicable<std::string>("no mode selected");
  info.audio_device = platform::make_not_applicable<std::string>("no mode selected");
  info.process_affinity = unavailable_field<platform::CpuSet>();
  info.scheduling_configuration = unavailable_field<std::string>();
  info.environment_metadata = unavailable_field<std::vector<std::string>>();

  const std::string report = tools::render_system_info(info);
  REQUIRE(std::count(report.begin(), report.end(), '\n') == 17);
  REQUIRE(report.find("CPU model: unavailable (test reason)") != std::string::npos);
  REQUIRE(report.find("CPU architecture: unsupported (no uname)") != std::string::npos);
  REQUIRE(report.find("Audio backend: not_applicable (no mode selected)") != std::string::npos);
  REQUIRE(report.find(": value") == std::string::npos);
}
