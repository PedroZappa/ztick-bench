// SPDX-License-Identifier: MIT
/**
 * @file app_b_main.cpp
 * @brief Executable entry point for the dynamic App B end-to-end harness.
 *
 * The normal modes run the full host preflight and engine workload. The
 * `engine-child` mode is launched only by the authorized host runner: it loads the
 * same shared library, runs the lifecycle and publishes the final status through the
 * out-of-process IPC abstraction.
 */

#include <app_b_contract.hpp>
#include <app_b_harness.hpp>

#include <ztick/plugin/child_process.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace {

/**
 * @brief Find the value of one named command-line option.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument array.
 * @param option Option name to find, for example `--module`.
 * @return The following argument, or an empty string when absent.
 */
auto option_value(int argc, char **argv, std::string_view option) -> std::string {
  for (int index = 2; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == option) {
      return argv[index + 1];
    }
  }
  return {};
}

} // namespace

/**
 * @brief Run App B and report its outcome through the process exit status.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument array.
 * @return Zero when the requested mode succeeded, one for harness failure, or two for
 *         invalid command-line usage.
 */
auto main(int argc, char **argv) -> int {
  if (argc < 2) {
    std::cerr << "usage: ztick_test_app_b <in_process|out_of_process|engine-child> "
                 "--module <path> [--child-executable <path>]\n";
    return 2;
  }

  const std::string_view mode{argv[1]};
  const std::string module_path = option_value(argc, argv, "--module");
  if (module_path.empty()) {
    std::cerr << "app_b failure: --module is required\n";
    return 2;
  }

  if (mode == ztick::example::app_b::kChildModeArgument) {
    const std::int32_t status = ztick::example::app_b::run_child_engine(module_path.c_str());
    if (!ztick::plugin::write_child_status(status)) {
      return 2;
    }
    return status == ztick_status_success ? 0 : 1;
  }

  ztick::example::app_b::HarnessIsolation isolation =
      ztick::example::app_b::HarnessIsolation::in_process;
  if (mode == "out_of_process") {
    isolation = ztick::example::app_b::HarnessIsolation::out_of_process;
  } else if (mode != "in_process") {
    std::cerr << "app_b failure: unknown mode " << mode << '\n';
    return 2;
  }

  const std::string child_executable = option_value(argc, argv, "--child-executable");
  const ztick::example::app_b::HarnessResult result = ztick::example::app_b::run_harness(
      module_path.c_str(), isolation,
      child_executable.empty() ? nullptr : child_executable.c_str());
  std::cout << ztick::example::app_b::format_summary(result) << '\n';
  if (!result.ok) {
    std::cerr << "app_b failure: " << result.error << '\n';
    return 1;
  }
  return 0;
}
