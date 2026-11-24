// SPDX-License-Identifier: MIT
/**
 * @file cli.hpp
 * @brief Command dispatch, output formats and stable exit codes for ztick-bench.
 *
 * PROJECT_SPEC §3.3 owns the command set, and BR §12.2 requires CI-friendly exit
 * codes and machine-readable output. This module parses global options, routes
 * them to the documented commands and returns text, structured data plus an exit
 * code instead of writing to streams, so every dispatch path is deterministic and
 * unit-testable. P4.5 implements `list` and the dispatch skeleton; P4.6 adds
 * `run`; P4.7 adds `system`; P4.8 adds `report` and `compare`; P4.9 adds
 * classification; P4.10 adds `plugin list`, `plugin inspect`, a regression exit
 * code and the JSON output envelope (RESULTS_FORMAT §12).
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_CLI_HPP
#define ZTICK_CLI_HPP

#include "ztick/config_value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** @brief Command-line front end for ztick-bench. */
namespace ztick::cli {

/**
 * @brief Stable process exit codes returned by command dispatch.
 * @note Codes are part of the CI contract documented in PROJECT_SPEC §3.3 and
 *       must not be renumbered within v1.0. New codes may only be appended.
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
  /** @brief A comparison classified at least one regression under an explicit flag. */
  regression = 4,
};

/**
 * @brief Render an exit code as its stable token.
 * @param code Exit code to encode.
 * @return `success`, `usage_error`, `unavailable`, `failure`, `regression`, or
 *         `unknown`.
 */
std::string_view exit_code_token(ExitCode code) noexcept;

/** @brief Text or deterministic JSON output selected by the global `--format` option. */
enum class OutputFormat : std::uint8_t {
  /** @brief Human-readable text on stdout and errors on stderr. */
  text = 0,
  /** @brief One machine-readable JSON envelope on stdout; stderr stays empty. */
  json = 1,
};

/**
 * @brief Render an output format as its stable token.
 * @param format Format to encode.
 * @return `text` or `json`.
 */
std::string_view output_format_token(OutputFormat format) noexcept;

/**
 * @brief One built-in profile shown by `list`.
 */
struct ProfileDescription {
  /** @brief Stable profile name. */
  std::string_view name;
  /** @brief Human-readable profile summary. */
  std::string_view description;
};

/**
 * @brief Read the built-in profile descriptions in display order.
 * @return Stable descriptions from BENCHMARK §5.
 * @note The text and JSON forms of `list` both derive from this sequence.
 */
const std::vector<ProfileDescription> &profile_descriptions() noexcept;

/**
 * @brief Global output format option removed from a command line.
 */
struct FormatSelection {
  /** @brief Selected output format; text when `--format` was absent. */
  OutputFormat format{OutputFormat::text};
  /** @brief Original argument views with the format option removed. */
  std::vector<std::string_view> args;
};

/**
 * @brief Extract the global `--format` option from command arguments.
 * @param args Arguments excluding the executable name.
 * @param error Optional receiver for a human-readable usage error.
 * @return Remaining arguments and selected format, or `std::nullopt` when the
 *         option is duplicated, malformed or has an unknown value.
 * @note Accepted forms are `--format text|json` and `--format=text|json`. The
 *       option may appear before or after the command name and is removed before
 *       command-specific parsing.
 */
[[nodiscard]] std::optional<FormatSelection>
select_output_format(const std::vector<std::string_view> &args, std::string *error = nullptr);

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
 *       @ref data carries the command's structured result for JSON output and is
 *       null for commands that have no data-bearing result.
 */
struct CommandResult {
  /** @brief Process exit code to return. */
  ExitCode code{ExitCode::success};
  /** @brief Deterministic text for stdout; empty when there is none. */
  std::string output;
  /** @brief Deterministic text for stderr; empty when there is none. */
  std::string error;
  /** @brief Structured command result used by the JSON output envelope. */
  ConfigValue data{ConfigValue::null()};
};

/**
 * @brief Dispatch one command line.
 * @param args Arguments excluding the executable name; may be empty.
 * @return Output text, structured data and exit code for the selected command.
 * @note `--help` and `--version` are global options and take no extra arguments.
 *       `list` accepts no argument or the single subject `profiles`. `system`
 *       prints the platform inventory with explicit availability states,
 *       `run` delegates to the config-driven execution path, `report` renders a
 *       stored result, and `compare` renders a core-produced comparison. The
 *       `plugin` subcommands inspect metadata without executing adapter code.
 *       Global `--format json` wraps any outcome in the envelope owned by
 *       RESULTS_FORMAT §12.
 */
CommandResult dispatch(const std::vector<std::string_view> &args);

} // namespace ztick::cli

#endif // ZTICK_CLI_HPP
