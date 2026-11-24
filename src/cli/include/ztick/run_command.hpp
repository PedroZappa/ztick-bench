// SPDX-License-Identifier: MIT
/**
 * @file run_command.hpp
 * @brief Command-line surface of the `ztick-bench run` command.
 *
 * PROJECT_SPEC §3.3 owns the command set and BR §12.2 requires config files and
 * command-line overrides. This module parses `run` arguments, applies overrides
 * to a portable config without partial mutation, delegates execution to the core
 * session orchestrator and writes the resulting document atomically. All
 * host-dependent inputs are injectable so every branch is unit-testable.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_RUN_COMMAND_HPP
#define ZTICK_RUN_COMMAND_HPP

#include "ztick/cli.hpp"
#include "ztick/config_value.hpp"
#include "ztick/run_session.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** @brief Command-line front end for ztick-bench. */
namespace ztick::cli {

/**
 * @brief Host services supplied to the core session orchestrator.
 * @note The default context uses a steady clock, the live platform inventory and
 *       timestamp-derived identifiers. Tests inject deterministic services.
 */
using RunContext = ztick::SessionContext;

/**
 * @brief Build the production run context.
 * @return Context using the steady clock, live inventory and generated identifiers.
 */
RunContext default_run_context();

/**
 * @brief Parsed arguments of one `run` invocation.
 */
struct RunInvocation {
  /** @brief Portable config document to read. */
  std::filesystem::path config_path;
  /** @brief Destination result document; never equal to @ref config_path. */
  std::filesystem::path output_path;
  /** @brief Dotted-path assignments applied in command-line order. */
  std::vector<std::string> overrides;
};

/**
 * @brief Derive the default result path from a config path.
 * @param config_path Path to the config document.
 * @return The same path with a `.result.json` extension.
 */
std::filesystem::path default_result_path(const std::filesystem::path &config_path);

/**
 * @brief Parse `run` arguments after the command token.
 * @param args Arguments excluding `run`; the first must be the config path.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Parsed invocation, or `std::nullopt` on malformed arguments.
 * @note Accepted options are `--output <path>`, `--output=<path>`, repeated
 *       `--set key=value` and `--set=key=value`. The config path is positional
 *       and required; a result path identical to it is rejected.
 */
std::optional<RunInvocation> parse_run_arguments(const std::vector<std::string_view> &args,
                                                 std::string *error = nullptr);

/**
 * @brief Apply dotted-path scalar overrides to a config document.
 * @param document Config document to update.
 * @param overrides Assignments in `path=value` form, applied in order.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when every override was applied; @p document is unchanged on failure.
 * @note Intermediate path segments must name existing objects; the final segment
 *       may be a new member of an existing object. JSON-looking values are parsed
 *       as JSON, while plain text uses the existing member type, or is inferred
 *       from the text for a new member.
 */
bool apply_config_overrides(ConfigValue &document, const std::vector<std::string> &overrides,
                            std::string *error = nullptr);

/**
 * @brief Outcome of executing one `run` invocation.
 */
struct RunOutcome {
  /** @brief Exit code, output and error for the CLI dispatch layer. */
  CommandResult command;
  /** @brief Written result document, or null when no result was produced. */
  ConfigValue document{ConfigValue::null()};
  /** @brief Destination path, empty when no write was attempted. */
  std::filesystem::path output_path;
};

/**
 * @brief Execute one `run` invocation with injected host services.
 * @param args Arguments excluding `run`.
 * @param context Injectable clock and host services.
 * @return CLI outcome plus the written document when available.
 * @note Config and override problems report usage errors; runtime and write
 *       failures report failure. A validation failure writes an incomplete
 *       result and reports failure so completed measurements are preserved (BR §14).
 */
RunOutcome execute_run(const std::vector<std::string_view> &args, const RunContext &context);

/**
 * @brief Execute one `run` invocation with production host services.
 * @param args Arguments excluding `run`.
 * @return CLI outcome plus the written document when available.
 */
RunOutcome execute_run(const std::vector<std::string_view> &args);

} // namespace ztick::cli

#endif // ZTICK_RUN_COMMAND_HPP
