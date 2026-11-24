// SPDX-License-Identifier: MIT
/**
 * @file comparison_test.cpp
 * @brief Catch2 tests for core-owned comparison of stored result documents.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/comparison.hpp>
#include <ztick/config_value.hpp>
#include <ztick/metadata.hpp>
#include <ztick/result_document.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ztick::ConfigValue;

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
 * @brief Build one numeric metric record.
 * @param id Stable metric identifier.
 * @param value Measured value.
 * @return Metric record with a finite numeric aggregate.
 */
ConfigValue make_metric(std::string id, double value) {
  ConfigValue metric = ConfigValue::object();
  metric.set("id", ConfigValue::string(std::move(id)));
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
 * @brief Build one explicitly unavailable metric record.
 * @return Metric record whose value is null and whose reason is retained.
 */
ConfigValue make_unavailable_metric() {
  ConfigValue metric = ConfigValue::object();
  metric.set("id", ConfigValue::string("MET-99"));
  metric.set("name", ConfigValue::string("Unavailable metric"));
  metric.set("value", ConfigValue::null());
  metric.set("unit", ConfigValue::string("s"));
  metric.set("status", ConfigValue::string("unavailable"));
  metric.set("source", ConfigValue::string("monitor"));
  metric.set("method", ConfigValue::string("none"));
  metric.set("reason", ConfigValue::string("source offline"));
  return metric;
}

/**
 * @brief Build one complete run record.
 * @param run_id Stable run identifier.
 * @param metric_value Value of MET-01.
 * @return Run record with a numeric and an unavailable metric.
 */
ConfigValue make_run(std::string run_id, double metric_value) {
  ConfigValue run = ConfigValue::object();
  run.set("id", ConfigValue::string(std::move(run_id)));
  run.set("status", ConfigValue::string("complete"));
  run.set("failures", ConfigValue::array());
  ConfigValue metrics = ConfigValue::array();
  metrics.push_back(make_metric("MET-01", metric_value));
  metrics.push_back(make_unavailable_metric());
  run.set("metrics", std::move(metrics));
  return run;
}

/**
 * @brief Build a valid stored result document.
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
  document.set("session", std::move(session));
  document.set("config_snapshot", make_config());
  document.set("build", ztick::make_metadata_object({ztick::metadata_value("compiler", "GCC")}));
  document.set("machine",
               ztick::make_metadata_object({ztick::metadata_value("cpu_model", cpu_model)}));
  document.set("environment",
               ztick::make_metadata_object({ztick::metadata_value("isolation", "in-process")}));
  ConfigValue runs = ConfigValue::array();
  runs.push_back(make_run("run-1", metric_value));
  document.set("runs", std::move(runs));
  document.set("comparisons", ConfigValue::array());
  document.set("warnings", ConfigValue::array());
  document.set("failures", ConfigValue::array());
  document.set("status", ConfigValue::string("complete"));
  return document;
}

/**
 * @brief Read one string member used in comparison assertions.
 * @param object Object to inspect.
 * @param key Member name.
 * @return Stored text, or an empty string for a missing or mistyped member.
 */
std::string string_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  return member == nullptr || !member->as_string().has_value() ? std::string{}
                                                               : *member->as_string();
}

/**
 * @brief Read one numeric member used in comparison assertions.
 * @param object Object to inspect.
 * @param key Member name.
 * @return Stored number, or zero for a missing or non-numeric member.
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
 * @brief Find the first metric record with a requested identifier.
 * @param metrics Metric comparison array.
 * @param id Identifier to find.
 * @return Pointer to the record, or `nullptr` when absent.
 */
const ConfigValue *find_metric(const ConfigValue &metrics, std::string_view id) {
  for (const ConfigValue &record : *(metrics.as_array())) {
    if (string_member(record, "metric_id") == id) {
      return &record;
    }
  }
  return nullptr;
}

} // namespace

/**
 * @brief Verify raw deltas, explicit unavailable metrics and comparability warnings.
 */
TEST_CASE("core comparison preserves raw differences and evidence", "[core][compare]") {
  const ConfigValue baseline = make_document("session-a", "Alpha CPU", 0.002);
  const ConfigValue candidate = make_document("session-b", "Beta CPU", 0.0015);
  std::string error;
  const auto comparison = ztick::compare_result_documents(
      baseline, candidate, ztick::ComparisonReferences{"base.json", "cand.json"}, &error);

  REQUIRE(comparison.has_value());
  REQUIRE(error.empty());
  REQUIRE(string_member(*comparison, "level") == "machine_to_machine");
  REQUIRE(string_member(*comparison, "classification") == "raw_difference");
  REQUIRE(string_member(*comparison, "threshold_percent") == std::string{});

  const ConfigValue *warnings = comparison->find("comparability_warnings");
  REQUIRE(warnings != nullptr);
  REQUIRE(warnings->as_array() != nullptr);
  REQUIRE_FALSE(warnings->as_array()->empty());

  const ConfigValue *metrics = comparison->find("metrics");
  REQUIRE(metrics != nullptr);
  REQUIRE(metrics->as_array() != nullptr);
  REQUIRE(metrics->as_array()->size() == 2U);

  const ConfigValue *duration = find_metric(*metrics, "MET-01");
  REQUIRE(duration != nullptr);
  REQUIRE(string_member(*duration, "comparison_status") == "raw_difference");
  REQUIRE(number_member(*duration, "delta") == Catch::Approx(-0.0005).epsilon(1.0e-12));
  REQUIRE(number_member(*duration, "relative_change_percent") ==
          Catch::Approx(-25.0).epsilon(1.0e-12));

  const ConfigValue *unavailable = find_metric(*metrics, "MET-99");
  REQUIRE(unavailable != nullptr);
  REQUIRE(string_member(*unavailable, "comparison_status") == "unavailable");
  REQUIRE(string_member(*unavailable, "reason") == "baseline value is not a finite number; "
                                                   "candidate value is not a finite number");
}

