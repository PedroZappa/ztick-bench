// SPDX-License-Identifier: MIT
/**
 * @file run_command.cpp
 * @brief Argument parsing and file I/O for the `ztick-bench run` command.
 *
 * Core orchestration lives in @ref ztick::run_configured_session; this module
 * only turns command-line text into a validated config, applies overrides and
 * emits the returned result document. Host-dependent services are injected
 * through @ref ztick::cli::RunContext so every branch is unit-testable.
 */

#include <ztick/run_command.hpp>

#include <ztick/config_document.hpp>
#include <ztick/json.hpp>
#include <ztick/metadata.hpp>
#include <ztick/platform/linux_system_info.hpp>
#include <ztick/result_document.hpp>
#include <ztick/run_session.hpp>
#include <ztick/system_info_metadata.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztick::cli {

namespace {

/** @brief Internal helpers for `run` argument and file handling. */
namespace detail {

/**
 * @brief Write an error only when the receiver is non-null.
 * @param error Optional receiver for the message.
 * @param message Human-readable failure explanation.
 */
void record_error(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

/**
 * @brief Report whether text should be parsed as a JSON value.
 * @param text Override value text.
 * @return True for quoted strings, objects and arrays.
 */
bool is_json_literal(std::string_view text) noexcept {
  return !text.empty() && (text.front() == '"' || text.front() == '{' || text.front() == '[');
}

/**
 * @brief Parse one override value using the target member's existing type.
 * @param text Raw text after the `=` separator.
 * @param target Existing member, or `nullptr` when creating a new member.
 * @return Parsed config value, or `std::nullopt` on failure.
 * @note JSON literals are parsed directly. Plain text uses the existing type so
 *       `--set execution.iterations=16` needs no quotes; a new member infers its
 *       type from the text.
 */
std::optional<ConfigValue> parse_override_value(std::string_view text, const ConfigValue *target) {
  if (text.empty()) {
    return std::nullopt;
  }
  if (is_json_literal(text)) {
    const JsonReadResult parsed = json_read(text);
    if (!parsed.valid() || parsed.value.type() == ConfigValueType::null) {
      return std::nullopt;
    }
    return parsed.value;
  }

  const ConfigValueType existing = target == nullptr ? ConfigValueType::string : target->type();
  if (existing == ConfigValueType::string) {
    if (target != nullptr) {
      return ConfigValue::string(std::string(text));
    }
    if (text == "true") {
      return ConfigValue::boolean(true);
    }
    if (text == "false") {
      return ConfigValue::boolean(false);
    }
  }
  if (existing == ConfigValueType::boolean) {
    if (text == "true") {
      return ConfigValue::boolean(true);
    }
    if (text == "false") {
      return ConfigValue::boolean(false);
    }
    return std::nullopt;
  }

  std::int64_t integer = 0;
  const auto integer_result = std::from_chars(text.data(), text.data() + text.size(), integer);
  const bool whole_integer =
      integer_result.ec == std::errc{} && integer_result.ptr == text.data() + text.size();
  if (whole_integer && existing != ConfigValueType::real) {
    return ConfigValue::integer(integer);
  }

  double real = 0.0;
  const auto real_result = std::from_chars(text.data(), text.data() + text.size(), real);
  const bool finite_real = real_result.ec == std::errc{} &&
                           real_result.ptr == text.data() + text.size() && std::isfinite(real);
  if (whole_integer && existing == ConfigValueType::real) {
    return ConfigValue::real(static_cast<double>(integer));
  }
  if (finite_real && existing != ConfigValueType::integer) {
    return ConfigValue::real(real);
  }
  if (existing == ConfigValueType::integer || existing == ConfigValueType::real ||
      existing == ConfigValueType::boolean) {
    return std::nullopt;
  }
  return ConfigValue::string(std::string(text));
}

/**
 * @brief Read the whole content of a text file.
 * @param path File to read.
 * @param content Receives the file content.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the file was read completely.
 */
bool read_text_file(const std::filesystem::path &path, std::string &content, std::string *error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    record_error(error, "cannot open config file: " + path.string());
    return false;
  }
  content.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  if (stream.bad()) {
    record_error(error, "cannot read config file: " + path.string());
    return false;
  }
  return true;
}

/**
 * @brief Build an ISO-8601 UTC timestamp for the current instant.
 * @return Timestamp string with second precision.
 */
std::string current_timestamp() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
  if (const std::tm *converted = std::gmtime(&now); converted != nullptr) {
    utc = *converted;
  }
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

/**
 * @brief Collect the live machine metadata with explicit availability states.
 * @return Metadata object for the `machine` result section.
 */
ConfigValue current_machine_metadata() {
  const auto provider = ztick::platform::linux_sysinfo::make_linux_system_info_provider();
  if (provider == nullptr) {
    return make_metadata_object({metadata_unavailable("system_inventory", "provider unavailable")});
  }
  const auto collected = provider->collect();
  if (!collected.has_value()) {
    const char *reason = collected.reason != nullptr ? collected.reason : "inventory unavailable";
    return make_metadata_object({metadata_unavailable("system_inventory", reason)});
  }
  return make_metadata_object(ztick::platform::system_info_metadata(collected.value));
}

} // namespace detail
} // namespace

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
RunContext default_run_context() {
  return RunContext{
      []() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
      },
      detail::current_machine_metadata,
      []() {
        return "session-" +
               std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
      },
      detail::current_timestamp,
  };
}

