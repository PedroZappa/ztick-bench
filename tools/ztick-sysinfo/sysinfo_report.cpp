// SPDX-License-Identifier: MIT
/**
 * @file sysinfo_report.cpp
 * @brief Implementation of the ztick-sysinfo text renderer.
 */

#include <ztick/tools/sysinfo_report.hpp>

#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ztick::tools {
namespace {

/** @brief Binary units indexed by the scale applied to a byte count. */
constexpr std::array<const char *, 5> kByteUnits{"B", "KiB", "MiB", "GiB", "TiB"};

/**
 * @brief Append one rendered field to a report, starting a new line if needed.
 * @tparam T Result value type.
 * @param out Report under construction.
 * @param label Human-readable field name.
 * @param result Result carrying the field state.
 */
template <typename T>
void append_line(std::string &out, std::string_view label, const platform::Result<T> &result) {
  if (!out.empty()) {
    out += '\n';
  }
  out += format_result(label, result);
}

} // namespace

const char *availability_name(std::underlying_type_t<ztick_availability> state) {
  switch (state) {
  case ztick_availability_value:
    return "value";
  case ztick_availability_unavailable:
    return "unavailable";
  case ztick_availability_unsupported:
    return "unsupported";
  case ztick_availability_not_applicable:
    return "not_applicable";
  default:
    return "unknown";
  }
}

std::string format_bytes(std::uint64_t bytes) {
  double scaled = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (scaled >= 1024.0 && unit + 1 < kByteUnits.size()) {
    scaled /= 1024.0;
    ++unit;
  }
  if (unit == 0) {
    return std::to_string(bytes) + " " + kByteUnits[unit];
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << scaled << ' ' << kByteUnits[unit];
  return out.str();
}

std::string format_value(const std::string &value) { return value; }

std::string format_value(std::uint32_t value) { return std::to_string(value); }

std::string format_value(std::uint64_t value) { return format_bytes(value); }

std::string format_value(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(0) << value << " Hz";
  return out.str();
}

std::string format_value(const platform::CpuSet &value) {
  std::string out;
  for (std::uint32_t id : value.ids) {
    if (!out.empty()) {
      out += ", ";
    }
    out += std::to_string(id);
  }
  return out.empty() ? std::string{"<default>"} : out;
}

std::string format_value(const platform::CacheInfo &value) {
  std::string out = "L" + std::to_string(value.level) + " " + format_bytes(value.size_bytes);
  if (!value.type.empty()) {
    out += " " + value.type;
  }
  return out;
}

std::string format_value(const std::vector<platform::CacheInfo> &value) {
  std::string out;
  for (const platform::CacheInfo &cache : value) {
    if (!out.empty()) {
      out += "; ";
    }
    out += format_value(cache);
  }
  return out.empty() ? std::string{"<none>"} : out;
}

std::string format_value(const std::vector<std::string> &value) {
  std::string out;
  for (const std::string &name : value) {
    if (!out.empty()) {
      out += ", ";
    }
    out += name;
  }
  return out.empty() ? std::string{"<none>"} : out;
}

std::string render_system_info(const platform::SystemInfo &info) {
  std::string out;
  append_line(out, "CPU model", info.cpu_model);
  append_line(out, "CPU architecture", info.cpu_architecture);
  append_line(out, "Physical core count", info.physical_core_count);
  append_line(out, "Logical core count", info.logical_core_count);
  append_line(out, "CPU frequency", info.cpu_frequency_hz);
  append_line(out, "Scaling governor", info.scaling_governor);
  append_line(out, "Caches", info.caches);
  append_line(out, "SIMD features", info.simd_features);
  append_line(out, "Total RAM", info.total_ram_bytes);
  append_line(out, "Available RAM", info.available_ram_bytes);
  append_line(out, "OS name", info.os_name);
  append_line(out, "Kernel version", info.kernel_version);
  append_line(out, "Audio backend", info.audio_backend);
  append_line(out, "Audio device", info.audio_device);
  append_line(out, "Process affinity", info.process_affinity);
  append_line(out, "Scheduling configuration", info.scheduling_configuration);
  append_line(out, "Environment metadata", info.environment_metadata);
  out += '\n';
  return out;
}

} // namespace ztick::tools
