// SPDX-License-Identifier: MIT
/**
 * @file cli.cpp
 * @brief Deterministic command dispatch, output formats and stable exit codes.
 *
 * Dispatch never writes to a stream directly: it returns the text, structured data
 * and exit code that the executable emits. This keeps every documented path
 * unit-testable and lets each roadmap phase replace a stub without changing
 * argument routing. P4.10 adds the `plugin` commands, the regression exit code and
 * the JSON envelope described by RESULTS_FORMAT §12.
 */

#include <ztick/cli.hpp>

#include <ztick/json.hpp>
#include <ztick/plugin_commands.hpp>
#include <ztick/result_commands.hpp>
#include <ztick/run_command.hpp>
#include <ztick/system_command.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
 * @brief Remove one trailing newline from text stored in a JSON string field.
 * @param text Text to trim.
 * @return Text without a single trailing LF or CRLF.
 */
std::string_view trim_trailing_newline(std::string_view text) noexcept {
  if (!text.empty() && text.back() == '\n') {
    text.remove_suffix(1U);
    if (!text.empty() && text.back() == '\r') {
      text.remove_suffix(1U);
    }
  }
  return text;
}

/**
 * @brief Build the stable label of the dispatched command.
 * @param args Command arguments after global options were removed.
 * @return `plugin list` for plugin subcommands, otherwise the command token.
 */
std::string command_label(const std::vector<std::string_view> &args) {
  if (args.empty()) {
    return "";
  }
  if (args.front() == "plugin" && args.size() > 1U) {
    return "plugin " + std::string(args[1U]);
  }
  return std::string(args.front());
}

/**
 * @brief Detect a request for JSON output in an otherwise malformed command line.
 * @param args Arguments excluding the executable name.
 * @return True when a syntactically complete `--format json` option is present.
 * @note This lets option-parsing errors remain machine-readable without guessing a
 *       format from text, a partial option or an unknown value.
 */
bool json_output_requested(const std::vector<std::string_view> &args) noexcept {
  for (std::size_t index = 0U; index < args.size(); ++index) {
    if (args[index] == "--format=json") {
      return true;
    }
    if (args[index] == "--format" && index + 1U < args.size() && args[index + 1U] == "json") {
      return true;
    }
  }
  return false;
}

/**
 * @brief Build the JSON output envelope for one command result.
 * @param result Command result to encode.
 * @param label Stable command label.
 * @return Object matching the schema in RESULTS_FORMAT §12.
 */
ConfigValue json_envelope(const CommandResult &result, std::string_view label) {
  ConfigValue envelope = ConfigValue::object();
  envelope.set("schema_version", ConfigValue::integer(1));
  envelope.set("command", ConfigValue::string(std::string(label)));
  envelope.set("status", ConfigValue::string(std::string(exit_code_token(result.code))));
  envelope.set("exit_code", ConfigValue::integer(static_cast<std::int64_t>(result.code)));
  envelope.set("data", result.data);
  envelope.set("error", ConfigValue::string(std::string(trim_trailing_newline(result.error))));
  return envelope;
}

/**
 * @brief Encode one command result as the deterministic JSON envelope.
 * @param result Command result to encode.
 * @param label Stable command label.
 * @return Outcome whose stdout is one compact JSON document and whose stderr is empty.
 * @note A serialization failure is itself reported as a JSON envelope with a
 *       `failure` status, so scripts never have to parse human-readable text.
 */
CommandResult as_json(const CommandResult &result, std::string_view label) {
  const JsonWriteResult written = json_write(json_envelope(result, label));
  if (written.ok) {
    return CommandResult{result.code, written.text + '\n', {}};
  }
  ConfigValue fallback = ConfigValue::object();
  fallback.set("schema_version", ConfigValue::integer(1));
  fallback.set("command", ConfigValue::string(std::string(label)));
  fallback.set("status", ConfigValue::string("failure"));
  fallback.set("exit_code", ConfigValue::integer(static_cast<std::int64_t>(ExitCode::failure)));
  fallback.set("data", ConfigValue::null());
  fallback.set("error", ConfigValue::string("json output serialization failed"));
  const JsonWriteResult safe = json_write(fallback);
  return CommandResult{ExitCode::failure, safe.ok ? safe.text + '\n' : std::string("{}\n"), {}};
}

/**
 * @brief Build structured run data from an executed run.
 * @param outcome Run outcome carrying the written document and its path.
 * @return Object with the destination path and the complete result document.
 */
