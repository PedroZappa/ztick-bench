// SPDX-License-Identifier: MIT
/**
 * @file csv_projection_test.cpp
 * @brief Catch2 tests for the convenience CSV projection of result documents.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/config_document.hpp>
#include <ztick/csv_projection.hpp>
#include <ztick/result_document.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * @brief Build the config snapshot required by the result document schema.
 * @return Valid v1 config JSON.
 */
std::string make_config() {
  std::string text = R"json({"config_version":"1.0.0","sdk_version":"1.0.0",)json";
  text.append(R"json("integration":{"id":"app-a","version":"1.2.3"},)json");
  text.append(R"json("workload":{"level":"processor","processors":["eq"],)json");
  text.append(R"json("generator":{"type":"noise"}},)json");
  text.append(R"json("dimensions":{"sample_rate_hz":48000,"block_size":128,)json");
  text.append(R"json("channels":2,"oversampling":1,"instances":1},)json");
  text.append(R"json("execution":{"duration_mode":"iterations","iterations":1000,)json");
  text.append(R"json("repetitions":3,"warmup":{"iterations":100},"isolation":"auto"},)json");
  text.append(R"json("profile":{"name":"Quick"},)json");
  text.append(R"json("measurement":{"detail":"aggregate","end_to_end":false,)json");
  text.append(R"json("timing_levels":["per_block","whole_run"]},)json");
  text.append(R"json("validation":{"level":"Basic","tolerances":{},"references":[]},)json");
  text.append(R"json("parallelism":{"workers":"auto","affinity":"auto"}})json");
  return text;
}

/**
 * @brief Build one metric record with explicit, independently known fields.
 * @param id Metric identifier.
 * @param name Human-readable metric name.
 * @param value Numeric value, or null when the metric has no value.
 * @param unit Value unit.
 * @param status Value status token.
 * @param source Measurement source token.
 * @param method Aggregation method token.
 * @param reason Reason for an unavailable or not-applicable value.
 * @return Metric-shaped ConfigValue.
 */
ztick::ConfigValue make_metric(std::string_view id, std::string_view name, ztick::ConfigValue value,
                               std::string_view unit, std::string_view status,
                               std::string_view source, std::string_view method = {},
                               std::string_view reason = {}) {
  ztick::ConfigValue metric = ztick::ConfigValue::object();
  metric.set("id", ztick::ConfigValue::string(std::string(id)));
  metric.set("name", ztick::ConfigValue::string(std::string(name)));
  if (value.type() != ztick::ConfigValueType::null) {
    metric.set("value", std::move(value));
  }
  metric.set("unit", ztick::ConfigValue::string(std::string(unit)));
  metric.set("status", ztick::ConfigValue::string(std::string(status)));
  metric.set("source", ztick::ConfigValue::string(std::string(source)));
  if (!method.empty()) {
    metric.set("method", ztick::ConfigValue::string(std::string(method)));
  }
  if (!reason.empty()) {
    metric.set("reason", ztick::ConfigValue::string(std::string(reason)));
  }
  return metric;
}

/**
 * @brief Build one run record.
 * @param id Run identifier.
 * @param dimensions Dimension values for the test point.
 * @param metrics Metric records; an empty array is stored as-is.
 * @param warnings Number of warning entries to attach.
 * @param failures Number of failure entries to attach.
 * @return Run-shaped ConfigValue.
 */
