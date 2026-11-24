// SPDX-License-Identifier: MIT
/**
 * @file result_commands_test.cpp
 * @brief Catch2 tests for `report` rendering and `compare` dispatch.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/comparison.hpp>
#include <ztick/config_value.hpp>
#include <ztick/metadata.hpp>
#include <ztick/result_commands.hpp>
#include <ztick/result_document.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ztick::ConfigValue;
using ztick::ResultParseResult;
using ztick::cli::ExitCode;

/**
 * @brief Require that text contains a stable fragment.
 * @param text Text to search.
 * @param fragment Fragment expected inside @p text.
 */
void require_contains(const std::string &text, std::string_view fragment) {
  INFO(std::string("missing fragment: ") + std::string(fragment));
  INFO(std::string("text: ") + text);
  REQUIRE(text.find(fragment) != std::string::npos);
}

/**
 * @brief Build the minimal valid config snapshot required by a result document.
 * @return Portable v1 config object with empty optional sections.
 */
ConfigValue make_config() {
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
 * @brief Build one classified warning or failure record.
 * @param category Classification token.
 * @param message Human-readable explanation.
 * @return Classified record.
 */
ConfigValue classified(std::string category, std::string message) {
  ConfigValue record = ConfigValue::object();
  record.set("category", ConfigValue::string(std::move(category)));
  record.set("message", ConfigValue::string(std::move(message)));
  return record;
}

/**
 * @brief Build one numeric metric record.
 * @param id Stable metric identifier.
 * @param name Human-readable metric name.
 * @param value Measured value.
 * @return Metric record with a finite numeric aggregate.
 */
ConfigValue make_metric(std::string id, std::string name, double value) {
  ConfigValue metric = ConfigValue::object();
  metric.set("id", ConfigValue::string(std::move(id)));
  metric.set("name", ConfigValue::string(std::move(name)));
  metric.set("value", ConfigValue::real(value));
  metric.set("unit", ConfigValue::string("s"));
  metric.set("status", ConfigValue::string("measured"));
  metric.set("source", ConfigValue::string("instrumented_dsp"));
  metric.set("method", ConfigValue::string("nearest_rank"));
  metric.set("reason", ConfigValue::string(""));
  return metric;
}

/**
 * @brief Build one explicitly unavailable metric record.
 * @return Metric record whose value is null and whose reason is retained.
 */
ConfigValue make_unavailable_metric() {
  ConfigValue metric = ConfigValue::object();
  metric.set("id", ConfigValue::string("MET-99"));
  metric.set("name", ConfigValue::string("Sampler status"));
  metric.set("value", ConfigValue::null());
  metric.set("unit", ConfigValue::string("s"));
  metric.set("status", ConfigValue::string("unavailable"));
  metric.set("source", ConfigValue::string("monitor"));
  metric.set("method", ConfigValue::string("none"));
  metric.set("reason", ConfigValue::string("sampler offline"));
  return metric;
}

/**
 * @brief Build a valid stored result document for rendering tests.
 * @param session_id Session identifier.
 * @param cpu_model Recorded CPU model.
 * @param metric_value Value of MET-01.
 * @return Complete v1 result document.
 */
ConfigValue make_document(std::string session_id, std::string cpu_model, double metric_value) {
  ConfigValue document = ConfigValue::object();
  document.set("schema_version", ConfigValue::string(std::string(ztick::kResultSchemaVersion)));
  document.set("sdk_version", ConfigValue::string("1.0.0"));
  ConfigValue session = ConfigValue::object();
  session.set("id", ConfigValue::string(std::move(session_id)));
  session.set("started_at", ConfigValue::string("2026-11-24T10:00:00Z"));
  session.set("ended_at", ConfigValue::string("2026-11-24T10:00:01Z"));
  document.set("session", std::move(session));
  document.set("config_snapshot", make_config());
  document.set("build", ztick::make_metadata_object({ztick::metadata_value("compiler", "GCC 15")}));
  document.set("machine", ztick::make_metadata_object(
                              {ztick::metadata_value("cpu_model", std::move(cpu_model)),
                               ztick::metadata_unavailable("cpu_frequency_hz", "source offline")}));
  document.set("environment",
               ztick::make_metadata_object({ztick::metadata_value("isolation", "in-process")}));

  ConfigValue run = ConfigValue::object();
  run.set("id", ConfigValue::string("run-1"));
  run.set("status", ConfigValue::string("complete"));
  ConfigValue point = ConfigValue::object();
  point.set("block_size", ConfigValue::integer(128));
  run.set("test_point", std::move(point));
  ConfigValue metrics = ConfigValue::array();
  metrics.push_back(make_metric("MET-01", "Execution time", metric_value));
  metrics.push_back(make_unavailable_metric());
  run.set("metrics", std::move(metrics));
  run.set("warnings", ConfigValue::array());
  run.set("failures", ConfigValue::array());
  ConfigValue runs = ConfigValue::array();
  runs.push_back(std::move(run));
  document.set("runs", std::move(runs));

  document.set("comparisons", ConfigValue::array());
  ConfigValue warnings = ConfigValue::array();
  warnings.push_back(classified("execution", "built-in reference workload used"));
  document.set("warnings", std::move(warnings));
  document.set("failures", ConfigValue::array());
  document.set("status", ConfigValue::string("complete"));
  return document;
}

/**
 * @brief Build a run record carrying one run-level metric sample.
 * @param run_id Stable run identifier.
 * @param metric_id Stable metric identifier.
 * @param value Measured value of @p metric_id.
 * @return Complete run record.
 */
ConfigValue make_metric_run(std::string run_id, std::string_view metric_id, double value) {
  ConfigValue run = ConfigValue::object();
  run.set("id", ConfigValue::string(std::move(run_id)));
  run.set("status", ConfigValue::string("complete"));
  ConfigValue point = ConfigValue::object();
  point.set("block_size", ConfigValue::integer(128));
  run.set("test_point", std::move(point));
  ConfigValue metrics = ConfigValue::array();
  ConfigValue metric = make_metric(std::string(metric_id), "Execution time", value);
  metric.set("unit", ConfigValue::string(metric_id == "MET-11" ? "frames/s" : "s"));
  metrics.push_back(std::move(metric));
  run.set("metrics", std::move(metrics));
  run.set("warnings", ConfigValue::array());
  run.set("failures", ConfigValue::array());
  return run;
}

/**
 * @brief Build a result document with one repetition per metric sample.
 * @param session_id Session identifier.
 * @param cpu_model Recorded CPU model.
 * @param metric_id Metric identifier to populate.
 * @param values Run-level values in stored order.
 * @return Complete v1 result document.
 */
ConfigValue make_multi_run_document(std::string session_id, std::string cpu_model,
                                    std::string_view metric_id, const std::vector<double> &values) {
  ConfigValue document = make_document(std::move(session_id), std::move(cpu_model), 0.0);
  ConfigValue runs = ConfigValue::array();
  for (std::size_t index = 0U; index < values.size(); ++index) {
    runs.push_back(make_metric_run("run-" + std::to_string(index + 1U), metric_id, values[index]));
  }
  document.set("runs", std::move(runs));
  return document;
}

/**
 * @brief Create a unique temporary path for a result-document file test.
 * @param suffix File name suffix.
 * @return Path inside the system temporary directory.
 */
std::filesystem::path temporary_result_path(std::string_view suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ztick_result_commands_" + std::string(suffix) + "_" + std::to_string(stamp) + ".json");
}

/**
 * @brief Verify the report renders stored evidence with explicit states.
 */
TEST_CASE("report rendering preserves evidence and availability states", "[cli][results]") {
  const ConfigValue document = make_document("session-42", "Alpha CPU", 0.001);
  const std::string text = ztick::cli::render_result_report(document);

  require_contains(text, "ztick-bench: result report\n");
  require_contains(text, "session: session-42\n");
  require_contains(text, "status: complete\n");
  require_contains(text, "build.compiler: value (GCC 15)\n");
  require_contains(text, "machine.cpu_model: value (Alpha CPU)\n");
  require_contains(text, "machine.cpu_frequency_hz: unavailable (source offline)\n");
  require_contains(text, "MET-01 Execution time: 0.001 s [measured]\n");
  require_contains(text, "MET-99 Sampler status: <null> s [unavailable] (sampler offline)\n");
  require_contains(text, "warnings: 1\n");
  require_contains(text, "execution | built-in reference workload used\n");
}

/**
 * @brief Verify a stale stored status cannot be presented as a successful session.
 */
TEST_CASE("report derives status from run evidence", "[cli][results]") {
  ConfigValue document = make_document("session-43", "Alpha CPU", 0.001);
  ConfigValue &run = document.find("runs")->as_array()->front();
  run.set("status", ConfigValue::string("failed"));
  run.find("failures")->push_back(classified("workload_failure", "validation checks failed"));

  const std::string text = ztick::cli::render_result_report(document);
  require_contains(text, "status: incomplete\n");
  require_contains(text, "stored_status: complete\n");
  require_contains(text,
                   "status_mismatch: stored status is not supported by recorded run evidence");
  require_contains(text, "run[0] run-1: failed\n");
}

/**
 * @brief Verify report argument and reader failures are deterministic.
 */
TEST_CASE("report rejects bad arguments and unreadable results", "[cli][results]") {
  bool called = false;
  const ztick::cli::ResultReader reader = [&called](const std::filesystem::path &) {
    called = true;
    return ResultParseResult{};
  };

  const auto missing = ztick::cli::execute_report({}, reader);
  REQUIRE(missing.code == ExitCode::usage_error);
  require_contains(missing.error, "requires exactly one result path");

  const auto extra = ztick::cli::execute_report({"a.json", "b.json"}, reader);
  REQUIRE(extra.code == ExitCode::usage_error);
  REQUIRE_FALSE(called);

  const auto no_reader = ztick::cli::execute_report({"a.json"}, ztick::cli::ResultReader{});
  REQUIRE(no_reader.code == ExitCode::failure);
  require_contains(no_reader.error, "reader is missing");

  const auto failed = ztick::cli::execute_report({"missing.json"}, reader);
  REQUIRE(failed.code == ExitCode::failure);
  REQUIRE(called);
  require_contains(failed.error, "cannot read report result 'missing.json'");
}

/**
 * @brief Verify the production report reader reads an atomically written file.
 */
TEST_CASE("report reads a stored result file", "[cli][results]") {
  const ConfigValue document = make_document("session-file", "Alpha CPU", 0.002);
  const std::filesystem::path path = temporary_result_path("report");
  REQUIRE(write_result_document(path, document).ok);

  const auto result = ztick::cli::execute_report({path.string()});
  REQUIRE(result.code == ExitCode::success);
  REQUIRE(result.error.empty());
  require_contains(result.output, "session: session-file\n");
  require_contains(result.output, "MET-01 Execution time: 0.002 s [measured]\n");
  std::filesystem::remove(path);
}

/**
 * @brief Verify a core comparison record renders raw differences without regressions.
 */
TEST_CASE("comparison rendering reports raw differences and warnings", "[cli][results]") {
  const ConfigValue baseline = make_document("session-a", "Alpha CPU", 0.002);
  ConfigValue candidate = make_document("session-b", "Beta CPU", 0.0015);
  candidate.find("runs")->as_array()->front().find("metrics")->push_back(
      make_metric("MET-50", "Extra metric", 42.0));
  const auto record = ztick::compare_result_documents(
      baseline, candidate, ztick::ComparisonReferences{"base.json", "cand.json"});
  REQUIRE(record.has_value());

  const std::string text = ztick::cli::render_result_comparison(*record);
  require_contains(text, "ztick-bench: stored result comparison\n");
  require_contains(text, "level: machine_to_machine\n");
  require_contains(text, "classification: raw_difference\n");
  require_contains(text, "threshold_percent: <null> (not_applicable)\n");
  require_contains(text, "comparability_warnings: 1\n");
  require_contains(text, "machine.cpu_model: value(Alpha CPU) vs value(Beta CPU)");
  require_contains(text, "delta=-0.0005 s relative_change=-25%");
  require_contains(text, "MET-99 Sampler status: baseline=<null> [unavailable]");
  require_contains(text, "MET-50 Extra metric: baseline=<null> [<absent>] candidate=42.0");
  require_contains(text, "contribution=<unavailable> (candidate_only)");
}

/**
 * @brief Verify compare policy options parse in either syntax and any order.
 */
TEST_CASE("compare parses threshold policy options", "[cli][results][p49]") {
  std::string error;
  const auto invocation = ztick::cli::parse_compare_arguments(
      {"--threshold", "2.5", "--alpha=0.01", "base.json", "--sided=one", "cand.json"}, &error);

  REQUIRE(invocation.has_value());
  REQUIRE(error.empty());
  REQUIRE(invocation->baseline == std::filesystem::path("base.json"));
  REQUIRE(invocation->candidate == std::filesystem::path("cand.json"));
  REQUIRE(invocation->policy.threshold_percent == 2.5);
  REQUIRE(invocation->policy.alpha == 0.01);
  REQUIRE(invocation->policy.sidedness == ztick::SignificanceSidedness::one_sided);
  REQUIRE_FALSE(invocation->fail_on_regression);
}

/**
 * @brief Verify malformed compare policy options are usage errors.
 */
TEST_CASE("compare rejects malformed policy options", "[cli][results][p49]") {
  const auto check_invalid = [](std::vector<std::string_view> args, std::string_view fragment) {
    std::string error;
    REQUIRE_FALSE(ztick::cli::parse_compare_arguments(args, &error).has_value());
    require_contains(error, fragment);
  };

  check_invalid({"--threshold", "2", "a.json"}, "requires a baseline and a candidate path");
  check_invalid({"--threshold", "2", "--threshold=3", "a.json", "b.json"}, "more than once");
  check_invalid({"--unknown", "a.json", "b.json"}, "unknown option");
  check_invalid({"--threshold=-1", "a.json", "b.json"}, "non-negative percentage");
  check_invalid({"--alpha=1", "a.json", "b.json"}, "greater than 0 and less than 1");
  check_invalid({"--sided=both", "a.json", "b.json"}, "accepts 'one' or 'two'");
  check_invalid({"--alpha", "a.json", "b.json"}, "finite number");
  check_invalid({"--fail-on-regression=yes", "a.json", "b.json"}, "takes no value");
  check_invalid({"--fail-on-regression", "--fail-on-regression", "a.json", "b.json"},
                "more than once");
}

/**
 * @brief Verify classified comparison records render threshold and significance detail.
 */
TEST_CASE("comparison rendering reports classified regressions", "[cli][results][p49]") {
  ztick::ComparisonPolicy policy;
  policy.bootstrap_iterations = 256U;
  const ConfigValue baseline = make_multi_run_document("session-a", "Alpha CPU", "MET-01",
                                                       {0.100, 0.101, 0.099, 0.100, 0.100});
  const ConfigValue candidate = make_multi_run_document("session-b", "Alpha CPU", "MET-01",
                                                        {0.150, 0.151, 0.149, 0.150, 0.150});
  const auto record = ztick::compare_result_documents_with_policy(
      baseline, candidate, ztick::ComparisonReferences{"base.json", "cand.json"}, policy);
  REQUIRE(record.has_value());

  const std::string text = ztick::cli::render_result_comparison(*record);
  require_contains(text, "classification: regression\n");
  require_contains(text, "threshold_percent: 1.0 (applied)\n");
  require_contains(text, "statistics: applied (");
  require_contains(text, "statistics_detail: method=bootstrap_percentile alpha=0.05 sided=two "
                         "iterations=256 seed=1592594996 multiplicity=unadjusted "
                         "metrics_compared=1 metrics_tested=1 outliers_removed=0\n");
  require_contains(text, "significance: applied (");
  require_contains(text, "significance_detail: method=bootstrap_percentile regressions=1 "
                         "improvements=0 raw_differences=0 within_variability=0\n");
  require_contains(text, "classification=regression polarity=lower_is_better "
                         "significance=applied significant=true interval=[");
  require_contains(text, "samples=5/5\n");
}

/**
 * @brief Verify compare argument, reader and comparison failures are deterministic.
 */
TEST_CASE("compare rejects bad arguments and unreadable results", "[cli][results]") {
  bool called = false;
  const ztick::cli::ResultReader reader = [&called](const std::filesystem::path &) {
    called = true;
    return ResultParseResult{};
  };

  const auto missing = ztick::cli::execute_compare({"a.json"}, reader);
  REQUIRE(missing.code == ExitCode::usage_error);
  require_contains(missing.error, "requires a baseline and a candidate path");

  const auto extra = ztick::cli::execute_compare({"a.json", "b.json", "c.json"}, reader);
  REQUIRE(extra.code == ExitCode::usage_error);
  REQUIRE_FALSE(called);

  const auto no_reader =
      ztick::cli::execute_compare({"a.json", "b.json"}, ztick::cli::ResultReader{});
  REQUIRE(no_reader.code == ExitCode::failure);
  require_contains(no_reader.error, "reader is missing");

  const auto failed = ztick::cli::execute_compare({"a.json", "b.json"}, reader);
  REQUIRE(failed.code == ExitCode::failure);
  require_contains(failed.error, "cannot read baseline result 'a.json'");
}

/**
 * @brief Verify compare reads two stored result files through the production reader.
 */
TEST_CASE("compare reads two stored result files", "[cli][results]") {
  const std::filesystem::path baseline_path = temporary_result_path("base");
  const std::filesystem::path candidate_path = temporary_result_path("cand");
  REQUIRE(write_result_document(baseline_path, make_document("session-a", "Alpha CPU", 0.002)).ok);
  REQUIRE(write_result_document(candidate_path, make_document("session-b", "Alpha CPU", 0.001)).ok);

  const auto result =
      ztick::cli::execute_compare({baseline_path.string(), candidate_path.string()});
  REQUIRE(result.code == ExitCode::success);
  REQUIRE(result.error.empty());
  require_contains(result.output, "baseline: " + baseline_path.string() + " session=session-a");
  require_contains(result.output, "candidate: " + candidate_path.string() + " session=session-b");
  require_contains(result.output, "level: run_to_run\n");
  std::filesystem::remove(baseline_path);
  std::filesystem::remove(candidate_path);
}

/**
 * @brief Verify execute_compare forwards CLI policy options to the core classifier.
 */
TEST_CASE("compare applies command-line policy options", "[cli][results][p49]") {
  const std::filesystem::path baseline_path = temporary_result_path("base_policy");
  const std::filesystem::path candidate_path = temporary_result_path("cand_policy");
  REQUIRE(write_result_document(baseline_path,
                                make_multi_run_document("session-a", "Alpha CPU", "MET-01",
                                                        {0.100, 0.101, 0.099, 0.100, 0.100}))
              .ok);
  REQUIRE(write_result_document(candidate_path,
                                make_multi_run_document("session-b", "Alpha CPU", "MET-01",
                                                        {0.150, 0.151, 0.149, 0.150, 0.150}))
              .ok);

  const auto result =
      ztick::cli::execute_compare({baseline_path.string(), "--threshold=40", "--alpha=0.01",
                                   "--sided=two", candidate_path.string()});
  REQUIRE(result.code == ExitCode::success);
  REQUIRE(result.error.empty());
  require_contains(result.output, "classification: regression\n");
  require_contains(result.output, "threshold_percent: 40.0 (applied)\n");
  require_contains(result.output, "alpha=0.01");
  require_contains(result.output, "sided=two");
  require_contains(result.output, "classification=regression");

  std::filesystem::remove(baseline_path);
  std::filesystem::remove(candidate_path);
}

/**
 * @brief Verify the explicit regression flag changes only the CI exit behavior.
 */
TEST_CASE("compare fail-on-regression returns the CI regression code", "[cli][results][p410]") {
  const std::filesystem::path baseline_path = temporary_result_path("base_exit");
  const std::filesystem::path candidate_path = temporary_result_path("cand_exit");
  REQUIRE(write_result_document(baseline_path,
                                make_multi_run_document("session-a", "Alpha CPU", "MET-01",
                                                        {0.100, 0.101, 0.099, 0.100, 0.100}))
              .ok);
  REQUIRE(write_result_document(candidate_path,
                                make_multi_run_document("session-b", "Alpha CPU", "MET-01",
                                                        {0.150, 0.151, 0.149, 0.150, 0.150}))
              .ok);

  ConfigValue default_data = ConfigValue::null();
  const auto default_result =
      ztick::cli::execute_compare({baseline_path.string(), candidate_path.string()},
                                  ztick::cli::default_result_reader(), &default_data);
  REQUIRE(default_result.code == ExitCode::success);
  REQUIRE(default_data.find("regression_detected")->as_boolean() == true);
  REQUIRE(default_data.find("comparison")->find("classification")->as_string() == "regression");

  ConfigValue flagged_data = ConfigValue::null();
  const auto flagged_result = ztick::cli::execute_compare(
      {baseline_path.string(), "--fail-on-regression", candidate_path.string()},
      ztick::cli::default_result_reader(), &flagged_data);
  REQUIRE(flagged_result.code == ExitCode::regression);
  REQUIRE(flagged_result.error.empty());
  REQUIRE(flagged_data.find("regression_detected")->as_boolean() == true);
  require_contains(flagged_result.output, "classification: regression\n");

  std::filesystem::remove(baseline_path);
  std::filesystem::remove(candidate_path);
}

} // namespace