ConfigValue run_data(const RunOutcome &outcome) {
  ConfigValue data = ConfigValue::object();
  data.set("output_path", ConfigValue::string(outcome.output_path.string()));
  data.set("document", outcome.document);
  return data;
}

/**
 * @brief Route one command after global options have been removed.
 * @param args Command arguments excluding the executable name and global options.
 * @param context Optional host services used by a delegated core run session;
 *        null selects the production run context.
 * @return Raw command result before output-format encoding.
 */
CommandResult dispatch_command(const std::vector<std::string_view> &args,
                               const ztick::SessionContext *context) {
  if (args.empty()) {
    return usage_error("ztick-bench: missing command\n");
  }

  const std::string_view command = args.front();
  CommandResult result;

  if (command == "--help" || command == "-h") {
    if (args.size() != 1U) {
      return usage_error("ztick-bench: '--help' takes no arguments\n");
    }
    result = CommandResult{ExitCode::success, std::string(usage_text()), {}};
    result.data = ConfigValue::object();
    result.data.set("text", ConfigValue::string(std::string(usage_text())));
  } else if (command == "--version") {
    if (args.size() != 1U) {
      return usage_error("ztick-bench: '--version' takes no arguments\n");
    }
    result = CommandResult{ExitCode::success, std::string(version_text()), {}};
    result.data = ConfigValue::object();
    result.data.set("text", ConfigValue::string(std::string(version_text())));
  } else if (command == "list") {
    if (args.size() == 1U || (args.size() == 2U && args[1U] == "profiles")) {
      result = CommandResult{ExitCode::success, std::string(profile_list_text()), {}};
      ConfigValue profiles = ConfigValue::array();
      for (const ProfileDescription &profile : profile_descriptions()) {
        ConfigValue entry = ConfigValue::object();
        entry.set("name", ConfigValue::string(std::string(profile.name)));
        entry.set("description", ConfigValue::string(std::string(profile.description)));
        profiles.push_back(std::move(entry));
      }
      result.data = ConfigValue::object();
      result.data.set("profiles", std::move(profiles));
    } else {
      return usage_error("ztick-bench: 'list' accepts no arguments or 'profiles'\n");
    }
  } else if (command == "run") {
    const std::vector<std::string_view> run_args(args.begin() + 1U, args.end());
    const RunOutcome outcome =
        context != nullptr ? execute_run(run_args, *context) : execute_run(run_args);
    result = outcome.command;
    result.data = run_data(outcome);
  } else if (command == "system") {
    const std::vector<std::string_view> system_args(args.begin() + 1U, args.end());
    ConfigValue data = ConfigValue::null();
    result = execute_system(system_args, default_system_info_collector(), &data);
    result.data = std::move(data);
  } else if (command == "report") {
    const std::vector<std::string_view> report_args(args.begin() + 1U, args.end());
    ConfigValue data = ConfigValue::null();
    result = execute_report(report_args, default_result_reader(), &data);
    result.data = std::move(data);
  } else if (command == "compare") {
    const std::vector<std::string_view> compare_args(args.begin() + 1U, args.end());
    ConfigValue data = ConfigValue::null();
    result = execute_compare(compare_args, default_result_reader(), &data);
    result.data = std::move(data);
  } else if (command == "plugin") {
    if (args.size() < 2U) {
      return usage_error("ztick-bench: 'plugin' requires 'list' or 'inspect'\n");
    }
    const std::string_view subcommand = args[1U];
    const std::vector<std::string_view> subcommand_args(args.begin() + 2U, args.end());
    if (subcommand == "list") {
      result = execute_plugin_list(subcommand_args);
    } else if (subcommand == "inspect") {
      result = execute_plugin_inspect(subcommand_args);
    } else {
      return usage_error("ztick-bench: unknown plugin subcommand '" + std::string(subcommand) +
                         "'\n");
    }
  } else {
    return usage_error("ztick-bench: unknown command '" + std::string(command) + "'\n");
  }
  return result;
}

} // namespace detail

} // namespace

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
  case ExitCode::regression:
    return "regression";
  }
  return "unknown";
}

// Consumed by in-tree tests; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
std::string_view output_format_token(OutputFormat format) noexcept {
  switch (format) {
  case OutputFormat::text:
    return "text";
  case OutputFormat::json:
    return "json";
  }
  return "text";
}

const std::vector<ProfileDescription> &profile_descriptions() noexcept {
  static const std::vector<ProfileDescription> profiles{
      {"Quick", "fast development feedback"},
      {"Standard", "balanced everyday benchmarking"},
      {"Comprehensive", "broad coverage and stronger statistical confidence"},
      {"Custom", "user-selected coverage and settings"},
  };
  return profiles;
}