std::filesystem::path default_result_path(const std::filesystem::path &config_path) {
  std::filesystem::path result = config_path;
  return result.replace_extension(".result.json");
}

std::optional<RunInvocation> parse_run_arguments(const std::vector<std::string_view> &args,
                                                 std::string *error) {
  if (args.empty()) {
    detail::record_error(error, "run requires a config path");
    return std::nullopt;
  }
  if (args.front().starts_with('-')) {
    detail::record_error(error, "run requires the config path as its first argument");
    return std::nullopt;
  }

  RunInvocation invocation;
  invocation.config_path = std::string(args.front());
  invocation.output_path = default_result_path(invocation.config_path);
  for (std::size_t index = 1U; index < args.size(); ++index) {
    const std::string_view argument = args[index];
    if (argument == "--output" || argument == "--set") {
      if (index + 1U >= args.size()) {
        detail::record_error(error, "option " + std::string(argument) + " requires a value");
        return std::nullopt;
      }
      const std::string value(args[++index]);
      if (value.empty()) {
        detail::record_error(error,
                             "option " + std::string(argument) + " requires a non-empty value");
        return std::nullopt;
      }
      if (argument == "--output") {
        invocation.output_path = value;
      } else {
        invocation.overrides.push_back(value);
      }
      continue;
    }
    if (argument.starts_with("--output=")) {
      const std::string value(argument.substr(9U));
      if (value.empty()) {
        detail::record_error(error, "option --output requires a non-empty value");
        return std::nullopt;
      }
      invocation.output_path = value;
      continue;
    }
    if (argument.starts_with("--set=")) {
      const std::string value(argument.substr(6U));
      if (value.empty()) {
        detail::record_error(error, "option --set requires a non-empty value");
        return std::nullopt;
      }
      invocation.overrides.push_back(value);
      continue;
    }
    detail::record_error(error, "unknown run option: " + std::string(argument));
    return std::nullopt;
  }

  if (invocation.output_path.lexically_normal() == invocation.config_path.lexically_normal()) {
    detail::record_error(error, "result path must differ from the config path");
    return std::nullopt;
  }
  return invocation;
}

