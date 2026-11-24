// SPDX-License-Identifier: MIT
/**
 * @file main.cpp
 * @brief Entry point of the ztick-bench command-line executable.
 *
 * The executable is intentionally thin: all argument parsing and exit-code
 * decisions live in @ref ztick::cli so they can be tested without spawning a
 * process. Streams are written only after dispatch returns.
 */

#include <ztick/cli.hpp>

#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

/**
 * @brief Dispatch command-line arguments and emit the resulting streams.
 * @param argc Number of arguments including the executable name.
 * @param argv Argument vector; entries are valid for the lifetime of `main`.
 * @return The stable exit code selected by @ref ztick::cli::dispatch.
 */
int main(int argc, char **argv) {
  std::vector<std::string_view> args;
  args.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
  for (int index = 1; index < argc; ++index) {
    args.emplace_back(argv[index]);
  }

  const ztick::cli::CommandResult result = ztick::cli::dispatch(args);
  if (!result.output.empty()) {
    std::cout << result.output;
  }
  if (!result.error.empty()) {
    std::cerr << result.error;
  }
  return static_cast<int>(result.code);
}
