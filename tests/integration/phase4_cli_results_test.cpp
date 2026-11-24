// SPDX-License-Identifier: MIT
/**
 * @file phase4_cli_results_test.cpp
 * @brief End-to-end verification of the Phase 4 CLI and results layer.
 *
 * This is the verification pass for ROADMAP P4.11. It drives the documented CLI
 * commands, portable config documents, core comparisons and CI output in one
 * suite. Every expected status, token and numeric change is computed from the
 * published contracts in PROJECT_SPEC §3.3, RESULTS_FORMAT §6/§10/§12, BR §13
 * and METRICS §6; no expectation is taken from the implementations under test
 * (TESTING_STRATEGY §9).
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/cli.hpp>
#include <ztick/comparison.hpp>
#include <ztick/config_document.hpp>
#include <ztick/config_value.hpp>
#include <ztick/json.hpp>
#include <ztick/metadata.hpp>
#include <ztick/result_document.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

/** @brief Deterministic helpers for the Phase 4 verification pass. */
namespace {

using ztick::ConfigValue;

/**
 * @brief Create an isolated scratch directory for one verification test case.
 * @param name Short test-case-specific directory name.
 * @return Absolute path of a freshly emptied directory under the build tree.
 * @note Separate names let CTest run the test cases in parallel safely.
 */
std::filesystem::path scratch_directory(std::string_view name) {
  const std::filesystem::path path =
      std::filesystem::current_path() / "phase4_verification_check" / std::string(name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

/**
 * @brief Write text to a file in the scratch directory.
 * @param path Destination path; its parent must already exist.
 * @param text Complete text to write.
 */
void write_text_file(const std::filesystem::path &path, std::string_view text) {
  std::ofstream stream(path);
  REQUIRE(stream.is_open());
  stream << text;
  REQUIRE(stream.good());
}

/**
 * @brief Read the runnable v1 config used by the verification pass.
 * @return Compact config JSON matching RESULTS_FORMAT §6 with small durations.
 */
std::string_view runnable_config_text() {
  static constexpr std::string_view text = R"json({
  "config_version": "1.0.0",
  "sdk_version": "1.0.0",
  "integration": { "id": "app-a", "version": "1.2.3" },
  "workload": {
    "level": "processor",
    "processors": ["eq"],
    "generator": {
      "kind": "sine",
      "amplitude": 0.5,
      "frequency_hz": 1000.0,
      "phase_rad": 0.0
    }
  },
  "dimensions": {
    "sample_rate_hz": 48000.0,
    "block_size": 128,
    "channels": 2,
    "oversampling": 1,
    "instances": 1
  },
  "execution": {
    "duration_mode": "iterations",
    "iterations": 2,
    "duration_seconds": 0.0,
    "repetitions": 1,
    "warmup": { "iterations": 0 },
    "isolation": "auto"
  },
  "profile": { "name": "Quick" },
  "measurement": {
    "detail": "aggregate",
    "end_to_end": false,
    "timing_levels": ["per_block", "whole_run"]
  },
  "validation": { "level": "Basic", "tolerances": {}, "references": [] },
  "parallelism": { "workers": "auto", "affinity": "auto" }
}
)json";
  return text;
}

/**
 * @brief Read one string member used in verification assertions.
 * @param object Object to inspect.
 * @param key Member name.
 * @return Stored text, or an empty string when the member is missing or mistyped.
 */
std::string string_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  return member == nullptr || !member->as_string().has_value() ? std::string{}
                                                               : *member->as_string();
}

/**
 * @brief Read one numeric member used in verification assertions.
 * @param object Object to inspect.
 * @param key Member name.
 * @return Stored number as a double, or zero when missing or non-numeric.
 */
double number_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  if (member == nullptr) {
    return 0.0;
  }
  if (member->as_real().has_value()) {
    return *member->as_real();
  }
  if (member->as_integer().has_value()) {
    return static_cast<double>(*member->as_integer());
  }
  return 0.0;
}

/**
 * @brief Read one boolean member used in verification assertions.
 * @param object Object to inspect.
 * @param key Member name.
 * @return Stored boolean, or false when the member is missing or mistyped.
 */
bool bool_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  return member != nullptr && member->as_boolean().has_value() && *member->as_boolean();
}

/**
 * @brief Build the minimal config snapshot required by a stored result.
 * @return Portable v1 config object with empty optional sections.
 */