ztick::ConfigValue make_run(std::string_view id, ztick::ConfigValue dimensions,
                            std::vector<ztick::ConfigValue> metrics, std::size_t warnings,
                            std::size_t failures) {
  ztick::ConfigValue run = ztick::ConfigValue::object();
  run.set("id", ztick::ConfigValue::string(std::string(id)));
  run.set("test_point", std::move(dimensions));

  ztick::ConfigValue metric_array = ztick::ConfigValue::array();
  for (ztick::ConfigValue &metric : metrics) {
    metric_array.push_back(std::move(metric));
  }
  run.set("metrics", std::move(metric_array));

  ztick::ConfigValue warning_array = ztick::ConfigValue::array();
  for (std::size_t index = 0; index < warnings; ++index) {
    warning_array.push_back(ztick::ConfigValue::string("thermal throttling"));
  }
  run.set("warnings", std::move(warning_array));

  ztick::ConfigValue failure_array = ztick::ConfigValue::array();
  for (std::size_t index = 0; index < failures; ++index) {
    failure_array.push_back(ztick::ConfigValue::string("adapter crash"));
  }
  run.set("failures", std::move(failure_array));
  run.set("status", ztick::ConfigValue::string(failures == 0U ? "complete" : "failed"));
  return run;
}

/**
 * @brief Build a complete result document holding the supplied runs.
 * @param runs Run records to attach.
 * @return Result-shaped ConfigValue satisfying the v1 schema.
 */
ztick::ConfigValue make_document(std::vector<ztick::ConfigValue> runs) {
  ztick::ConfigValue document = ztick::ConfigValue::object();
  document.set("schema_version",
               ztick::ConfigValue::string(std::string(ztick::kResultSchemaVersion)));
  document.set("sdk_version", ztick::ConfigValue::string("1.0.0"));

  ztick::ConfigValue session = ztick::ConfigValue::object();
  session.set("id", ztick::ConfigValue::string("session-1"));
  document.set("session", std::move(session));
  document.set("config_snapshot", ztick::parse_config_document(make_config()).document);
  document.set("build", ztick::ConfigValue::object());
  document.set("machine", ztick::ConfigValue::object());
  document.set("environment", ztick::ConfigValue::object());

  ztick::ConfigValue run_array = ztick::ConfigValue::array();
  for (ztick::ConfigValue &run : runs) {
    run_array.push_back(std::move(run));
  }
  document.set("runs", std::move(run_array));
  document.set("comparisons", ztick::ConfigValue::array());
  document.set("warnings", ztick::ConfigValue::array());
  document.set("failures", ztick::ConfigValue::array());
  document.set("status", ztick::ConfigValue::string("complete"));
  return document;
}

/**
 * @brief Build a two-dimension test point with deterministic member order.
 * @param block_size Block size dimension.
 * @param channels Channel count dimension.
 * @return Test-point object.
 */
ztick::ConfigValue make_dimensions(int block_size, int channels) {
  ztick::ConfigValue dimensions = ztick::ConfigValue::object();
  dimensions.set("block_size", ztick::ConfigValue::integer(block_size));
  dimensions.set("channels", ztick::ConfigValue::integer(channels));
  return dimensions;
}

/**
 * @brief Split one CSV line using an independent RFC 4180 reader.
 * @param line Single CSV line without its newline.
 * @return Unescaped cell texts in column order.
 */
std::vector<std::string> split_csv_line(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool in_quotes = false;
  std::size_t index = 0;
  while (index < line.size()) {
    const char character = line[index];
    if (in_quotes) {
      if (character == '"') {
        if (index + 1U < line.size() && line[index + 1U] == '"') {
          field.push_back('"');
          index += 2U;
          continue;
        }
        in_quotes = false;
        ++index;
        continue;
      }
      field.push_back(character);
      ++index;
      continue;
    }
    if (character == '"') {
      in_quotes = true;
      ++index;
      continue;
    }
    if (character == ',') {
      fields.push_back(field);
      field.clear();
      ++index;
      continue;
    }
    field.push_back(character);
    ++index;
  }
  fields.push_back(field);
  return fields;
}

/**
 * @brief Split CSV text into non-empty lines.
 * @param text Complete CSV text.
 * @return Lines in file order without terminators.
 */
std::vector<std::string> csv_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.emplace_back(text.substr(start));
      break;
    }
    lines.emplace_back(text.substr(start, end - start));
    start = end + 1U;
  }
  return lines;
}

