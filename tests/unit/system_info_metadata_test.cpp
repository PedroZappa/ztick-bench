// SPDX-License-Identifier: MIT
/**
 * @file system_info_metadata_test.cpp
 * @brief Catch2 tests for the platform inventory to results metadata adapter.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/system_info_metadata.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

/**
 * @brief Locate one converted metadata field by name.
 * @param fields Converted fields to search.
 * @param name Field name to find.
 * @return Pointer to the field, or `nullptr` when absent.
 */
const ztick::MetadataField *find_field(const std::vector<ztick::MetadataField> &fields,
                                       std::string_view name) {
  for (const ztick::MetadataField &field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

} // namespace

/**
 * @brief Verify every inventory item is converted with its availability state.
 */
TEST_CASE("system info converts to availability aware metadata", "[platform][metadata]") {
  ztick::platform::SystemInfo info;
  info.cpu_model = ztick::platform::make_value(std::string("Test CPU @ 3.00GHz"));
  info.cpu_architecture = ztick::platform::make_value(std::string("x86_64"));
  info.physical_core_count = ztick::platform::make_value(std::uint32_t{4});
  info.logical_core_count = ztick::platform::make_value(std::uint32_t{8});
  info.cpu_frequency_hz = ztick::platform::make_value(3000000000.0);
  info.scaling_governor = ztick::platform::make_unavailable<std::string>("cpufreq unavailable");
  info.total_ram_bytes = ztick::platform::make_value(std::uint64_t{17179869184ULL});
  info.os_name = ztick::platform::make_value(std::string("Linux"));
  info.audio_backend = ztick::platform::make_not_applicable<std::string>("offline mode");
  info.audio_device = ztick::platform::make_unsupported<std::string>("no audio backend");
  info.simd_features = ztick::platform::make_value(std::vector<std::string>{"sse2", "avx2"});
  ztick::platform::CpuSet affinity;
  affinity.ids = {0U, 2U, 4U};
  info.process_affinity = ztick::platform::make_value(affinity);
  ztick::platform::CacheInfo cache;
  cache.level = 1U;
  cache.size_bytes = 32768U;
  cache.type = "data";
  info.caches = ztick::platform::make_value(std::vector<ztick::platform::CacheInfo>{cache});

  const std::vector<ztick::MetadataField> fields = ztick::platform::system_info_metadata(info);
  REQUIRE(fields.size() == 17U);

  const ztick::MetadataField *model = find_field(fields, "cpu_model");
  REQUIRE(model != nullptr);
  REQUIRE(model->status == ztick::MetadataStatus::value);
  REQUIRE(model->value == "Test CPU @ 3.00GHz");

  const ztick::MetadataField *cores = find_field(fields, "physical_core_count");
  REQUIRE(cores != nullptr);
  REQUIRE(cores->value == "4");

  const ztick::MetadataField *frequency = find_field(fields, "cpu_frequency_hz");
  REQUIRE(frequency != nullptr);
  REQUIRE(frequency->status == ztick::MetadataStatus::value);
  REQUIRE_FALSE(frequency->value.empty());

  const ztick::MetadataField *governor = find_field(fields, "scaling_governor");
  REQUIRE(governor != nullptr);
  REQUIRE(governor->status == ztick::MetadataStatus::unavailable);
  REQUIRE(governor->value.empty());
  REQUIRE(governor->reason == "cpufreq unavailable");

  const ztick::MetadataField *audio = find_field(fields, "audio_backend");
  REQUIRE(audio != nullptr);
  REQUIRE(audio->status == ztick::MetadataStatus::not_applicable);
  REQUIRE(audio->reason == "offline mode");

  const ztick::MetadataField *device = find_field(fields, "audio_device");
  REQUIRE(device != nullptr);
  REQUIRE(device->status == ztick::MetadataStatus::unsupported);

  const ztick::MetadataField *simd = find_field(fields, "simd_features");
  REQUIRE(simd != nullptr);
  REQUIRE(simd->value == "sse2;avx2");

  const ztick::MetadataField *cpu_set = find_field(fields, "process_affinity");
  REQUIRE(cpu_set != nullptr);
  REQUIRE(cpu_set->value == "0,2,4");

  const ztick::MetadataField *caches = find_field(fields, "caches");
  REQUIRE(caches != nullptr);
  REQUIRE(caches->value == "L1:32768:data");
}

/**
 * @brief Verify default unavailable fields keep a non-empty reason.
 */
TEST_CASE("system info default fields are reported unavailable", "[platform][metadata]") {
  const ztick::platform::SystemInfo info;
  const std::vector<ztick::MetadataField> fields = ztick::platform::system_info_metadata(info);

  for (const ztick::MetadataField &field : fields) {
    REQUIRE(field.status == ztick::MetadataStatus::unavailable);
    REQUIRE_FALSE(field.reason.empty());
    REQUIRE(field.value.empty());
  }
}
