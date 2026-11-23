// SPDX-License-Identifier: MIT
/**
 * @file system_info_metadata.cpp
 * @brief Implementation of the platform inventory to metadata adapter declared
 *        in @ref system_info_metadata.hpp.
 */

#include "ztick/system_info_metadata.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ztick::platform {

namespace {

/**
 * @brief Render a finite real number without lossy fixed precision.
 * @param value Real value to render.
 * @return General-format text, or `unavailable` for a non-finite value.
 */
std::string render_real(double value) {
  char buffer[64];
  const auto [end, error] =
      std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
  if (error != std::errc{}) {
    return "unavailable";
  }
  return std::string(buffer, static_cast<std::size_t>(end - buffer));
}

/**
 * @brief Join a list of strings with a semicolon.
 * @param values Strings to join.
 * @return Joined text, empty when @p values is empty.
 */
std::string join(const std::vector<std::string> &values) {
  std::string text;
  for (const std::string &value : values) {
    if (!text.empty()) {
      text += ';';
    }
    text += value;
  }
  return text;
}

/**
 * @brief Render an explicit CPU set.
 * @param set CPU identifiers to render.
 * @return `os_default` for an empty set, otherwise comma-separated identifiers.
 */
std::string render_cpu_set(const CpuSet &set) {
  if (set.ids.empty()) {
    return "os_default";
  }
  std::string text;
  for (const std::uint32_t id : set.ids) {
    if (!text.empty()) {
      text += ',';
    }
    text += std::to_string(id);
  }
  return text;
}

/**
 * @brief Render cache topology entries.
 * @param caches Cache descriptions to render.
 * @return Semicolon-separated `L<level>:<bytes>:<type>` entries.
 */
std::string render_caches(const std::vector<CacheInfo> &caches) {
  std::string text;
  for (const CacheInfo &cache : caches) {
    if (!text.empty()) {
      text += ';';
    }
    text += 'L' + std::to_string(cache.level) + ':' + std::to_string(cache.size_bytes) + ':' +
            cache.type;
  }
  return text;
}

/**
 * @brief Convert one platform result into a metadata field.
 * @tparam T Carried value type.
 * @tparam Renderer Callable converting `const T&` to text.
 * @param name Stable field name.
 * @param result Platform result to convert.
 * @param render Value renderer used only for a `value` state.
 * @return Availability-aware metadata field preserving state and reason.
 */
template <typename T, typename Renderer>
MetadataField convert_field(const char *name, const Result<T> &result, Renderer render) {
  const char *reason = result.reason != nullptr ? result.reason : "not collected";
  switch (result.state) {
  case ztick_availability_value:
    return metadata_value(name, render(result.value));
  case ztick_availability_unavailable:
    return metadata_unavailable(name, reason);
  case ztick_availability_unsupported:
    return metadata_unsupported(name, reason);
  case ztick_availability_not_applicable:
    return metadata_not_applicable(name, reason);
  }
  return metadata_unavailable(name, "unknown platform availability state");
}

} // namespace

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
std::vector<MetadataField> system_info_metadata(const SystemInfo &info) {
  const auto text = [](const std::string &value) { return value; };
  const auto unsigned_value = [](std::uint32_t value) { return std::to_string(value); };
  const auto bytes_value = [](std::uint64_t value) { return std::to_string(value); };

  return {
      convert_field("cpu_model", info.cpu_model, text),
      convert_field("cpu_architecture", info.cpu_architecture, text),
      convert_field("physical_core_count", info.physical_core_count, unsigned_value),
      convert_field("logical_core_count", info.logical_core_count, unsigned_value),
      convert_field("cpu_frequency_hz", info.cpu_frequency_hz, render_real),
      convert_field("scaling_governor", info.scaling_governor, text),
      convert_field("caches", info.caches, render_caches),
      convert_field("simd_features", info.simd_features, join),
      convert_field("total_ram_bytes", info.total_ram_bytes, bytes_value),
      convert_field("available_ram_bytes", info.available_ram_bytes, bytes_value),
      convert_field("os_name", info.os_name, text),
      convert_field("kernel_version", info.kernel_version, text),
      convert_field("audio_backend", info.audio_backend, text),
      convert_field("audio_device", info.audio_device, text),
      convert_field("process_affinity", info.process_affinity, render_cpu_set),
      convert_field("scheduling_configuration", info.scheduling_configuration, text),
      convert_field("environment_metadata", info.environment_metadata, join),
  };
}

} // namespace ztick::platform
