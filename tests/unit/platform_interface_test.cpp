// SPDX-License-Identifier: MIT
/**
 * @file platform_interface_test.cpp
 * @brief Catch2 tests for the portable platform abstraction interface.
 *
 * The interface is intentionally declaration-only at this phase. These tests
 * verify that it compiles, that every operation returns an availability-aware
 * result, and that default results are explicit rather than silently omitted.
 * Concrete Linux behavior is covered in P1.11 and P1.12.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/platform.hpp>

#include <cstdint>
#include <string>
#include <type_traits>

/** @brief Short alias for the platform namespace in this test. */
namespace platform = ztick::platform;

/**
 * @brief Verify the interface types are abstract and safely polymorphic.
 * @note Pure virtual declarations without definitions are exempt from behavioral
 *       unit testing; their signatures are asserted instead.
 */
TEST_CASE("platform interfaces are abstract and polymorphic", "[platform][interface]") {
  STATIC_REQUIRE(std::is_same_v<platform::Availability, ztick_availability>);
  STATIC_REQUIRE(std::is_abstract_v<platform::SystemInfoProvider>);
  STATIC_REQUIRE(std::is_abstract_v<platform::RuntimeMonitor>);
  STATIC_REQUIRE(std::is_abstract_v<platform::CpuControl>);
  STATIC_REQUIRE(std::is_abstract_v<platform::WorkerControl>);
  STATIC_REQUIRE(std::is_abstract_v<platform::SchedulingPriority>);
  STATIC_REQUIRE(std::is_abstract_v<platform::PlatformAbstraction>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<platform::SystemInfoProvider>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<platform::RuntimeMonitor>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<platform::CpuControl>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<platform::WorkerControl>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<platform::SchedulingPriority>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<platform::PlatformAbstraction>);
}

/**
 * @brief Verify every operation returns its declared availability-aware result.
 * @note This locks the ARCHITECTURE §7 rule that no platform capability has an
 *       unlabeled return type.
 */
TEST_CASE("platform operations return availability results", "[platform][interface]") {
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::SystemInfoProvider::collect),
                                platform::Result<platform::SystemInfo> (
                                    platform::SystemInfoProvider::*)() const>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::RuntimeMonitor::sample),
                                platform::Result<platform::RuntimeSnapshot> (
                                    platform::RuntimeMonitor::*)(platform::SamplingPhase)>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::CpuControl::physical_core_count),
                                platform::Result<std::uint32_t> (platform::CpuControl::*)() const>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::CpuControl::logical_core_count),
                                platform::Result<std::uint32_t> (platform::CpuControl::*)() const>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(&platform::CpuControl::affinity),
                     platform::Result<platform::CpuSet> (platform::CpuControl::*)() const>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::CpuControl::set_affinity),
                                platform::Result<platform::CpuSet> (platform::CpuControl::*)(
                                    const platform::CpuSet &)>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(&platform::WorkerControl::worker_count),
                     platform::Result<std::uint32_t> (platform::WorkerControl::*)() const>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(&platform::WorkerControl::set_worker_count),
                     platform::Result<std::uint32_t> (platform::WorkerControl::*)(std::uint32_t)>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(&platform::SchedulingPriority::current_priority),
                     platform::Result<std::int32_t> (platform::SchedulingPriority::*)() const>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::SchedulingPriority::set_priority),
                                platform::Result<std::int32_t> (platform::SchedulingPriority::*)(
                                    std::int32_t)>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::PlatformAbstraction::system_info),
                                const platform::SystemInfoProvider &(
                                    platform::PlatformAbstraction::*)() const>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::PlatformAbstraction::runtime_monitor),
                                platform::RuntimeMonitor &(platform::PlatformAbstraction::*)()>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::PlatformAbstraction::cpu_control),
                                platform::CpuControl &(platform::PlatformAbstraction::*)()>);
  STATIC_REQUIRE(std::is_same_v<decltype(&platform::PlatformAbstraction::worker_control),
                                platform::WorkerControl &(platform::PlatformAbstraction::*)()>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(&platform::PlatformAbstraction::scheduling_priority),
                     platform::SchedulingPriority &(platform::PlatformAbstraction::*)()>);
}

/**
 * @brief Verify sampling phases have explicit stable identifiers.
 */
TEST_CASE("sampling phases have frozen codes", "[platform][interface]") {
  STATIC_REQUIRE(static_cast<std::uint32_t>(platform::SamplingPhase::before) == 0);
  STATIC_REQUIRE(static_cast<std::uint32_t>(platform::SamplingPhase::during) == 1);
  STATIC_REQUIRE(static_cast<std::uint32_t>(platform::SamplingPhase::after) == 2);
}

/**
 * @brief Verify default results are unavailable with an explanatory reason.
 * @note This prevents a new inventory field from silently appearing as a value.
 */
TEST_CASE("default platform results are explicitly unavailable", "[platform][availability]") {
  const platform::Result<std::uint32_t> result;
  REQUIRE(result.state == ztick_availability_unavailable);
  REQUIRE(result.value == 0);
  REQUIRE(result.reason != nullptr);
  REQUIRE_FALSE(result.has_value());

  const platform::SystemInfo info;
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
  REQUIRE(info.audio_backend.state == ztick_availability_unavailable);
  REQUIRE(info.audio_device.state == ztick_availability_unavailable);
  REQUIRE(info.process_affinity.state == ztick_availability_unavailable);
  REQUIRE(info.scheduling_configuration.state == ztick_availability_unavailable);
  REQUIRE(info.environment_metadata.state == ztick_availability_unavailable);

  const platform::RuntimeSnapshot snapshot;
  REQUIRE(snapshot.cpu_frequency_hz.state == ztick_availability_unavailable);
  REQUIRE(snapshot.cpu_temperature_celsius.state == ztick_availability_unavailable);
  REQUIRE(snapshot.thermal_throttling.state == ztick_availability_unavailable);
  REQUIRE(snapshot.power_state.state == ztick_availability_unavailable);
  REQUIRE(snapshot.background_cpu_load.state == ztick_availability_unavailable);
}

/**
 * @brief Verify result factories set the correct state, value and reason.
 */
TEST_CASE("platform result factories encode all four states", "[platform][availability]") {
  const auto value = platform::make_value(std::uint32_t{42});
  REQUIRE(value.has_value());
  REQUIRE(value.state == ztick_availability_value);
  REQUIRE(value.value == 42);
  REQUIRE(value.reason == nullptr);

  const auto text = platform::make_value(std::string{"x86_64"});
  REQUIRE(text.has_value());
  REQUIRE(text.value == "x86_64");
  REQUIRE(text.reason == nullptr);

  const auto unavailable = platform::make_unavailable<std::uint32_t>("read failed");
  REQUIRE_FALSE(unavailable.has_value());
  REQUIRE(unavailable.state == ztick_availability_unavailable);
  REQUIRE(unavailable.value == 0);
  REQUIRE(std::string{unavailable.reason} == "read failed");

  const auto unsupported = platform::make_unsupported<std::uint32_t>("no backend");
  REQUIRE_FALSE(unsupported.has_value());
  REQUIRE(unsupported.state == ztick_availability_unsupported);
  REQUIRE(unsupported.value == 0);
  REQUIRE(std::string{unsupported.reason} == "no backend");

  const auto not_applicable = platform::make_not_applicable<std::uint32_t>("offline mode");
  REQUIRE_FALSE(not_applicable.has_value());
  REQUIRE(not_applicable.state == ztick_availability_not_applicable);
  REQUIRE(not_applicable.value == 0);
  REQUIRE(std::string{not_applicable.reason} == "offline mode");
}
