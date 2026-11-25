// SPDX-License-Identifier: MIT
/**
 * @file run_command_test.cpp
 * @brief Catch2 tests for `run` argument parsing, overrides and file I/O.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/config_document.hpp>
#include <ztick/config_value.hpp>
#include <ztick/metadata.hpp>
#include <ztick/result_document.hpp>
#include <ztick/run_command.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using ztick::ConfigValue;
using ztick::SessionContext;
using ztick::cli::ExitCode;

/** @brief Fixed clock increment in seconds used by every deterministic test. */
constexpr double kClockStepSeconds = 0.001;

/**
 * @brief Build a complete v1 config with a deterministic built-in generator.
 * @param iterations Number of processing blocks in one repetition.
 * @param repetitions Number of repetitions in the session.
 * @return Compact JSON config text accepted by the schema and semantic planner.
 */
std::string make_config(std::int64_t iterations, std::int64_t repetitions) {
  std::string text = R"json({"config_version":"1.0.0","sdk_version":"1.0.0",)json"
                     R"json("integration":{"id":"app-a","version":"1.2.3"},)json"
                     R"json("workload":{"level":"processor","processors":["eq"],)json"
                     R"json("generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,)json"
                     R"json("phase_rad":0.0}},)json"
                     R"json("dimensions":{"sample_rate_hz":48000.0,"block_size":128,)json"
                     R"json("channels":2,"oversampling":1,"instances":1},)json"
                     R"json("execution":{"duration_mode":"iterations","iterations":)json";
  text += std::to_string(iterations);
  text += R"json(,"duration_seconds":0.0,"repetitions":)json";
  text += std::to_string(repetitions);
  text += R"json(,"warmup":{"iterations":0},"isolation":"auto"},)json"
          R"json("profile":{"name":"Quick"},)json"
          R"json("measurement":{"detail":"aggregate","end_to_end":false,)json"
          R"json("timing_levels":["per_block","whole_run"]},)json"
          R"json("validation":{"level":"Basic","tolerances":{},"references":[]},)json"
          R"json("parallelism":{"workers":"auto","affinity":"auto"}})json";
  return text;
}

/** @brief Shared state behind a deterministic injected clock. */
struct ClockState {
  /** @brief Current synthetic time in seconds. */
  double seconds{0.0};
};

/**
 * @brief Build a fully injected run context with a deterministic clock.
 * @return Context using synthetic timing, metadata and identity.
 */
SessionContext make_context() {
  const auto clock_state = std::make_shared<ClockState>();
  SessionContext context;
  context.clock = [clock_state]() {
    clock_state->seconds += kClockStepSeconds;
    return clock_state->seconds;
  };
  context.machine_metadata = []() {
    return ztick::make_metadata_object({ztick::metadata_value("hostname", "test-host")});
  };
  context.session_id = []() { return std::string("session-run-command"); };
  context.timestamp = []() { return std::string("2026-01-01T00:00:00Z"); };
  return context;
}

/** @brief Owning temporary directory for one test case. */
class TempDirectory {
public:
  /**
   * @brief Create a uniquely named temporary directory.
   * @param tag Short discriminator for the calling test case.
   */
  explicit TempDirectory(std::string_view tag) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("ztick_run_cmd_" + std::string(tag) + "_" + std::to_string(ticks));
    std::filesystem::create_directories(path_);
  }

  /** @brief Remove the directory and everything inside it without throwing. */
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempDirectory(const TempDirectory &) = delete;
  TempDirectory &operator=(const TempDirectory &) = delete;

  /**
   * @brief Access the created directory.
   * @return Path to the temporary directory.
   */
  [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

private:
  /** @brief Path to the temporary directory. */
  std::filesystem::path path_;
};

/**
 * @brief Write text to a file with a checked stream.
 * @param path Destination file.
 * @param text Complete content to write.
 */
void write_text(const std::filesystem::path &path, const std::string &text) {
  std::ofstream stream(path, std::ios::binary);
  REQUIRE(stream.is_open());
  stream << text;
  stream.close();
  REQUIRE_FALSE(stream.bad());
}

/**
 * @brief Read an integer member from an object.
 * @param object Object to search.
 * @param key Member name.
 * @return Stored integer, or zero for a missing or mistyped member.
 */
std::int64_t integer_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  return member == nullptr || !member->as_integer().has_value() ? 0 : *member->as_integer();
}

/**
 * @brief Read a string member from an object.
 * @param object Object to search.
 * @param key Member name.
 * @return Stored string, or an empty string for a missing or mistyped member.
 */
std::string string_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  return member == nullptr || !member->as_string().has_value() ? std::string{}
                                                               : *member->as_string();
}

} // namespace

/**
 * @brief Verify argument forms, defaults and override ordering are parsed exactly.
 */