bool apply_config_overrides(ConfigValue &document, const std::vector<std::string> &overrides,
                            std::string *error) {
  ConfigValue updated = document;
  for (const std::string_view assignment : overrides) {
    const std::size_t separator = assignment.find('=');
    if (separator == std::string_view::npos || separator == 0U ||
        separator + 1U == assignment.size()) {
      detail::record_error(error, "override must use path=value form: " + std::string(assignment));
      return false;
    }
    const std::string_view path = assignment.substr(0U, separator);
    const std::string_view text = assignment.substr(separator + 1U);
    ConfigValue *parent = &updated;
    std::size_t begin = 0U;
    while (begin < path.size()) {
      const std::size_t end = path.find('.', begin);
      const std::size_t length = end == std::string_view::npos ? path.size() - begin : end - begin;
      if (length == 0U) {
        detail::record_error(error, "override path has an empty segment: " + std::string(path));
        return false;
      }
      const std::string_view segment = path.substr(begin, length);
      if (end == std::string_view::npos) {
        ConfigValue *target = parent->find(segment);
        const auto parsed = detail::parse_override_value(text, target);
        if (!parsed.has_value() || !parent->set(std::string(segment), *parsed)) {
          detail::record_error(error, "invalid override value for " + std::string(path));
          return false;
        }
        break;
      }
      parent = parent->find(segment);
      if (parent == nullptr || parent->type() != ConfigValueType::object) {
        detail::record_error(error, "override path is not an object: " + std::string(path));
        return false;
      }
      begin = end + 1U;
    }
  }
  document = std::move(updated);
  return true;
}

RunOutcome execute_run(const std::vector<std::string_view> &args, const RunContext &context) {
  RunOutcome outcome;
  std::string error;
  const auto invocation = parse_run_arguments(args, &error);
  if (!invocation.has_value()) {
    outcome.command = CommandResult{ExitCode::usage_error, {}, "ztick-bench: " + error + "\n"};
    return outcome;
  }

  std::string text;
  if (!detail::read_text_file(invocation->config_path, text, &error)) {
    outcome.command = CommandResult{ExitCode::failure, {}, "ztick-bench: " + error + "\n"};
    return outcome;
  }
  const ConfigParseResult parsed = parse_config_document(text);
  if (!parsed.valid()) {
    outcome.command =
        CommandResult{ExitCode::failure, {}, "ztick-bench: invalid config: " + parsed.error + "\n"};
    return outcome;
  }

  ConfigValue config = parsed.document;
  if (!validate_config_document(config, &error)) {
    outcome.command = CommandResult{ExitCode::failure, {}, "ztick-bench: " + error + "\n"};
    return outcome;
  }
  if (!apply_config_overrides(config, invocation->overrides, &error) ||
      !validate_config_document(config, &error)) {
    outcome.command = CommandResult{ExitCode::usage_error, {}, "ztick-bench: " + error + "\n"};
    return outcome;
  }

  const auto document = run_configured_session(config, context, &error);
  if (!document.has_value()) {
    outcome.command = CommandResult{ExitCode::failure, {}, "ztick-bench: " + error + "\n"};
    return outcome;
  }
  const ResultWriteResult written = write_result_document(invocation->output_path, *document);
  if (!written.valid()) {
    outcome.command = CommandResult{ExitCode::failure, {}, "ztick-bench: " + written.error + "\n"};
    return outcome;
  }

  outcome.document = *document;
  outcome.output_path = invocation->output_path;
  const ConfigValue *status = document->find("status");
  const std::string status_text =
      status != nullptr && status->as_string().has_value() ? *status->as_string() : "incomplete";
  if (status_text == "complete") {
    const ConfigValue *runs = document->find("runs");
    const std::size_t run_count =
        runs != nullptr && runs->as_array() != nullptr ? runs->as_array()->size() : 0U;
    outcome.command =
        CommandResult{ExitCode::success,
                      "ztick-bench: run complete\n  config: " + invocation->config_path.string() +
                          "\n  result: " + invocation->output_path.string() +
                          "\n  runs: " + std::to_string(run_count) + "\n  status: complete\n",
                      {}};
  } else {
    outcome.command = CommandResult{ExitCode::failure,
                                    "ztick-bench: result written with incomplete runs: " +
                                        invocation->output_path.string() + "\n",
                                    "ztick-bench: validation failed; see the result document\n"};
  }
  return outcome;
}

RunOutcome execute_run(const std::vector<std::string_view> &args) {
  return execute_run(args, default_run_context());
}

} // namespace ztick::cli
