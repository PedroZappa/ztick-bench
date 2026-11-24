// SPDX-License-Identifier: MIT
/**
 * @file cli_test.cpp
 * @brief Catch2 tests for CLI dispatch, help text and stable exit codes.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/cli.hpp>
#include <ztick/json.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ztick::cli::CommandResult;
using ztick::cli::ExitCode;

/**
 * @brief Check that text contains a stable fragment.
 * @param text Text to search.
 * @param fragment Fragment expected inside @p text.
 */
void require_contains(std::string_view text, std::string_view fragment) {
  REQUIRE(text.find(fragment) != std::string_view::npos);
}

/**
 * @brief Build a dispatch argument list from string literals.
 * @param args Argument views to copy.
 * @return Vector accepted by @ref ztick::cli::dispatch.
 */
std::vector<std::string_view> arguments(std::initializer_list<std::string_view> args) {
  return std::vector<std::string_view>(args.begin(), args.end());
}

/**
 * @brief Parse one JSON output envelope emitted by dispatch.
 * @param result Dispatch result whose stdout should hold the envelope.
 * @return Parsed JSON value.
 */
ztick::ConfigValue parse_envelope(const CommandResult &result) {
  const ztick::JsonReadResult read = ztick::json_read(result.output);
  REQUIRE(read.ok);
  REQUIRE(read.error.empty());
  return read.value;
}

/**
 * @brief Read one string member of a JSON value.
 * @param value Object to inspect.
 * @param key Member name.
 * @return Member text, or an empty string when absent or not a string.
 */
std::string string_field(const ztick::ConfigValue &value, std::string_view key) {
  const ztick::ConfigValue *field = value.find(key);
  if (field == nullptr || field->type() != ztick::ConfigValueType::string) {
    return {};
  }
  return field->as_string().value();
}

} // namespace

/**
 * @brief Verify every exit code has a stable token, including unknown values.
 */
TEST_CASE("CLI exit codes have stable tokens", "[cli][results]") {
  REQUIRE(ztick::cli::exit_code_token(ExitCode::success) == "success");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::usage_error) == "usage_error");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::unavailable) == "unavailable");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::failure) == "failure");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::regression) == "regression");
  REQUIRE(ztick::cli::exit_code_token(static_cast<ExitCode>(99)) == "unknown");

  REQUIRE(static_cast<int>(ExitCode::success) == 0);
  REQUIRE(static_cast<int>(ExitCode::usage_error) == 1);
  REQUIRE(static_cast<int>(ExitCode::unavailable) == 2);
  REQUIRE(static_cast<int>(ExitCode::failure) == 3);
  REQUIRE(static_cast<int>(ExitCode::regression) == 4);
}

/**
 * @brief Verify the static help and profile text expose the documented surface.
 */
TEST_CASE("CLI help and list text document the command set", "[cli][results]") {
  const std::string_view usage = ztick::cli::usage_text();
  require_contains(usage, "Usage:");
  require_contains(usage, "system");
  require_contains(usage, "list [profiles]");
  require_contains(usage, "run <config.json>");
  require_contains(usage, "--output <path>");
  require_contains(usage, "--set key=value");
  require_contains(usage, "--format text|json");
  require_contains(usage, "compare <a.json> <b.json>");
  require_contains(usage, "--fail-on-regression");
  require_contains(usage, "report <run.json>");
  require_contains(usage, "plugin list");
  require_contains(usage, "--path <path>");
  require_contains(usage, "plugin inspect");
  require_contains(usage, "0  success");
  require_contains(usage, "3  failure");
  require_contains(usage, "4  regression");

  const std::string_view profiles = ztick::cli::profile_list_text();
  require_contains(profiles, "Built-in profiles:");
  require_contains(profiles, "Quick");
  require_contains(profiles, "Standard");
  require_contains(profiles, "Comprehensive");
  require_contains(profiles, "Custom");

  require_contains(ztick::cli::version_text(), "ztick-bench ");
}

/**
 * @brief Verify global options succeed and reject extra arguments.
 */
TEST_CASE("CLI global options dispatch correctly", "[cli][results]") {
  const CommandResult help = ztick::cli::dispatch(arguments({"--help"}));
  REQUIRE(help.code == ExitCode::success);
  REQUIRE(help.output == ztick::cli::usage_text());
  REQUIRE(help.error.empty());

  const CommandResult short_help = ztick::cli::dispatch(arguments({"-h"}));
  REQUIRE(short_help.code == ExitCode::success);
  REQUIRE(short_help.output == ztick::cli::usage_text());

  const CommandResult version = ztick::cli::dispatch(arguments({"--version"}));
  REQUIRE(version.code == ExitCode::success);
  REQUIRE(version.output == ztick::cli::version_text());
  REQUIRE(version.error.empty());

  const CommandResult bad_help = ztick::cli::dispatch(arguments({"--help", "list"}));
  REQUIRE(bad_help.code == ExitCode::usage_error);
  require_contains(bad_help.error, "takes no arguments");

  const CommandResult bad_version = ztick::cli::dispatch(arguments({"--version", "list"}));
  REQUIRE(bad_version.code == ExitCode::usage_error);
  require_contains(bad_version.error, "takes no arguments");
}