ConfigValue make_config_snapshot() {
  ConfigValue config = ConfigValue::object();
  config.set("config_version", ConfigValue::string("1.0.0"));
  config.set("sdk_version", ConfigValue::string("1.0.0"));
  for (const std::string_view section : {"integration", "workload", "dimensions", "execution",
                                         "profile", "measurement", "validation", "parallelism"}) {
    config.set(std::string(section), ConfigValue::object());
  }
  config.find("integration")->set("id", ConfigValue::string("app-a"));
  config.find("integration")->set("version", ConfigValue::string("1.2.3"));
  return config;
}

/**
 * @brief Build one measured MET-01 record with a finite aggregate.
 * @param value Measured execution time in seconds.
 * @return Metric record using the identifiers from METRICS §3.
 */
ConfigValue make_duration_metric(double value) {
  ConfigValue metric = ConfigValue::object();
  metric.set("id", ConfigValue::string("MET-01"));
  metric.set("name", ConfigValue::string("Execution time"));
  metric.set("value", ConfigValue::real(value));
  metric.set("unit", ConfigValue::string("s"));
  metric.set("status", ConfigValue::string("measured"));
  metric.set("source", ConfigValue::string("instrumented_dsp"));
  metric.set("method", ConfigValue::string("nearest_rank"));
  metric.set("reason", ConfigValue::string(""));
  return metric;
}

/**
 * @brief Build one complete run carrying a single duration sample.
 * @param run_id Stable run identifier.
 * @param value Run-level MET-01 value in seconds.
 * @return Complete run record for a comparison fixture.
 */
ConfigValue make_run(std::string run_id, double value) {
  ConfigValue run = ConfigValue::object();
  run.set("id", ConfigValue::string(std::move(run_id)));
  run.set("status", ConfigValue::string("complete"));
  run.set("failures", ConfigValue::array());
  ConfigValue metrics = ConfigValue::array();
  metrics.push_back(make_duration_metric(value));
  run.set("metrics", std::move(metrics));
  return run;
}

/**
 * @brief Build a valid stored result document with explicit host metadata.
 * @param session_id Session identifier.
 * @param cpu_model Recorded CPU model.
 * @param isolation Recorded isolation mode.
 * @param values Run-level MET-01 samples in seconds, one per repetition.
 * @return Complete v1 result document matching RESULTS_FORMAT §3.
 */
ConfigValue make_result_document(std::string session_id, std::string cpu_model,
                                 std::string isolation, const std::vector<double> &values) {
  ConfigValue document = ConfigValue::object();
  document.set("schema_version", ConfigValue::string(std::string(ztick::kResultSchemaVersion)));
  document.set("sdk_version", ConfigValue::string("1.0.0"));
  ConfigValue session = ConfigValue::object();
  session.set("id", ConfigValue::string(std::move(session_id)));
  document.set("session", std::move(session));
  document.set("config_snapshot", make_config_snapshot());
  document.set("build", ztick::build_metadata(ztick::BuildInfo{}));
  document.set("machine", ztick::make_metadata_object(
                              {ztick::metadata_value("cpu_model", std::move(cpu_model))}));
  document.set("environment", ztick::make_metadata_object(
                                  {ztick::metadata_value("isolation", std::move(isolation))}));
  ConfigValue runs = ConfigValue::array();
  for (std::size_t index = 0U; index < values.size(); ++index) {
    runs.push_back(make_run("run-" + std::to_string(index + 1U), values[index]));
  }
  document.set("runs", std::move(runs));
  document.set("comparisons", ConfigValue::array());
  document.set("warnings", ConfigValue::array());
  document.set("failures", ConfigValue::array());
  document.set("status", ConfigValue::string("complete"));
  return document;
}

/**
 * @brief Find one metric record in a comparison by identifier.
 * @param metrics Comparison metric array.
 * @param id Stable metric identifier to find.
 * @return Pointer to the record, or `nullptr` when absent.
 */
const ConfigValue *find_metric(const ConfigValue &metrics, std::string_view id) {
  const ConfigValue::Array *records = metrics.as_array();
  if (records == nullptr) {
    return nullptr;
  }
  for (const ConfigValue &record : *records) {
    if (string_member(record, "metric_id") == id) {
      return &record;
    }
  }
  return nullptr;
}

/**
 * @brief Parse one JSON output envelope emitted by `--format json`.
 * @param text Complete stdout text from dispatch.
 * @return Parsed envelope value.
 */