/**
 * @brief Return a dynamically generated path for one CSV export.
 * @param name Short discriminator for the calling test case.
 * @return Path inside the system temp directory.
 */
std::filesystem::path csv_path(std::string_view name) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ztick_csv_" + std::string(name) + "_" + std::to_string(ticks) + ".csv");
}

/**
 * @brief Remove a generated file without throwing.
 * @param path Path to remove.
 */
void remove_file(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

} // namespace

/**
 * @brief Verify row mode tokens map to and from their stable names.
 */
TEST_CASE("csv row mode tokens round trip", "[results][csv]") {
  using ztick::CsvRowMode;

  REQUIRE(ztick::csv_row_mode_token(CsvRowMode::per_test_point) == "per_test_point");
  REQUIRE(ztick::csv_row_mode_token(CsvRowMode::per_metric_sample) == "per_metric_sample");
  REQUIRE(ztick::parse_csv_row_mode("per_test_point") == CsvRowMode::per_test_point);
  REQUIRE(ztick::parse_csv_row_mode("per_metric_sample") == CsvRowMode::per_metric_sample);
  REQUIRE_FALSE(ztick::parse_csv_row_mode("per_block").has_value());
}

/**
 * @brief Verify the fixed header, row width and omitted-fields declaration.
 */
TEST_CASE("csv projection uses the documented column set", "[results][csv]") {
  const auto document = make_document({make_run("run-1", make_dimensions(128, 2), {}, 0U, 0U)});
  const auto projection = ztick::project_result_document(document);

  REQUIRE(projection.valid());
  REQUIRE(projection.row_count == 1U);
  const std::vector<std::string> lines = csv_lines(projection.text);
  REQUIRE(lines.size() == 2U);
  REQUIRE(lines.front() == ztick::kCsvHeader);
  REQUIRE(projection.text.back() == '\n');

  const std::vector<std::string> header = split_csv_line(lines.front());
  const std::vector<std::string> cells = split_csv_line(lines.at(1));
  REQUIRE(cells.size() == header.size());
  REQUIRE(cells.back() == ztick::kCsvOmittedFields);
  REQUIRE(cells.at(1) == "run-1");
  REQUIRE(cells.at(2) == "block_size=128;channels=2");
  REQUIRE(cells.at(13) == "complete");
}

/**
 * @brief Verify every shared field matches an independently specified JSON value.
 */
TEST_CASE("shared fields agree between JSON and CSV", "[results][csv]") {
  std::vector<ztick::ConfigValue> first_metrics;
  first_metrics.push_back(make_metric("MET-01", "mean", ztick::ConfigValue::real(1.5), "s",
                                      "measured", "instrumented_dsp", "arithmetic_mean"));
  first_metrics.push_back(make_metric("MET-09", "p99", ztick::ConfigValue::real(3.25), "s",
                                      "estimated", "black_box_monitor", "nearest_rank"));

  const auto document = make_document({
      make_run("run-a", make_dimensions(128, 2), std::move(first_metrics), 1U, 0U),
      make_run("run-b", make_dimensions(256, 1),
               {make_metric("MET-02", "peak", ztick::ConfigValue::real(7.75), "s", "measured",
                            "instrumented_dsp", "max")},
               0U, 1U),
  });

  const auto projection = ztick::project_result_document(document);
  REQUIRE(projection.valid());
  REQUIRE(projection.row_count == 3U);

  const std::vector<std::string> lines = csv_lines(projection.text);
  REQUIRE(lines.size() == 4U);
  const std::vector<std::vector<std::string>> rows{
      split_csv_line(lines.at(1)), split_csv_line(lines.at(2)), split_csv_line(lines.at(3))};

  REQUIRE(rows[0].at(0) == "session-1");
  REQUIRE(rows[0].at(1) == "run-a");
  REQUIRE(rows[0].at(3) == "MET-01");
  REQUIRE(rows[0].at(4) == "mean");
  REQUIRE(rows[0].at(5) == "1.5");
  REQUIRE(rows[0].at(6) == "s");
  REQUIRE(rows[0].at(7) == "measured");
  REQUIRE(rows[0].at(8) == "instrumented_dsp");
  REQUIRE(rows[0].at(9) == "arithmetic_mean");
  REQUIRE(rows[0].at(11) == "1");
  REQUIRE(rows[0].at(12) == "0");
  REQUIRE(rows[0].at(13) == "complete");

  REQUIRE(rows[1].at(5) == "3.25");
  REQUIRE(rows[1].at(7) == "estimated");
  REQUIRE(rows[1].at(8) == "black_box_monitor");

  REQUIRE(rows[2].at(1) == "run-b");
  REQUIRE(rows[2].at(2) == "block_size=256;channels=1");
  REQUIRE(rows[2].at(5) == "7.75");
  REQUIRE(rows[2].at(11) == "0");
  REQUIRE(rows[2].at(12) == "1");
  REQUIRE(rows[2].at(13) == "failed");
}