/**
 * @brief Verify `list` accepts its documented subjects and nothing else.
 */
TEST_CASE("CLI list command succeeds", "[cli][results]") {
  const CommandResult bare = ztick::cli::dispatch(arguments({"list"}));
  REQUIRE(bare.code == ExitCode::success);
  REQUIRE(bare.output == ztick::cli::profile_list_text());
  REQUIRE(bare.error.empty());

  const CommandResult profiles = ztick::cli::dispatch(arguments({"list", "profiles"}));
  REQUIRE(profiles.code == ExitCode::success);
  REQUIRE(profiles.output == ztick::cli::profile_list_text());

  const CommandResult invalid = ztick::cli::dispatch(arguments({"list", "plugins"}));
  REQUIRE(invalid.code == ExitCode::usage_error);
  require_contains(invalid.error, "accepts no arguments or 'profiles'");
}

/**
 * @brief Verify `system` is routed to the platform inventory command.
 */
TEST_CASE("CLI system command succeeds", "[cli][results]") {
  const CommandResult result = ztick::cli::dispatch(arguments({"system"}));
  REQUIRE(result.code == ExitCode::success);
  REQUIRE(result.error.empty());
  require_contains(result.output, "availability states");
  require_contains(result.output, "cpu_model:");
  REQUIRE(result.data.find("fields") != nullptr);

  const CommandResult system_json = ztick::cli::dispatch(arguments({"--format=json", "system"}));
  REQUIRE(system_json.code == ExitCode::success);
  REQUIRE(parse_envelope(system_json).find("data")->find("fields") != nullptr);

  const CommandResult extra = ztick::cli::dispatch(arguments({"system", "extra"}));
  REQUIRE(extra.code == ExitCode::usage_error);
  require_contains(extra.error, "'system' accepts no arguments");
}

/**
 * @brief Verify result commands are routed to real readers rather than stubs.
 */
TEST_CASE("CLI routes report and compare to stored-result commands", "[cli][results]") {
  const CommandResult report = ztick::cli::dispatch(arguments({"report", "missing.json"}));
  REQUIRE(report.code == ExitCode::failure);
  require_contains(report.error, "cannot read report result 'missing.json'");

  const CommandResult compare = ztick::cli::dispatch(arguments({"compare", "a.json", "b.json"}));
  REQUIRE(compare.code == ExitCode::failure);
  require_contains(compare.error, "cannot read baseline result 'a.json'");

  const CommandResult bad_report = ztick::cli::dispatch(arguments({"report"}));
  REQUIRE(bad_report.code == ExitCode::usage_error);
  require_contains(bad_report.error, "requires exactly one result path");

  const CommandResult bad_compare = ztick::cli::dispatch(arguments({"compare", "a.json"}));
  REQUIRE(bad_compare.code == ExitCode::usage_error);
  require_contains(bad_compare.error, "requires a baseline and a candidate path");
}

/**
 * @brief Verify plugin commands are routed to metadata-only implementations.
 */
TEST_CASE("CLI routes plugin commands to metadata-only commands", "[cli][results][p410]") {
  const CommandResult list = ztick::cli::dispatch(arguments({"plugin", "list"}));
  REQUIRE(list.code == ExitCode::success);
  REQUIRE(list.error.empty());
  require_contains(list.output, "discovered plugins");

  const CommandResult inspect = ztick::cli::dispatch(arguments({"plugin", "inspect"}));
  REQUIRE(inspect.code == ExitCode::usage_error);
  require_contains(inspect.error, "requires exactly one path");

  const CommandResult missing =
      ztick::cli::dispatch(arguments({"plugin", "inspect", "missing-module.so"}));
  REQUIRE(missing.code == ExitCode::failure);
  require_contains(missing.output, "plugin inspection");
  require_contains(missing.error, "plugin inspection failed");
}

/**
 * @brief Verify global format tokens and option removal are deterministic.
 */