ConfigValue parse_envelope(std::string_view text) {
  const ztick::JsonReadResult parsed = ztick::json_read(text);
  REQUIRE(parsed.ok);
  REQUIRE(parsed.error.empty());
  return parsed.value;
}

/**
 * @brief Assert the fixed envelope header from RESULTS_FORMAT §12.
 * @param envelope Parsed envelope object.
 * @param command Expected stable command label.
 * @param status Expected status token from PROJECT_SPEC §3.3.
 * @param code Expected numeric exit code.
 */
void check_envelope(const ConfigValue &envelope, std::string_view command, std::string_view status,
                    std::int64_t code) {
  const ConfigValue::Object *members = envelope.as_object();
  REQUIRE(members != nullptr);
  REQUIRE(members->size() == 6U);
  CHECK(number_member(envelope, "schema_version") == 1.0);
  CHECK(string_member(envelope, "command") == command);
  CHECK(string_member(envelope, "status") == status);
  CHECK(number_member(envelope, "exit_code") == static_cast<double>(code));
  REQUIRE(envelope.find("data") != nullptr);
  REQUIRE(envelope.find("error") != nullptr);
  CHECK(envelope.find("error")->as_string().has_value());
}

} // namespace

/**
 * @brief Verify that the documented CLI commands work together end to end.
 */
TEST_CASE("phase4 CLI workflows run end to end", "[integration][phase4][cli]") {
  using ztick::cli::dispatch;
  using ztick::cli::ExitCode;

  const std::filesystem::path directory = scratch_directory("cli_workflows");
  const std::filesystem::path config_path = directory / "config.json";
  const std::filesystem::path result_a = directory / "result_a.json";
  const std::filesystem::path result_b = directory / "result_b.json";
  write_text_file(config_path, runnable_config_text());

  const ztick::cli::CommandResult help = dispatch({"--help"});
  CHECK(help.code == ExitCode::success);
  CHECK(help.output.find("Usage:") != std::string::npos);
  CHECK(help.error.empty());
  REQUIRE(help.data.find("text") != nullptr);

  const ztick::cli::CommandResult version = dispatch({"--version"});
  CHECK(version.code == ExitCode::success);
  CHECK(version.output.find("ztick-bench ") == 0U);

  const ztick::cli::CommandResult profiles = dispatch({"list"});
  CHECK(profiles.code == ExitCode::success);
  CHECK(profiles.output.find("Built-in profiles:") == 0U);
  for (const ztick::cli::ProfileDescription &profile : ztick::cli::profile_descriptions()) {
    CHECK(profiles.output.find(profile.name) != std::string::npos);
  }
  const ConfigValue *listed = profiles.data.find("profiles");
  REQUIRE(listed != nullptr);
  REQUIRE(listed->as_array() != nullptr);
  CHECK(listed->as_array()->size() == ztick::cli::profile_descriptions().size());

  const ztick::cli::CommandResult system = dispatch({"system"});
  CHECK(system.code == ExitCode::success);
  CHECK(system.output.find("availability states") != std::string::npos);

  const std::string config_text = config_path.string();
  const std::string output_a = result_a.string();
  const std::string output_b = result_b.string();
  const ztick::cli::CommandResult run_a =
      dispatch({"run", config_text, "--set", "execution.iterations=4", "--output", output_a});
  REQUIRE(run_a.code == ExitCode::success);
  CHECK(run_a.output.find("run complete") != std::string::npos);
  CHECK(run_a.error.empty());
  REQUIRE(run_a.data.find("document") != nullptr);
  REQUIRE(std::filesystem::exists(result_a));

  const ztick::ResultParseResult stored = ztick::read_result_document(result_a);
  REQUIRE(stored.ok);
  const ConfigValue *snapshot = stored.document.find("config_snapshot");
  REQUIRE(snapshot != nullptr);
  const ConfigValue *execution = snapshot->find("execution");
  REQUIRE(execution != nullptr);
  CHECK(number_member(*execution, "iterations") == 4.0);

  const ztick::cli::CommandResult run_b =
      dispatch({"run", config_text, "--set=execution.iterations=8", "--output", output_b});
  REQUIRE(run_b.code == ExitCode::success);
  REQUIRE(std::filesystem::exists(result_b));

  const std::string report_path = output_a;
  const ztick::cli::CommandResult report = dispatch({"report", report_path});
  CHECK(report.code == ExitCode::success);
  CHECK(report.output.find("result report") != std::string::npos);
  CHECK(report.output.find("MET-01") != std::string::npos);
  CHECK(string_member(report.data, "derived_status") == "complete");

  const ztick::cli::CommandResult compare = dispatch({"compare", report_path, output_b});
  CHECK(compare.code == ExitCode::success);
  CHECK(compare.output.find("stored result comparison") != std::string::npos);
  CHECK(compare.output.find("classification:") != std::string::npos);
  REQUIRE(compare.data.find("comparison") != nullptr);

  const ztick::cli::CommandResult plugin_list = dispatch({"plugin", "list"});
  CHECK(plugin_list.code == ExitCode::success);
  CHECK(plugin_list.output.find("discovered plugins") != std::string::npos);
  REQUIRE(plugin_list.data.find("plugins") != nullptr);
  REQUIRE(plugin_list.data.find("directories") != nullptr);
  REQUIRE(plugin_list.data.find("trust") != nullptr);

  const std::string missing_plugin = (directory / "no-such-module.so").string();
  const ztick::cli::CommandResult plugin_inspect = dispatch({"plugin", "inspect", missing_plugin});
  CHECK(plugin_inspect.code == ExitCode::failure);
  CHECK(plugin_inspect.output.find("plugin inspection") != std::string::npos);
  CHECK(plugin_inspect.output.find(missing_plugin) != std::string::npos);

  const ztick::cli::CommandResult unknown = dispatch({"benchmark"});
  CHECK(unknown.code == ExitCode::usage_error);
  CHECK(unknown.error.find("unknown command") != std::string::npos);

  const std::string missing_result = (directory / "missing.json").string();
  const ztick::cli::CommandResult missing = dispatch({"report", missing_result});
  CHECK(missing.code == ExitCode::failure);
  CHECK_FALSE(missing.error.empty());
}

