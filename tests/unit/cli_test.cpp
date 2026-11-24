// SPDX-License-Identifier: MIT
/**
 * @file cli_test.cpp
 * @brief Catch2 tests for CLI dispatch, help text and stable exit codes.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/cli.hpp>

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

} // namespace

/**
 * @brief Verify every exit code has a stable token, including unknown values.
 */
TEST_CASE("CLI exit codes have stable tokens", "[cli][results]") {
  REQUIRE(ztick::cli::exit_code_token(ExitCode::success) == "success");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::usage_error) == "usage_error");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::unavailable) == "unavailable");
  REQUIRE(ztick::cli::exit_code_token(ExitCode::failure) == "failure");
  REQUIRE(ztick::cli::exit_code_token(static_cast<ExitCode>(99)) == "unknown");

  REQUIRE(static_cast<int>(ExitCode::success) == 0);
  REQUIRE(static_cast<int>(ExitCode::usage_error) == 1);
  REQUIRE(static_cast<int>(ExitCode::unavailable) == 2);
  REQUIRE(static_cast<int>(ExitCode::failure) == 3);
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
  require_contains(usage, "plugin inspect");
  require_contains(usage, "0  success");
  require_contains(usage, "3  failure");

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

  const CommandResult extra = ztick::cli::dispatch(arguments({"system", "extra"}));
  REQUIRE(extra.code == ExitCode::usage_error);
  require_contains(extra.error, "'system' accepts no arguments");
}

/**
 * @brief Verify later roadmap commands are recognized but reported unavailable.
 */
TEST_CASE("CLI recognizes commands whose implementation lands later", "[cli][results]") {
  for (const std::string_view command : {"compare", "report", "plugin list", "plugin inspect"}) {
    const std::size_t split = command.find(' ');
    const CommandResult result =
        split == std::string_view::npos
            ? ztick::cli::dispatch(arguments({command}))
            : ztick::cli::dispatch(
                  arguments({command.substr(0U, split), command.substr(split + 1U)}));
    REQUIRE(result.code == ExitCode::unavailable);
    REQUIRE(result.output.empty());
    require_contains(result.error, "not available in this build");
  }
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