/**
 * @brief Verify unavailable metrics keep status and reason with an empty value.
 */
TEST_CASE("csv projection preserves unavailable metric status", "[results][csv]") {
  const auto document = make_document(
      {make_run("run-1", make_dimensions(128, 2),
                {make_metric("MET-07", "cpu", ztick::ConfigValue::null(), "%", "unavailable",
                             "platform_monitor", {}, "monitor unavailable")},
                0U, 0U)});

  const auto projection = ztick::project_result_document(document);
  REQUIRE(projection.valid());
  const std::vector<std::string> cells = split_csv_line(csv_lines(projection.text).at(1));
  REQUIRE(cells.at(5).empty());
  REQUIRE(cells.at(7) == "unavailable");
  REQUIRE(cells.at(10) == "monitor unavailable");
}

/**
 * @brief Verify text cells containing commas and quotes are escaped correctly.
 */
TEST_CASE("csv projection escapes comma and quote in metric names", "[results][csv]") {
  const auto document =
      make_document({make_run("run-1", make_dimensions(128, 2),
                              {make_metric("MET-03", "P99, \"tail\"", ztick::ConfigValue::real(2.5),
                                           "s", "measured", "instrumented_dsp")},
                              0U, 0U)});

  const auto projection = ztick::project_result_document(document);
  REQUIRE(projection.valid());
  REQUIRE(projection.text.find("\"P99, \"\"tail\"\"\"") != std::string::npos);

  const std::vector<std::string> cells = split_csv_line(csv_lines(projection.text).at(1));
  REQUIRE(cells.at(4) == "P99, \"tail\"");
}

/**
 * @brief Verify sample mode emits one row per sample with sample overrides.
 */
TEST_CASE("csv projection supports per metric sample rows", "[results][csv]") {
  ztick::ConfigValue metric = make_metric("MET-01", "mean", ztick::ConfigValue::real(1.5), "s",
                                          "measured", "instrumented_dsp");
  ztick::ConfigValue samples = ztick::ConfigValue::array();
  samples.push_back(ztick::ConfigValue::real(1.25));
  ztick::ConfigValue override = ztick::ConfigValue::object();
  override.set("value", ztick::ConfigValue::real(2.5));
  override.set("status", ztick::ConfigValue::string("estimated"));
  samples.push_back(std::move(override));
  metric.set("samples", std::move(samples));

  const auto document =
      make_document({make_run("run-1", make_dimensions(128, 2), {metric}, 0U, 0U)});
  const ztick::CsvExportOptions options{ztick::CsvRowMode::per_metric_sample};
  const auto projection = ztick::project_result_document(document, options);

  REQUIRE(projection.valid());
  REQUIRE(projection.row_count == 2U);
  const std::vector<std::string> lines = csv_lines(projection.text);
  const std::vector<std::string> first = split_csv_line(lines.at(1));
  const std::vector<std::string> second = split_csv_line(lines.at(2));
  REQUIRE(first.at(5) == "1.25");
  REQUIRE(first.at(7) == "measured");
  REQUIRE(second.at(5) == "2.5");
  REQUIRE(second.at(7) == "estimated");
  REQUIRE(second.at(8) == "instrumented_dsp");
}