/**
 * @brief Verify that portable configs round-trip and host differences are reported.
 */
TEST_CASE("phase4 portable configs surface host differences",
          "[integration][phase4][portability]") {
  const ztick::ConfigParseResult parsed = ztick::parse_config_document(runnable_config_text());
  REQUIRE(parsed.ok);
  REQUIRE(string_member(parsed.document, "config_version") == ztick::kConfigSchemaVersion);
  CHECK(string_member(parsed.document, "sdk_version") == "1.0.0");

  const ztick::ConfigWriteResult written = ztick::write_config_document(parsed.document);
  REQUIRE(written.ok);
  const ztick::JsonReadResult reparsed = ztick::json_read(written.text);
  REQUIRE(reparsed.ok);
  CHECK(reparsed.value == parsed.document);
  REQUIRE(ztick::parse_config_document(written.text).ok);

  ConfigValue future_schema = parsed.document;
  future_schema.set("config_version", ConfigValue::string("2.0.0"));
  CHECK_FALSE(ztick::validate_config_document(future_schema));
  ConfigValue future_sdk = parsed.document;
  future_sdk.set("sdk_version", ConfigValue::string("2.0.0"));
  CHECK_FALSE(ztick::validate_config_document(future_sdk));

  // BR §13: reusing a config on another host yields explicit warnings instead of
  // silently changing the benchmark.
  const ConfigValue host_a = make_result_document("host-a", "Alpha CPU", "in-process", {0.1});
  const ConfigValue host_b = make_result_document("host-b", "Beta CPU", "out-of-process", {0.1});
  const ztick::MetadataComparison different = ztick::compare_metadata(host_a, host_b);
  CHECK_FALSE(different.comparable);
  CHECK(different.difference_count >= 2U);
  REQUIRE(different.warnings.as_array() != nullptr);
  CHECK(different.warnings.as_array()->size() == different.difference_count);

  const ztick::MetadataComparison same = ztick::compare_metadata(host_a, host_a);
  CHECK(same.comparable);
  CHECK(same.difference_count == 0U);

  // Identical explicit states stay comparable: an unsupported capability is still
  // reported as a field on both sides rather than being dropped.
  ConfigValue unsupported_a = host_a;
  ConfigValue unsupported_b = host_b;
  const ConfigValue gpu =
      ztick::make_metadata_object({ztick::metadata_unsupported("gpu", "no driver in this build")});
  const ConfigValue environment = *host_a.find("environment");
  REQUIRE(unsupported_a.set("machine", gpu));
  REQUIRE(unsupported_b.set("machine", gpu));
  REQUIRE(unsupported_a.set("environment", environment));
  REQUIRE(unsupported_b.set("environment", environment));
  const ztick::MetadataComparison both_unsupported =
      ztick::compare_metadata(unsupported_a, unsupported_b);
  CHECK(both_unsupported.comparable);
  CHECK(both_unsupported.difference_count == 0U);
}

