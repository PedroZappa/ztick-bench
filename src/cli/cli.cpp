// SPDX-License-Identifier: MIT
/**
 * @file cli.cpp
 * @brief Deterministic command dispatch and stable exit codes for ztick-bench.
 *
 * Dispatch never writes to a stream directly: it returns the text and exit code
 * that the executable emits. This keeps every documented path unit-testable and
 * lets later command phases replace a stub without changing argument routing.
 */

#include <ztick/cli.hpp>

#include <string>
#include <string_view>
#include <utility>

#ifndef ZTICK_CLI_VERSION
/** @brief Fallback tool version when CMake has not supplied one. */
#define ZTICK_CLI_VERSION "unknown"
#endif

namespace ztick::cli {

/** @brief Internal state and helpers for deterministic command dispatch. */
namespace {

/** @brief Internal helpers that build deterministic dispatch outcomes. */
namespace detail {

/**
 * @brief Build a usage-error outcome.
 * @param message Error text for stderr.
 * @return Usage error with the full help text on stdout.
 */
CommandResult usage_error(std::string message) {
  return CommandResult{ExitCode::usage_error, std::string(usage_text()), std::move(message)};
}

/**
 * @brief Build an unavailable-command outcome.
 * @param command Command name to report.
 * @return Unavailable outcome with an explanatory stderr message.
 */
CommandResult unavailable_command(std::string_view command) {
  return CommandResult{ExitCode::unavailable,
                       {},
                       "ztick-bench: '" + std::string(command) +
                           "' is not available in this build\n"};
}

} // namespace detail

} // namespace

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
std::string_view exit_code_token(ExitCode code) noexcept {
  switch (code) {
  case ExitCode::success:
    return "success";
  case ExitCode::usage_error:
    return "usage_error";
  case ExitCode::unavailable:
    return "unavailable";
  case ExitCode::failure:
    return "failure";
  }
  return "unknown";
}

std::string_view usage_text() noexcept {
  static constexpr std::string_view text = R"text(ztick-bench - portable DSP and audio benchmarking

Usage:
  ztick-bench <command> [arguments]
  ztick-bench --help
  ztick-bench --version

Commands:
  system                 Print the platform inventory and availability states
  list [profiles]        List built-in benchmark profiles
  run <config.json>      Run a benchmark configuration
  compare <a.json> <b.json>  Compare two stored result documents
  report <run.json>      Render a stored result document
  plugin list            List discovered plugins
  plugin inspect <path>  Inspect plugin metadata without loading it

Exit codes:
  0  success        the command completed as requested
  1  usage_error    unknown command, unsupported option or malformed arguments
  2  unavailable    recognized command whose implementation has not landed
  3  failure        the command ran but hit a runtime or infrastructure failure
)text";
  return text;
}

std::string_view profile_list_text() noexcept {
  static constexpr std::string_view text = R"text(Built-in profiles:
  Quick: fast development feedback
  Standard: balanced everyday benchmarking
  Comprehensive: broad coverage and stronger statistical confidence
  Custom: user-selected coverage and settings
)text";
  return text;
}

std::string_view version_text() noexcept {
  static const std::string text = std::string("ztick-bench ") + ZTICK_CLI_VERSION + "\n";
  return text;
}

CommandResult dispatch(const std::vector<std::string_view> &args) {
  if (args.empty()) {
    return detail::usage_error("ztick-bench: missing command\n");
  }

  const std::string_view command = args.front();
  if (command == "--help" || command == "-h") {
    if (args.size() != 1U) {
      return detail::usage_error("ztick-bench: '--help' takes no arguments\n");
    }
    return CommandResult{ExitCode::success, std::string(usage_text()), {}};
  }
  if (command == "--version") {
    if (args.size() != 1U) {
      return detail::usage_error("ztick-bench: '--version' takes no arguments\n");
    }
    return CommandResult{ExitCode::success, std::string(version_text()), {}};
  }
  if (command == "list") {
    if (args.size() == 1U || (args.size() == 2U && args[1] == "profiles")) {
      return CommandResult{ExitCode::success, std::string(profile_list_text()), {}};
    }
    return detail::usage_error("ztick-bench: 'list' accepts no arguments or 'profiles'\n");
  }
  if (command == "system" || command == "run" || command == "compare" || command == "report") {
    return detail::unavailable_command(command);
  }
  if (command == "plugin") {
    if (args.size() < 2U) {
      return detail::usage_error("ztick-bench: 'plugin' requires 'list' or 'inspect'\n");
    }
    const std::string_view subcommand = args[1];
    if (subcommand != "list" && subcommand != "inspect") {
      return detail::usage_error("ztick-bench: unknown plugin subcommand '" +
                                 std::string(subcommand) + "'\n");
    }
    return detail::unavailable_command("plugin " + std::string(subcommand));
  }
  return detail::usage_error("ztick-bench: unknown command '" + std::string(command) + "'\n");
}

} // namespace ztick::cli
