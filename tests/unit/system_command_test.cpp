// SPDX-License-Identifier: MIT
/**
 * @file system_command_test.cpp
 * @brief Catch2 tests for the `system` command and availability-state rendering.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/cli.hpp>
#include <ztick/metadata.hpp>
#include <ztick/platform.hpp>
#include <ztick/system_command.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ztick::MetadataField;
using ztick::cli::ExitCode;
using ztick::cli::SystemInfoCollector;

/**
 * @brief Require that a text contains a substring.
 * @param text Text to search.
 * @param fragment Expected substring.
 */
void require_contains(const std::string &text, std::string_view fragment) {
  REQUIRE(text.find(fragment) != std::string::npos);
}

/**
 * @brief Build a deterministic inventory containing all four availability states.
 * @return Inventory with explicit value, unavailable, unsupported and
 *         not_applicable fields.
 */
ztick::platform::SystemInfo make_inventory() {
  namespace platform = ztick::platform;

  platform::SystemInfo info;
  info.cpu_model = platform::make_value<std::string>("Test CPU");
  info.cpu_architecture = platform::make_value<std::string>("test64");
  info.physical_core_count = platform::make_value<std::uint32_t>(4U);
  info.logical_core_count = platform::make_value<std::uint32_t>(8U);
  info.cpu_frequency_hz = platform::make_unavailable<double>("frequency source offline");
  info.scaling_governor = platform::make_value<std::string>("performance");
  info.caches = platform::make_value<std::vector<platform::CacheInfo>>({{1U, 32768U, "data"}});
  info.simd_features = platform::make_value<std::vector<std::string>>({"SSE2", "AVX2"});
  info.total_ram_bytes = platform::make_value<std::uint64_t>(17179869184ULL);
  info.available_ram_bytes = platform::make_value<std::uint64_t>(8589934592ULL);
  info.os_name = platform::make_value<std::string>("TestOS");
  info.kernel_version = platform::make_unsupported<std::string>("uname disabled");
  info.audio_backend = platform::make_not_applicable<std::string>("offline benchmark mode");
  info.audio_device = platform::make_not_applicable<std::string>("offline benchmark mode");
  info.process_affinity =
      platform::make_value<platform::CpuSet>(platform::CpuSet{{0U, 1U, 2U, 3U}});
  info.scheduling_configuration = platform::make_value<std::string>("SCHED_OTHER");
  info.environment_metadata = platform::make_value<std::vector<std::string>>({"ZTICK_TEST=1"});
  return info;
}

} // namespace

/**
 * @brief Verify metadata rendering prints every state and keeps its detail.
 */
TEST_CASE("system metadata rendering exposes all availability states", "[cli][system]") {
  const std::vector<MetadataField> fields{
      ztick::metadata_value("present", "alpha"), ztick::metadata_unavailable("missing", "not read"),
      ztick::metadata_unsupported("absent", "no backend"),
      ztick::metadata_not_applicable("irrelevant", "offline mode")};

  const std::string text = ztick::cli::render_system_metadata(fields);
  REQUIRE(text.find("ztick-bench: system information (availability states)\n") == 0U);
  require_contains(text, "present: value (alpha)\n");
  require_contains(text, "missing: unavailable (not read)\n");
  require_contains(text, "absent: unsupported (no backend)\n");
  require_contains(text, "irrelevant: not_applicable (offline mode)\n");
}

/**
 * @brief Verify an injected inventory is rendered without losing field states.
 */
TEST_CASE("system command prints the platform inventory", "[cli][system]") {
  const auto inventory = make_inventory();
  bool called = false;
  const SystemInfoCollector collector = [&inventory, &called]() {
    called = true;
    return ztick::platform::make_value(inventory);
  };

  const ztick::cli::CommandResult result = ztick::cli::execute_system({}, collector);
  REQUIRE(result.code == ExitCode::success);
  REQUIRE(result.error.empty());
  REQUIRE(called);
  require_contains(result.output, "cpu_model: value (Test CPU)\n");
  require_contains(result.output, "logical_core_count: value (8)\n");
  require_contains(result.output, "cpu_frequency_hz: unavailable (frequency source offline)\n");
  require_contains(result.output, "kernel_version: unsupported (uname disabled)\n");
  require_contains(result.output, "audio_backend: not_applicable (offline benchmark mode)\n");
}

/**
 * @brief Verify argument errors reject input before collecting inventory.
 */
TEST_CASE("system command rejects arguments", "[cli][system]") {
  bool called = false;
  const SystemInfoCollector collector = [&called]() {
    called = true;
    return ztick::platform::make_value(make_inventory());
  };

  const auto result = ztick::cli::execute_system({"verbose"}, collector);
  REQUIRE(result.code == ExitCode::usage_error);
  REQUIRE(result.output.empty());
  require_contains(result.error, "'system' accepts no arguments");
  REQUIRE_FALSE(called);
}

/**
 * @brief Verify a missing collector is a runtime failure, not an omitted report.
 */
TEST_CASE("system command requires a collector", "[cli][system]") {
  const SystemInfoCollector missing;
  const auto result = ztick::cli::execute_system({}, missing);
  REQUIRE(result.code == ExitCode::failure);
  REQUIRE(result.output.empty());
  require_contains(result.error, "collector is missing");
}

/**
 * @brief Verify whole-inventory failures keep an explicit availability state.
 */
TEST_CASE("system command reports collector failure states", "[cli][system]") {
  const std::vector<std::pair<ztick_availability, std::string_view>> cases{
      {ztick_availability_unavailable, "unavailable"},
      {ztick_availability_unsupported, "unsupported"},
      {ztick_availability_not_applicable, "not_applicable"}};

  for (const auto &entry : cases) {
    ztick::platform::Result<ztick::platform::SystemInfo> collected;
    collected.state = entry.first;
    collected.reason = "provider offline";
    const auto result = ztick::cli::execute_system({}, [collected]() { return collected; });
    REQUIRE(result.code == ExitCode::failure);
    require_contains(result.output,
                     "system_inventory: " + std::string(entry.second) + " (provider offline)\n");
    require_contains(result.error, "system inventory " + std::string(entry.second));
  }
}

/**
 * @brief Verify the production collector uses the platform abstraction.
 */
TEST_CASE("system command supplies a live platform collector", "[cli][system]") {
  const auto collector = ztick::cli::default_system_info_collector();
  REQUIRE(static_cast<bool>(collector));

  const auto collected = collector();
  REQUIRE(collected.has_value());
  REQUIRE(collected.reason == nullptr);
}