/**
 * @brief Verify comparisons retain evidence and classify against independent values.
 */
TEST_CASE("phase4 comparisons retain evidence and classify regressions",
          "[integration][phase4][comparison]") {
  ztick::ComparisonPolicy policy;
  policy.bootstrap_iterations = 256U;
  const ConfigValue baseline =
      make_result_document("base", "Alpha CPU", "in-process", {0.100, 0.101, 0.099, 0.100, 0.100});
  const ConfigValue candidate =
      make_result_document("cand", "Alpha CPU", "in-process", {0.150, 0.151, 0.149, 0.150, 0.150});

  const auto comparison = ztick::compare_result_documents_with_policy(
      baseline, candidate, ztick::ComparisonReferences{"base.json", "cand.json"}, policy);
  REQUIRE(comparison.has_value());
  CHECK(string_member(*comparison, "classification") == "regression");

  // METRICS §6: independently compute means, absolute delta and relative change
  // from the fixture values rather than reading the implementation's output.
  const double baseline_mean = (0.100 + 0.101 + 0.099 + 0.100 + 0.100) / 5.0;
  const double candidate_mean = (0.150 + 0.151 + 0.149 + 0.150 + 0.150) / 5.0;
  const double expected_delta = candidate_mean - baseline_mean;
  const double expected_percent = expected_delta / baseline_mean * 100.0;

  const ConfigValue *duration = find_metric(*comparison->find("metrics"), "MET-01");
  REQUIRE(duration != nullptr);
  CHECK(string_member(*duration, "polarity") == "lower_is_better");
  CHECK(number_member(*duration, "baseline_mean") == Catch::Approx(baseline_mean));
  CHECK(number_member(*duration, "candidate_mean") == Catch::Approx(candidate_mean));
  CHECK(number_member(*duration, "aggregate_delta") == Catch::Approx(expected_delta));
  CHECK(number_member(*duration, "aggregate_relative_change_percent") ==
        Catch::Approx(expected_percent));
  REQUIRE(duration->find("baseline_samples") != nullptr);
  REQUIRE(duration->find("candidate_samples") != nullptr);
  CHECK(duration->find("baseline_samples")->as_array()->size() == 5U);
  CHECK(duration->find("candidate_samples")->as_array()->size() == 5U);

  // A significant but sub-threshold change is retained as a raw difference.
  ztick::ComparisonPolicy sub_policy = policy;
  sub_policy.threshold_percent = 5.0;
  const ConfigValue small =
      make_result_document("small", "Alpha CPU", "in-process", {1.000, 1.001, 0.999, 1.000, 1.000});
  const ConfigValue larger = make_result_document("larger", "Alpha CPU", "in-process",
                                                  {1.010, 1.011, 1.009, 1.010, 1.010});
  const auto sub_threshold = ztick::compare_result_documents_with_policy(
      small, larger, ztick::ComparisonReferences{"small.json", "larger.json"}, sub_policy);
  REQUIRE(sub_threshold.has_value());
  CHECK(string_member(*sub_threshold, "classification") == "raw_difference");
  CHECK(string_member(*sub_threshold, "threshold_status") == "");

  // A different host produces comparability warnings beside the numeric evidence.
  const ConfigValue other_host =
      make_result_document("other", "Beta CPU", "in-process", {0.100, 0.101, 0.099, 0.100, 0.100});
  const auto flagged = ztick::compare_result_documents_with_policy(
      baseline, other_host, ztick::ComparisonReferences{"base.json", "other.json"}, policy);
  REQUIRE(flagged.has_value());
  const ConfigValue *warnings = flagged->find("comparability_warnings");
  REQUIRE(warnings != nullptr);
  REQUIRE(warnings->as_array() != nullptr);
  CHECK_FALSE(warnings->as_array()->empty());
}

/**
 * @brief Verify the CI envelope is stable for every documented outcome.
 */
