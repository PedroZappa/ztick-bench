// SPDX-License-Identifier: MIT
/**
 * @file sysinfo_report.hpp
 * @brief Deterministic text rendering of the platform system-inventory.
 *
 * The ztick-sysinfo executable prints the inventory collected by
 * @ref ztick::platform::SystemInfoProvider. Rendering lives in this separate
 * library so the exact output is unit-testable with injected data rather than
 * depending on the host under test. Every field is rendered with its explicit
 * availability state, so a missing capability is never an absent line
 * (PLATFORM_MONITORING §2, §6).
 */

#ifndef ZTICK_SYSINFO_REPORT_HPP
#define ZTICK_SYSINFO_REPORT_HPP

#include <ztick/platform.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/**
 * @brief Text rendering helpers for the ztick-sysinfo developer tool.
 * @note These helpers are private to the tool and are not part of the SDK.
 */
namespace ztick::tools {

/**
 * @brief Map an availability state to its stable report token.
 * @param state Numeric state to render. An integer is used instead of the enum so
 *        malformed C ABI values can be reported without undefined behavior.
 * @return `value`, `unavailable`, `unsupported`, `not_applicable`, or `unknown`
 *         for a value outside the declared enumeration.
 */
const char *availability_name(std::underlying_type_t<ztick_availability> state);

/**
 * @brief Render a byte count using binary units up to TiB.
 * @param bytes Byte count to render.
 * @return Exact bytes below 1 KiB, otherwise a two-decimal amount such as
 *         `1.50 KiB`.
 */
std::string format_bytes(std::uint64_t bytes);

/**
 * @brief Render a text value verbatim.
 * @param value Text to render.
 * @return A copy of @p value.
 */
std::string format_value(const std::string &value);

/**
 * @brief Render a small count such as a core count.
 * @param value Count to render.
 * @return Decimal representation of @p value.
 */
std::string format_value(std::uint32_t value);

/**
 * @brief Render a byte count in its natural unit.
 * @param value Byte count to render.
 * @return Human-readable size from @ref format_bytes.
 */
std::string format_value(std::uint64_t value);

/**
 * @brief Render a frequency in hertz.
 * @param value Frequency in Hz.
 * @return Decimal value followed by ` Hz`.
 */
std::string format_value(double value);

/**
 * @brief Render an explicit CPU set.
 * @param value CPU set to render.
 * @return Comma-separated CPU identifiers, or `<default>` for an empty set.
 */
std::string format_value(const platform::CpuSet &value);

/**
 * @brief Render one cache description.
 * @param value Cache entry to render.
 * @return Level, size and type such as `L1 32.00 KiB data`.
 */
std::string format_value(const platform::CacheInfo &value);

/**
 * @brief Render a cache topology.
 * @param value Cache entries to render.
 * @return Entries separated by `; `, or `<none>` when the value is empty.
 */
std::string format_value(const std::vector<platform::CacheInfo> &value);

/**
 * @brief Render a list of names.
 * @param value Names to render.
 * @return Names separated by `, `, or `<none>` when the value is empty.
 */
std::string format_value(const std::vector<std::string> &value);

/**
 * @brief Render one labelled inventory field with its availability state.
 * @tparam T Result value type, with a @ref format_value overload.
 * @param label Human-readable field name.
 * @param result Result carrying the field value or a non-value state.
 * @return A single line `label: state (detail)`, with the rendered value or the
 *         result reason as @p detail.
 */
template <typename T>
std::string format_result(std::string_view label, const platform::Result<T> &result) {
  std::string line{label};
  line += ": ";
  line += availability_name(result.state);
  if (result.has_value()) {
    line += " (";
    line += format_value(result.value);
    line += ')';
  } else if (result.reason != nullptr) {
    line += " (";
    line += result.reason;
    line += ')';
  }
  return line;
}

/**
 * @brief Render the complete system-inventory as one line per field.
 * @param info Inventory collected by a system-info provider.
 * @return Seventeen newline-terminated lines, each with an explicit state.
 */
std::string render_system_info(const platform::SystemInfo &info);

} // namespace ztick::tools

#endif // ZTICK_SYSINFO_REPORT_HPP
