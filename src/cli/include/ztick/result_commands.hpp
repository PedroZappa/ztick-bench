// SPDX-License-Identifier: MIT
/**
 * @file result_commands.hpp
 * @brief Command-line surfaces of `ztick-bench report` and `compare`.
 *
 * PROJECT_SPEC §3.3 owns both commands. These modules read and validate stored
 * result documents, use the core comparison record for `compare`, and render
 * text that preserves availability states, incomplete statuses, raw differences
 * and P4.9 regression decisions. File reads are injectable so every success and
 * error path is unit-testable. This header is private to ztick-bench and is not
 * installed.
 */

#ifndef ZTICK_RESULT_COMMANDS_HPP
#define ZTICK_RESULT_COMMANDS_HPP

#include "ztick/cli.hpp"
#include "ztick/comparison.hpp"
#include "ztick/config_value.hpp"
#include "ztick/result_document.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** @brief Command-line front end for ztick-bench. */
namespace ztick::cli {

/**
 * @brief Read and validate one stored result document.
 * @note The production reader calls @ref ztick::read_result_document. Tests may
 *       inject a deterministic reader without creating files.
 */
using ResultReader = std::function<ResultParseResult(const std::filesystem::path &)>;

/**
 * @brief Supply the production result-document reader.
 * @return Reader that opens, parses and validates a file through `ztick_results`.
 */
ResultReader default_result_reader();

/**
 * @brief Render a human-readable report from a stored result document.
 * @param document Validated result document to render.
 * @return Deterministic report text ending with a newline.
 * @note The session status is derived from run evidence rather than trusting a
 *       stale stored token; the stored token is printed separately. Metadata,
 *       metrics, warnings and failures are retained with explicit states.
 */
std::string render_result_report(const ConfigValue &document);

/**
 * @brief Render a core-produced comparison record.
 * @param comparison Comparison record from a core comparison entry point.
 * @return Deterministic comparison text ending with a newline.
 * @note Raw differences, retained samples, threshold decisions and significance
 *       outcomes are displayed with explicit tokens; absent P4.9 fields are
 *       rendered as not-applicable rather than omitted.
 */
std::string render_result_comparison(const ConfigValue &comparison);

/** @brief Parsed arguments for the `compare` command. */
struct CompareInvocation {
  /** @brief Path of the baseline result document. */
  std::filesystem::path baseline;
  /** @brief Path of the candidate result document. */
  std::filesystem::path candidate;
  /** @brief Threshold and significance policy passed to the core. */
  ztick::ComparisonPolicy policy;
  /** @brief Return the regression exit code when the comparison is a regression. */
  bool fail_on_regression{false};
};

/**
 * @brief Parse `compare` positional paths and policy options.
 * @param args Arguments after the command name.
 * @param error Optional receiver for a human-readable usage error.
 * @return Parsed invocation, or `std::nullopt` when arguments are malformed.
 * @note Accepted forms are `--threshold <percent>`, `--alpha <level>`,
 *       `--sided one|two`, with or without `=`, and the flag
 *       `--fail-on-regression`. Exactly two positional paths are required;
 *       options may appear in any order and duplicates are usage errors.
 */
[[nodiscard]] std::optional<CompareInvocation>
parse_compare_arguments(const std::vector<std::string_view> &args, std::string *error = nullptr);

/**
 * @brief Execute `report` with an injected result reader.
 * @param args Arguments after the command name; exactly one result path.
 * @param reader Reader that parses and validates result files.
 * @return Rendered report on success, usage error for wrong arguments, or a
 *         runtime failure when the document cannot be read or validated.
 */
CommandResult execute_report(const std::vector<std::string_view> &args, const ResultReader &reader);

/**
 * @brief Execute `report` and expose its structured evidence.
 * @param args Arguments after the command name; exactly one result path.
 * @param reader Reader that parses and validates result files.
 * @param data Optional receiver for the stored document, the derived status and
 *        the stored status token.
 * @return Outcome of the report command.
 */
CommandResult execute_report(const std::vector<std::string_view> &args, const ResultReader &reader,
                             ConfigValue *data);

/**
 * @brief Execute `report` with the production result reader.
 * @param args Arguments after the command name; exactly one result path.
 * @return Outcome of the report command.
 */
CommandResult execute_report(const std::vector<std::string_view> &args);

/**
 * @brief Execute `compare` with an injected result reader.
 * @param args Baseline and candidate paths plus optional policy options.
 * @param reader Reader that parses and validates result files.
 * @return Rendered classified comparison on success, usage error for malformed
 *         arguments, or a runtime failure when a document or comparison fails.
 */
CommandResult execute_compare(const std::vector<std::string_view> &args,
                              const ResultReader &reader);

/**
 * @brief Execute `compare` and expose its structured comparison record.
 * @param args Baseline and candidate paths plus optional policy options.
 * @param reader Reader that parses and validates result files.
 * @param data Optional receiver for the comparison record and the regression flag.
 * @return Outcome of the comparison command, including the regression exit code
 *         when requested.
 */
CommandResult execute_compare(const std::vector<std::string_view> &args, const ResultReader &reader,
                              ConfigValue *data);

/**
 * @brief Execute `compare` with the production result reader.
 * @param args Arguments after the command name; baseline then candidate path.
 * @return Outcome of the comparison command.
 */
CommandResult execute_compare(const std::vector<std::string_view> &args);

} // namespace ztick::cli

#endif // ZTICK_RESULT_COMMANDS_HPP