TEST_CASE("phase4 CI envelope carries stable outcomes", "[integration][phase4][ci]") {
  using ztick::cli::ExitCode;
  using ztick::cli::OutputFormat;

  // Tokens and codes are fixed by PROJECT_SPEC §3.3 and RESULTS_FORMAT §12.
  CHECK(ztick::cli::exit_code_token(ExitCode::success) == "success");
  CHECK(ztick::cli::exit_code_token(ExitCode::usage_error) == "usage_error");
  CHECK(ztick::cli::exit_code_token(ExitCode::unavailable) == "unavailable");
  CHECK(ztick::cli::exit_code_token(ExitCode::failure) == "failure");
  CHECK(ztick::cli::exit_code_token(ExitCode::regression) == "regression");

  const auto separated = ztick::cli::select_output_format({"--format", "json", "list"});
  REQUIRE(separated.has_value());
  CHECK(separated->format == OutputFormat::json);
  REQUIRE(separated->args.size() == 1U);
  CHECK(separated->args.front() == "list");
  const auto inline_value = ztick::cli::select_output_format({"list", "--format=json"});
  REQUIRE(inline_value.has_value());
  CHECK(inline_value->format == OutputFormat::json);
  CHECK_FALSE(ztick::cli::select_output_format({"--format", "yaml", "list"}).has_value());
  CHECK_FALSE(
      ztick::cli::select_output_format({"--format", "json", "--format=json", "list"}).has_value());

  const ztick::cli::CommandResult success = ztick::cli::dispatch({"--format", "json", "list"});
  CHECK(success.code == ExitCode::success);
  CHECK(success.error.empty());
  const ConfigValue success_envelope = parse_envelope(success.output);
  check_envelope(success_envelope, "list", "success", 0);
  REQUIRE(success_envelope.find("data")->find("profiles") != nullptr);

  const ztick::cli::CommandResult usage = ztick::cli::dispatch({"--format=json", "benchmark"});
  CHECK(usage.code == ExitCode::usage_error);
  CHECK(usage.error.empty());
  const ConfigValue usage_envelope = parse_envelope(usage.output);
  check_envelope(usage_envelope, "benchmark", "usage_error", 1);
  CHECK(string_member(usage_envelope, "error").find("unknown command") != std::string::npos);

  const std::filesystem::path directory = scratch_directory("ci_envelope");
  const std::string missing_result = (directory / "missing.json").string();
  const ztick::cli::CommandResult failure =
      ztick::cli::dispatch({"--format=json", "report", missing_result});
  CHECK(failure.code == ExitCode::failure);
  CHECK(failure.error.empty());
  const ConfigValue failure_envelope = parse_envelope(failure.output);
  check_envelope(failure_envelope, "report", "failure", 3);
  CHECK_FALSE(string_member(failure_envelope, "error").empty());

  const std::filesystem::path baseline = directory / "baseline.json";
  const std::filesystem::path candidate = directory / "candidate.json";
  const ConfigValue base_document =
      make_result_document("base", "Alpha CPU", "in-process", {0.100, 0.101, 0.099, 0.100, 0.100});
  const ConfigValue candidate_document =
      make_result_document("cand", "Alpha CPU", "in-process", {0.150, 0.151, 0.149, 0.150, 0.150});
  REQUIRE(ztick::write_result_document(baseline, base_document).ok);
  REQUIRE(ztick::write_result_document(candidate, candidate_document).ok);

  const std::string base_text = baseline.string();
  const std::string candidate_text = candidate.string();
  const ztick::cli::CommandResult regression_result =
      ztick::cli::dispatch({"--format=json", "compare", base_text, candidate_text, "--threshold=1",
                            "--fail-on-regression"});
  CHECK(regression_result.code == ExitCode::regression);
  CHECK(regression_result.error.empty());
  const ConfigValue regression_envelope = parse_envelope(regression_result.output);
  check_envelope(regression_envelope, "compare", "regression", 4);
  const ConfigValue *comparison = regression_envelope.find("data")->find("comparison");
  REQUIRE(comparison != nullptr);
  CHECK(string_member(*comparison, "classification") == "regression");
  CHECK(bool_member(*regression_envelope.find("data"), "regression_detected"));

  const ztick::cli::CommandResult no_flag = ztick::cli::dispatch(
      {"--format=json", "compare", base_text, candidate_text, "--threshold=1"});
  CHECK(no_flag.code == ExitCode::success);
  const ConfigValue no_flag_envelope = parse_envelope(no_flag.output);
  check_envelope(no_flag_envelope, "compare", "success", 0);
  // Detection is reported even without the flag; only the exit code is gated.
  CHECK(bool_member(*no_flag_envelope.find("data"), "regression_detected"));
}