/**
 * @brief Verify identical metadata yields a run-to-run level and no warnings.
 */
TEST_CASE("core comparison classifies identical metadata as run-to-run", "[core][compare]") {
  const ConfigValue document = make_document("session", "Alpha CPU", 0.001);
  const auto comparison = ztick::compare_result_documents(
      document, document, ztick::ComparisonReferences{"a.json", "b.json"});

  REQUIRE(comparison.has_value());
  REQUIRE(string_member(*comparison, "level") == "run_to_run");
  REQUIRE(comparison->find("comparability_warnings")->as_array()->empty());
  const ConfigValue *duration = find_metric(*comparison->find("metrics"), "MET-01");
  REQUIRE(number_member(*duration, "delta") == Catch::Approx(0.0));
  REQUIRE(number_member(*duration, "relative_change_percent") == Catch::Approx(0.0));
}

/**
 * @brief Verify candidate-only metrics are reported rather than silently dropped.
 */
TEST_CASE("core comparison reports unmatched candidate metrics", "[core][compare]") {
  ConfigValue candidate = make_document("session-b", "Alpha CPU", 0.001);
  ConfigValue extra = make_metric("MET-50", 42.0);
  candidate.find("runs")->as_array()->front().find("metrics")->push_back(std::move(extra));
  const auto comparison =
      ztick::compare_result_documents(make_document("session-a", "Alpha CPU", 0.001), candidate,
                                      ztick::ComparisonReferences{"a.json", "b.json"});

  REQUIRE(comparison.has_value());
  const ConfigValue *unmatched = find_metric(*comparison->find("metrics"), "MET-50");
  REQUIRE(unmatched != nullptr);
  REQUIRE(string_member(*unmatched, "comparison_status") == "unmatched");
  REQUIRE(string_member(*unmatched, "side") == "candidate");
}

/**
 * @brief Verify baseline-only metrics are reported rather than silently dropped.
 */
TEST_CASE("core comparison reports unmatched baseline metrics", "[core][compare]") {
  ConfigValue baseline = make_document("session-a", "Alpha CPU", 0.001);
  ConfigValue extra = make_metric("MET-50", 42.0);
  baseline.find("runs")->as_array()->front().find("metrics")->push_back(std::move(extra));
  const auto comparison =
      ztick::compare_result_documents(baseline, make_document("session-b", "Alpha CPU", 0.001),
                                      ztick::ComparisonReferences{"a.json", "b.json"});

  REQUIRE(comparison.has_value());
  const ConfigValue *unmatched = find_metric(*comparison->find("metrics"), "MET-50");
  REQUIRE(unmatched != nullptr);
  REQUIRE(string_member(*unmatched, "comparison_status") == "unmatched");
  REQUIRE(string_member(*unmatched, "side") == "baseline");
  REQUIRE(number_member(*unmatched, "baseline_value") == Catch::Approx(42.0));
}

/**
 * @brief Verify relative change follows the documented signed baseline formula.
 */
TEST_CASE("core comparison uses signed baseline for relative change", "[core][compare]") {
  const auto comparison =
      ztick::compare_result_documents(make_document("session-a", "Alpha CPU", -0.002),
                                      make_document("session-b", "Alpha CPU", -0.0015),
                                      ztick::ComparisonReferences{"a.json", "b.json"});

  REQUIRE(comparison.has_value());
  const ConfigValue *duration = find_metric(*comparison->find("metrics"), "MET-01");
  REQUIRE(duration != nullptr);
  REQUIRE(number_member(*duration, "delta") == Catch::Approx(0.0005).epsilon(1.0e-12));
  REQUIRE(number_member(*duration, "relative_change_percent") ==
          Catch::Approx(-25.0).epsilon(1.0e-12));
}

/**
 * @brief Verify invalid stored documents are rejected with an explanation.
 */
TEST_CASE("core comparison rejects invalid result documents", "[core][compare]") {
  ConfigValue invalid = make_document("session", "Alpha CPU", 0.001);
  invalid.set("schema_version", ConfigValue::string("9.9.9"));
  std::string error;
  const auto comparison =
      ztick::compare_result_documents(invalid, make_document("session", "Alpha CPU", 0.001),
                                      ztick::ComparisonReferences{"a.json", "b.json"}, &error);

  REQUIRE_FALSE(comparison.has_value());
  REQUIRE(error.find("invalid baseline result") != std::string::npos);
}
