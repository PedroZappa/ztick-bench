// SPDX-License-Identifier: MIT
/**
 * @file cli.hpp
 * @brief Command dispatch and stable exit codes for the ztick-bench executable.
 *
 * PROJECT_SPEC §3.3 owns the command set, and BR §12.2 requires CI-friendly exit
 * codes. This module parses arguments, routes them to the documented commands
 * and returns text plus an exit code instead of writing to streams, so every
 * dispatch path is deterministic and unit-testable. P4.5 implements `list` and
 * the dispatch skeleton; P4.6 implements `run`. Remaining commands report
 * `unavailable` until their roadmap tasks land. This header is private to
 * ztick-bench and is not installed.
 */

#ifndef ZTICK_CLI_HPP
#define ZTICK_CLI_HPP

#include <string>
#include <string_view>
#include <vector>

/** @brief Command-line front end for ztick-bench. */
namespace ztick::cli {

/**
 * @brief Stable process exit codes returned by command dispatch.
 * @note Codes are part of the CI contract documented in PROJECT_SPEC §3.3 and
 *       must not be renumbered within v1.0.
 */
enum class ExitCode : int {
  /** @brief The command completed as requested. */
  success = 0,
  /** @brief Unknown command, unsupported option or malformed arguments. */
  usage_error = 1,
  /** @brief Recognized command whose implementation has not landed yet. */
  unavailable = 2,
  /** @brief The command ran but hit a runtime or infrastructure failure. */
  failure = 3,
};

/**
 * @brief Render an exit code as its stable token.
 * @param code Exit code to encode.
 * @return `success`, `usage_error`, `unavailable`, `failure`, or `unknown`.
 */
std::string_view exit_code_token(ExitCode code) noexcept;

/**
 * @brief Read the full help text.
 * @return Stable usage text ending with a newline.
 */
std::string_view usage_text() noexcept;

/**
 * @brief Read the built-in profile list.
 * @return Stable text listing the four profiles from BENCHMARK §5.
 */
std::string_view profile_list_text() noexcept;

/**
 * @brief Read the tool version line captured at configure time.
 * @return Stable text of the form `ztick-bench <version>\n`.
 */
std::string_view version_text() noexcept;

/**
 * @brief Result of dispatching one command line.
 * @note Output and error text are kept separate so the executable can write
 *       them to stdout and stderr respectively without losing information.
 */
struct CommandResult {
  /** @brief Process exit code to return. */
  ExitCode code{ExitCode::success};
  /** @brief Deterministic text for stdout; empty when there is none. */
  std::string output;
  /** @brief Deterministic text for stderr; empty when there is none. */
  std::string error;
};

/**
 * @brief Dispatch one command line.
 * @param args Arguments excluding the executable name; may be empty.
 * @return Output text and exit code for the selected command.
 * @note `--help` and `--version` are global options and take no extra arguments.
 *       `list` accepts no argument or the single subject `profiles`. `run`
 *       delegates to the config-driven execution path. Commands with a later
 *       roadmap task return @ref ExitCode::unavailable rather than being silently
 *       ignored.
 */
CommandResult dispatch(const std::vector<std::string_view> &args);

} // namespace ztick::cli

#endif // ZTICK_CLI_HPP