TEST_CASE("run parser accepts documented argument forms", "[cli][run]") {
  const auto separated =
      ztick::cli::parse_run_arguments({"config.json", "--set", "execution.iterations=8",
                                       "--set=profile.name=Custom", "--output", "out.json"});
  REQUIRE(separated.has_value());
  REQUIRE(separated->config_path == std::filesystem::path("config.json"));
  REQUIRE(separated->output_path == std::filesystem::path("out.json"));
  REQUIRE(separated->overrides.size() == 2U);
  REQUIRE(separated->overrides[0] == "execution.iterations=8");
  REQUIRE(separated->overrides[1] == "profile.name=Custom");

  const auto inline_forms = ztick::cli::parse_run_arguments(
      {"measure.json", "--output=result.json", "--set=execution.iterations=2"});
  REQUIRE(inline_forms.has_value());
  REQUIRE(inline_forms->output_path == std::filesystem::path("result.json"));
  REQUIRE(inline_forms->overrides.size() == 1U);
  REQUIRE(inline_forms->overrides.front() == "execution.iterations=2");

  REQUIRE(ztick::cli::default_result_path("input.json") ==
          std::filesystem::path("input.result.json"));
  const auto defaulted = ztick::cli::parse_run_arguments({"input.json"});
  REQUIRE(defaulted.has_value());
  REQUIRE(defaulted->output_path == std::filesystem::path("input.result.json"));
  REQUIRE(defaulted->overrides.empty());
}

/**
 * @brief Verify malformed run arguments produce useful usage errors.
 */
TEST_CASE("run parser rejects malformed arguments", "[cli][run]") {
  std::string error;
  REQUIRE_FALSE(ztick::cli::parse_run_arguments({}, &error));
  REQUIRE(error.find("config path") != std::string::npos);

  REQUIRE_FALSE(ztick::cli::parse_run_arguments({"--output", "out.json"}, &error));
  REQUIRE(error.find("first argument") != std::string::npos);

  REQUIRE_FALSE(ztick::cli::parse_run_arguments({"config.json", "--set"}, &error));
  REQUIRE(error.find("requires a value") != std::string::npos);

  REQUIRE_FALSE(ztick::cli::parse_run_arguments({"config.json", "--unknown"}, &error));
  REQUIRE(error.find("unknown run option") != std::string::npos);

  REQUIRE_FALSE(
      ztick::cli::parse_run_arguments({"config.json", "--output", "config.json"}, &error));
  REQUIRE(error.find("must differ") != std::string::npos);
}

/**
 * @brief Verify typed overrides, JSON literals and new scalar members.
 */
TEST_CASE("config overrides apply with target-aware types", "[cli][run]") {
  const auto parsed = ztick::parse_config_document(make_config(2, 1));
  REQUIRE(parsed.valid());
  ConfigValue config = parsed.document;

  const std::vector<std::string> overrides{"execution.iterations=8",
                                           "dimensions.sample_rate_hz=44100",
                                           "measurement.end_to_end=true",
                                           "profile.name=Custom",
                                           R"(workload.processors=["eq","comp"])",
                                           "validation.tolerances.absolute=0.0001"};
  REQUIRE(ztick::cli::apply_config_overrides(config, overrides));
  REQUIRE(integer_member(*config.find("execution"), "iterations") == 8);
  REQUIRE(*config.find("dimensions")->find("sample_rate_hz")->as_real() == 44100.0);
  REQUIRE(*config.find("measurement")->find("end_to_end")->as_boolean());
  REQUIRE(string_member(*config.find("profile"), "name") == "Custom");
  const ConfigValue *processors = config.find("workload")->find("processors");
  REQUIRE(processors != nullptr);
  REQUIRE(processors->as_array() != nullptr);
  REQUIRE(processors->as_array()->size() == 2U);
  REQUIRE(*config.find("validation")->find("tolerances")->find("absolute")->as_real() == 0.0001);
}

/**
 * @brief Verify a failed override leaves the whole config unchanged.
 */
TEST_CASE("config overrides are atomic on failure", "[cli][run]") {
  const auto parsed = ztick::parse_config_document(make_config(2, 1));
  REQUIRE(parsed.valid());
  const ConfigValue original = parsed.document;
  ConfigValue config = original;

  std::string error;
  const std::vector<std::string> invalid_value{"execution.iterations=12",
                                               "execution.repetitions=not-a-number"};
  REQUIRE_FALSE(ztick::cli::apply_config_overrides(config, invalid_value, &error));
  REQUIRE(error.find("invalid override value") != std::string::npos);
  REQUIRE(config == original);

  const std::vector<std::string> invalid_path{"profile.name.nested=x"};
  REQUIRE_FALSE(ztick::cli::apply_config_overrides(config, invalid_path, &error));
  REQUIRE(error.find("not an object") != std::string::npos);
  REQUIRE(config == original);
}

/**
 * @brief Verify an overridden run is executed and written to the requested path.
 */