/**
 * @brief Verify sample mode falls back to the aggregate when no samples exist.
 */
TEST_CASE("csv sample mode falls back to aggregate values", "[results][csv]") {
  const auto document = make_document({make_run(
      "run-1", make_dimensions(128, 2),
      {make_metric("MET-01", "mean", ztick::ConfigValue::real(4.5), "s", "measured", "dsp")}, 0U,
      0U)});

  const ztick::CsvExportOptions options{ztick::CsvRowMode::per_metric_sample};
  const auto projection = ztick::project_result_document(document, options);
  REQUIRE(projection.valid());
  REQUIRE(projection.row_count == 1U);
  REQUIRE(split_csv_line(csv_lines(projection.text).at(1)).at(5) == "4.5");
}

/**
 * @brief Verify a metric-less run still appears in the projection.
 */
TEST_CASE("csv projection keeps a run without metrics", "[results][csv]") {
  const auto document = make_document({make_run("run-empty", make_dimensions(64, 1), {}, 0U, 0U)});
  const auto projection = ztick::project_result_document(document);

  REQUIRE(projection.valid());
  REQUIRE(projection.row_count == 1U);
  const std::vector<std::string> cells = split_csv_line(csv_lines(projection.text).at(1));
  REQUIRE(cells.at(1) == "run-empty");
  REQUIRE(cells.at(3).empty());
  REQUIRE(cells.at(7).empty());
  REQUIRE(cells.at(13) == "complete");
}

/**
 * @brief Verify invalid documents are rejected before any projection is produced.
 */
TEST_CASE("csv projection rejects invalid result documents", "[results][csv]") {
  std::string error;
  REQUIRE_FALSE(ztick::validate_result_document(ztick::ConfigValue::null(), &error));

  const auto projection = ztick::project_result_document(ztick::ConfigValue::null());
  REQUIRE_FALSE(projection.valid());
  REQUIRE_FALSE(projection.error.empty());
  REQUIRE(projection.text.empty());
  REQUIRE(projection.row_count == 0U);
}

/**
 * @brief Verify the CSV writer stores the projection and replaces files atomically.
 */
TEST_CASE("csv writer atomically stores projections", "[results][csv]") {
  const std::filesystem::path path = csv_path("write");
  const auto document = make_document({make_run(
      "run-1", make_dimensions(128, 2),
      {make_metric("MET-01", "mean", ztick::ConfigValue::real(1.5), "s", "measured", "dsp")}, 0U,
      0U)});
  const auto expected = ztick::project_result_document(document);
  REQUIRE(expected.valid());

  const auto written = ztick::write_result_csv(path, document);
  REQUIRE(written.valid());
  REQUIRE(written.row_count == 1U);

  std::ifstream stream(path, std::ios::binary);
  const std::string stored{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
  REQUIRE(stored == expected.text);

  const auto replaced = ztick::write_result_csv(path, document);
  REQUIRE(replaced.valid());
  REQUIRE(replaced.row_count == 1U);

  const auto invalid = ztick::write_result_csv(path, ztick::ConfigValue::null());
  REQUIRE_FALSE(invalid.valid());
  std::ifstream preserved(path, std::ios::binary);
  const std::string still_stored{std::istreambuf_iterator<char>{preserved},
                                 std::istreambuf_iterator<char>{}};
  REQUIRE(still_stored == expected.text);

  remove_file(path);
}