std::optional<FormatSelection> select_output_format(const std::vector<std::string_view> &args,
                                                    std::string *error) {
  FormatSelection selection;
  bool seen = false;
  for (std::size_t index = 0U; index < args.size(); ++index) {
    const std::string_view argument = args[index];
    std::string_view value;
    bool inline_value = false;
    if (argument.starts_with("--format=")) {
      value = argument.substr(9U);
      inline_value = true;
    } else if (argument == "--format") {
      if (index + 1U >= args.size()) {
        if (error != nullptr) {
          *error = "option --format requires a value";
        }
        return std::nullopt;
      }
      value = args[++index];
    } else {
      selection.args.push_back(argument);
      continue;
    }
    if (seen) {
      if (error != nullptr) {
        *error = "option --format was specified more than once";
      }
      return std::nullopt;
    }
    seen = true;
    if (inline_value && value.empty()) {
      if (error != nullptr) {
        *error = "option --format requires a non-empty value";
      }
      return std::nullopt;
    }
    if (value == "text") {
      selection.format = OutputFormat::text;
    } else if (value == "json") {
      selection.format = OutputFormat::json;
    } else {
      if (error != nullptr) {
        *error = "option --format accepts 'text' or 'json'";
      }
      return std::nullopt;
    }
  }
  return selection;
}

std::string_view usage_text() noexcept {
  static constexpr std::string_view text = R"text(ztick-bench - portable DSP and audio benchmarking

Usage:
  ztick-bench <command> [arguments]
  ztick-bench --help
  ztick-bench --version
  ztick-bench [--format text|json] <command> [arguments]

Commands:
  system                 Print the platform inventory and availability states
  list [profiles]        List built-in benchmark profiles
  run <config.json> [options]  Run a benchmark configuration
    --output <path>      Write the result to <path> (default: <config>.result.json)
    --set key=value      Override a dotted config path; repeat for multiple values
  compare <a.json> <b.json> [options]  Compare two stored result documents
    --threshold <percent>  Required relative change (default: 1)
    --alpha <level>        Significance level (default: 0.05)
    --sided one|two        Interval tail selection (default: two)
    --fail-on-regression   Return the regression exit code when a regression is found
  report <run.json>      Render a stored result document
  plugin list [options]  List discovered plugins without executing them
    --path <path>        Add an explicit plugin candidate; repeat for multiple paths
  plugin inspect <path>  Inspect one plugin descriptor without executing it

Exit codes:
  0  success        the command completed as requested
  1  usage_error    unknown command, unsupported option or malformed arguments
  2  unavailable    recognized command whose implementation has not landed
  3  failure        the command ran but hit a runtime or infrastructure failure
  4  regression     a comparison found a regression and --fail-on-regression was set
)text";
  return text;
}

std::string_view profile_list_text() noexcept {
  static const std::string text = []() {
    std::string out = "Built-in profiles:\n";
    for (const ProfileDescription &profile : profile_descriptions()) {
      out += "  ";
      out += profile.name;
      out += ": ";
      out += profile.description;
      out += '\n';
    }
    return out;
  }();
  return text;
}

std::string_view version_text() noexcept {
  static const std::string text = std::string("ztick-bench ") + ZTICK_CLI_VERSION + "\n";
  return text;
}

CommandResult dispatch_with_context(const std::vector<std::string_view> &args,
                                    const ztick::SessionContext &context) {
  std::string format_error;
  const std::optional<FormatSelection> selection = select_output_format(args, &format_error);
  if (!selection.has_value()) {
    const CommandResult result = detail::usage_error("ztick-bench: " + format_error + "\n");
    return detail::json_output_requested(args) ? detail::as_json(result, "") : result;
  }
  const CommandResult result = detail::dispatch_command(selection->args, &context);
  if (selection->format == OutputFormat::json) {
    return detail::as_json(result, detail::command_label(selection->args));
  }
  return result;
}

CommandResult dispatch(const std::vector<std::string_view> &args) {
  std::string format_error;
  const std::optional<FormatSelection> selection = select_output_format(args, &format_error);
  if (!selection.has_value()) {
    const CommandResult result = detail::usage_error("ztick-bench: " + format_error + "\n");
    return detail::json_output_requested(args) ? detail::as_json(result, "") : result;
  }
  const CommandResult result = detail::dispatch_command(selection->args, nullptr);
  if (selection->format == OutputFormat::json) {
    return detail::as_json(result, detail::command_label(selection->args));
  }
  return result;
}

} // namespace ztick::cli
