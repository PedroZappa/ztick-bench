// SPDX-License-Identifier: MIT
/**
 * @file app_a_main.cpp
 * @brief Executable entry point for the static App A end-to-end harness.
 *
 * The process prints one stable summary line on success and exits non-zero with
 * a diagnostic when any SDK lifecycle expectation is unmet. It is registered
 * with CTest as the App A acceptance executable.
 */

#include <app_a_harness.hpp>

#include <iostream>

/**
 * @brief Run App A and report its outcome through the process exit status.
 * @return Zero when every lifecycle expectation passed, one otherwise.
 */
auto main() -> int {
  const ztick::example::app_a::HarnessResult result = ztick::example::app_a::run_harness();
  std::cout << ztick::example::app_a::format_summary(result) << '\n';
  if (!result.ok) {
    std::cerr << "app_a failure: " << result.error << '\n';
    return 1;
  }
  return 0;
}