TEST_CASE("run executes with overrides applied", "[cli][run]") {
  TempDirectory directory("success");
  const std::filesystem::path config_path = directory.path() / "config.json";
  const std::filesystem::path output_path = directory.path() / "result.json";
  write_text(config_path, make_config(2, 1));

  const std::string config_arg = config_path.string();
  const std::string output_arg = output_path.string();
  const std::vector<std::string_view> args{
      config_arg, "--set",   "execution.iterations=4", "--set", "profile.name=Override",
      "--output", output_arg};
  const auto outcome = ztick::cli::execute_run(args, make_context());
  REQUIRE(outcome.command.code == ExitCode::success);
  REQUIRE(outcome.command.error.empty());
  REQUIRE(outcome.command.output.find("run complete") != std::string::npos);
  REQUIRE(outcome.output_path == output_path);
  REQUIRE(std::filesystem::exists(output_path));

  const auto stored = ztick::read_result_document(output_path);
  REQUIRE(stored.valid());
  REQUIRE(stored.document.find("config_snapshot") != nullptr);
  const ConfigValue &snapshot = *stored.document.find("config_snapshot");
  REQUIRE(integer_member(*snapshot.find("execution"), "iterations") == 4);
  REQUIRE(string_member(*snapshot.find("profile"), "name") == "Override");
  REQUIRE(stored.document.find("runs")->as_array()->size() == 1U);
  REQUIRE(string_member(stored.document, "status") == "complete");
}

/**
 * @brief Verify context-aware dispatch observes a run but leaves read-only commands alone.
 */
TEST_CASE("CLI context dispatch forwards run progress", "[cli][run][progress]") {
  TempDirectory directory("context-dispatch");
  const std::filesystem::path config_path = directory.path() / "config.json";
  write_text(config_path, make_config(2, 1));
  const std::string config_arg = config_path.string();

  std::vector<ztick::SessionProgress> observations;
  SessionContext context = make_context();
  context.progress = [&observations](const ztick::SessionProgress &progress) {
    observations.push_back(progress);
  };
  const std::vector<std::string_view> run_args{"--format", "json", "run", config_arg};
  const auto run = ztick::cli::dispatch_with_context(run_args, context);
  REQUIRE(run.code == ExitCode::success);
  REQUIRE_FALSE(observations.empty());
  REQUIRE(observations.front().phase == ztick::SessionPhase::validating);
  REQUIRE(observations.back().phase == ztick::SessionPhase::complete);
  const bool saw_running_context = std::any_of(
      observations.begin(), observations.end(), [](const ztick::SessionProgress &progress) {
        return progress.phase == ztick::SessionPhase::running &&
               progress.timing_level == "per_block" && progress.isolation == "in_process";
      });
  REQUIRE(saw_running_context);

  const std::size_t observed = observations.size();
  const auto list = ztick::cli::dispatch_with_context({"--format", "json", "list"}, context);
  REQUIRE(list.code == ExitCode::success);
  REQUIRE(observations.size() == observed);
}

/**
 * @brief Verify usage and runtime failures do not create partial result files.
 */
TEST_CASE("run reports failures without writing results", "[cli][run]") {
  TempDirectory directory("failures");
  const std::filesystem::path config_path = directory.path() / "config.json";
  write_text(config_path, make_config(2, 1));

  const std::string config_arg = config_path.string();
  const std::filesystem::path default_output = ztick::cli::default_result_path(config_path);
  const std::string missing_arg = (directory.path() / "missing.json").string();
  const auto missing = ztick::cli::execute_run({missing_arg}, make_context());
  REQUIRE(missing.command.code == ExitCode::failure);
  REQUIRE(missing.command.error.find("cannot open config file") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(directory.path() / "missing.result.json"));

  const auto bad_override =
      ztick::cli::execute_run({config_arg, "--set", "execution.iterations=abc"}, make_context());
  REQUIRE(bad_override.command.code == ExitCode::usage_error);
  REQUIRE(bad_override.command.error.find("invalid override value") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(default_output));

  const std::string bad_output_arg = (directory.path() / "absent" / "result.json").string();
  const auto bad_output =
      ztick::cli::execute_run({config_arg, "--output", bad_output_arg}, make_context());
  REQUIRE(bad_output.command.code == ExitCode::failure);
  REQUIRE_FALSE(std::filesystem::exists(bad_output_arg));

  const std::string absent_arg = (directory.path() / "absent-config.json").string();
  const auto production = ztick::cli::execute_run({absent_arg});
  REQUIRE(production.command.code == ExitCode::failure);
}

/**
 * @brief Verify the production context exposes usable, non-empty services.
 */
TEST_CASE("run production context supplies host services", "[cli][run]") {
  const auto context = ztick::cli::default_run_context();
  REQUIRE(context.clock);
  REQUIRE(context.machine_metadata);
  REQUIRE(context.session_id);
  REQUIRE(context.timestamp);

  const double first = context.clock();
  REQUIRE(std::isfinite(first));
  REQUIRE(context.clock() >= first);
  REQUIRE(context.machine_metadata().type() == ztick::ConfigValueType::object);
  REQUIRE_FALSE(context.session_id().empty());
  REQUIRE_FALSE(context.timestamp().empty());
}