TEST_CASE("CLI selects global output format", "[cli][results][p410]") {
  using ztick::cli::OutputFormat;

  REQUIRE(ztick::cli::output_format_token(OutputFormat::text) == "text");
  REQUIRE(ztick::cli::output_format_token(OutputFormat::json) == "json");
  REQUIRE(ztick::cli::output_format_token(static_cast<OutputFormat>(99)) == "text");

  std::string error;
  const auto separate =
      ztick::cli::select_output_format(arguments({"list", "--format", "json", "profiles"}), &error);
  REQUIRE(separate.has_value());
  REQUIRE(error.empty());
  REQUIRE(separate->format == OutputFormat::json);
  REQUIRE(separate->args == arguments({"list", "profiles"}));

  const auto inline_value =
      ztick::cli::select_output_format(arguments({"--format=text", "system"}), &error);
  REQUIRE(inline_value.has_value());
  REQUIRE(inline_value->format == OutputFormat::text);
  REQUIRE(inline_value->args == arguments({"system"}));

  REQUIRE_FALSE(
      ztick::cli::select_output_format(arguments({"list", "--format=yaml"}), &error).has_value());
  require_contains(error, "'text' or 'json'");
  REQUIRE_FALSE(ztick::cli::select_output_format(arguments({"--format"}), &error).has_value());
  require_contains(error, "requires a value");
  REQUIRE_FALSE(
      ztick::cli::select_output_format(arguments({"--format=json", "--format=json"}), &error)
          .has_value());
  require_contains(error, "more than once");
}

/**
 * @brief Verify JSON output wraps success, failure and usage outcomes in one envelope.
 */
TEST_CASE("CLI JSON output uses a stable envelope", "[cli][results][p410]") {
  const CommandResult list =
      ztick::cli::dispatch(arguments({"--format", "json", "list", "profiles"}));
  REQUIRE(list.code == ExitCode::success);
  REQUIRE(list.error.empty());
  const ztick::ConfigValue envelope = parse_envelope(list);
  REQUIRE(envelope.type() == ztick::ConfigValueType::object);
  REQUIRE(envelope.find("schema_version")->as_integer() == 1);
  REQUIRE(string_field(envelope, "command") == "list");
  REQUIRE(string_field(envelope, "status") == "success");
  REQUIRE(envelope.find("exit_code")->as_integer() == 0);
  REQUIRE(envelope.find("data")->find("profiles")->as_array()->size() == 4U);
  REQUIRE(string_field(envelope, "error").empty());

  const CommandResult failure =
      ztick::cli::dispatch(arguments({"--format=json", "report", "missing.json"}));
  REQUIRE(failure.code == ExitCode::failure);
  REQUIRE(failure.error.empty());
  const ztick::ConfigValue failure_envelope = parse_envelope(failure);
  REQUIRE(string_field(failure_envelope, "command") == "report");
  REQUIRE(string_field(failure_envelope, "status") == "failure");
  REQUIRE(failure_envelope.find("exit_code")->as_integer() == 3);
  require_contains(string_field(failure_envelope, "error"), "cannot read report result");

  const CommandResult usage =
      ztick::cli::dispatch(arguments({"--format=json", "compare", "only-one.json"}));
  REQUIRE(usage.code == ExitCode::usage_error);
  REQUIRE(usage.error.empty());
  const ztick::ConfigValue usage_envelope = parse_envelope(usage);
  REQUIRE(string_field(usage_envelope, "status") == "usage_error");
  REQUIRE(usage_envelope.find("exit_code")->as_integer() == 1);
  require_contains(string_field(usage_envelope, "error"), "baseline and a candidate path");

  const CommandResult option_error =
      ztick::cli::dispatch(arguments({"list", "--format=json", "--format=text"}));
  REQUIRE(option_error.code == ExitCode::usage_error);
  REQUIRE(option_error.error.empty());
  REQUIRE(string_field(parse_envelope(option_error), "status") == "usage_error");
}

/**
 * @brief Verify malformed invocations produce usage errors instead of silence.
 */
TEST_CASE("CLI malformed input reports usage errors", "[cli][results]") {
  const CommandResult empty = ztick::cli::dispatch({});
  REQUIRE(empty.code == ExitCode::usage_error);
  REQUIRE(empty.output == ztick::cli::usage_text());
  require_contains(empty.error, "missing command");

  const CommandResult unknown = ztick::cli::dispatch(arguments({"benchmark"}));
  REQUIRE(unknown.code == ExitCode::usage_error);
  require_contains(unknown.error, "unknown command 'benchmark'");

  const CommandResult option = ztick::cli::dispatch(arguments({"--json"}));
  REQUIRE(option.code == ExitCode::usage_error);
  require_contains(option.error, "unknown command '--json'");

  const CommandResult bare_plugin = ztick::cli::dispatch(arguments({"plugin"}));
  REQUIRE(bare_plugin.code == ExitCode::usage_error);
  require_contains(bare_plugin.error, "requires 'list' or 'inspect'");

  const CommandResult bad_plugin = ztick::cli::dispatch(arguments({"plugin", "run"}));
  REQUIRE(bad_plugin.code == ExitCode::usage_error);
  require_contains(bad_plugin.error, "unknown plugin subcommand 'run'");

  const CommandResult bare_run = ztick::cli::dispatch(arguments({"run"}));
  REQUIRE(bare_run.code == ExitCode::usage_error);
  require_contains(bare_run.error, "run requires a config path");
}
