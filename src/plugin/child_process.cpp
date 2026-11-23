// SPDX-License-Identifier: MIT
/**
 * @file child_process.cpp
 * @brief Portable validation for out-of-process child commands.
 */

#include <ztick/plugin/child_process.hpp>

#include <algorithm>
#include <string>

namespace ztick::plugin {
namespace {

/**
 * @brief Check whether one string contains an embedded NUL byte.
 * @param value String to inspect.
 * @return True when a NUL byte is present before the string's terminator.
 */
auto string_has_nul(const std::string &value) noexcept -> bool {
  return value.find('\0') != std::string::npos;
}

} // namespace

auto child_process_spec_valid(const ChildProcessSpec &spec) noexcept -> bool {
  if (spec.executable.empty() || string_has_nul(spec.executable)) {
    return false;
  }
  return std::none_of(spec.arguments.begin(), spec.arguments.end(), string_has_nul);
}

} // namespace ztick::plugin
