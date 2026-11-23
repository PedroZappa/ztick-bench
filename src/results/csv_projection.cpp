// SPDX-License-Identifier: MIT
/**
 * @file csv_projection.cpp
 * @brief Implementation of the CSV projection declared in @ref csv_projection.hpp.
 */

#include "ztick/csv_projection.hpp"

#include "ztick/json.hpp"
#include "ztick/result_document.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztick {

namespace {

/**
 * @brief Render one value as plain cell text without CSV quoting.
 * @param value Value to render.
 * @return Raw string text, compact JSON for containers, or empty for null.
 * @note Numbers and booleans use the JSON writer so their text is identical to
 *       the canonical document.
 */
std::string render_scalar(const ConfigValue &value) {
  if (value.type() == ConfigValueType::null) {
    return {};
  }
  if (value.type() == ConfigValueType::string) {
    return *(value.as_string());
  }
  const JsonWriteResult json = json_write(value);
  return json.ok ? json.text : std::string{};
}

/**
 * @brief Read one named field as plain cell text.
 * @param owner Object to inspect, or `nullptr`.
 * @param key Field name to read.
 * @return Rendered field text, or empty when absent.
 */
std::string field_text(const ConfigValue *owner, std::string_view key) {
  if (owner == nullptr) {
    return {};
  }
  const ConfigValue *field = owner->find(key);
  return field == nullptr ? std::string{} : render_scalar(*field);
}

/**
 * @brief Count an array-valued field.
 * @param owner Object to inspect, or `nullptr`.
 * @param key Field name to read.
 * @return Array length, or zero when absent or not an array.
 */
std::size_t array_count(const ConfigValue *owner, std::string_view key) {
  if (owner == nullptr) {
    return 0U;
  }
  const ConfigValue *field = owner->find(key);
  if (field == nullptr || field->type() != ConfigValueType::array) {
    return 0U;
  }
  return field->as_array()->size();
}

/**
 * @brief Flatten a test-point object into a stable `key=value` list.
 * @param test_point Test-point object, or `nullptr`.
 * @return Member list separated by semicolons, preserving JSON member order.
 */
std::string flatten_test_point(const ConfigValue *test_point) {
  if (test_point == nullptr || test_point->type() != ConfigValueType::object) {
    return {};
  }
  std::string text;
  for (const auto &[key, value] : *(test_point->as_object())) {
    if (!text.empty()) {
      text += ';';
    }
    text += key;
    text += '=';
    text += render_scalar(value);
  }
  return text;
}

/**
 * @brief Append one RFC 4180 escaped cell.
 * @param output Row text to append to.
 * @param text Unescaped cell text.
 */
void append_cell(std::string &output, std::string_view text) {
  const bool quoted = text.find_first_of(",\"\r\n") != std::string_view::npos;
  if (!quoted) {
    output.append(text);
    return;
  }
  output.push_back('"');
  for (const char character : text) {
    if (character == '"') {
      output += "\"\"";
    } else {
      output.push_back(character);
    }
  }
  output.push_back('"');
}

/**
 * @brief Encode a complete row with a trailing newline.
 * @param cells Cell texts in header order.
 * @return Encoded row; the caller checks the width before calling.
 */
std::string make_row(const std::vector<std::string> &cells) {
  std::string row;
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (index != 0U) {
      row.push_back(',');
    }
    append_cell(row, cells[index]);
  }
  row.push_back('\n');
  return row;
}

/** @brief Run-level fields repeated on every generated row. */
struct RowContext {
  /** @brief Session identifier from the result document. */
  std::string session_id;
  /** @brief Run identifier from the result record. */
  std::string run_id;
  /** @brief Flattened dimension values. */
  std::string test_point;
  /** @brief Number of warnings attached to the run. */
  std::string warning_count;
  /** @brief Number of failures attached to the run. */
  std::string failure_count;
  /** @brief Run status token from the JSON document. */
  std::string run_status;
};

/**
 * @brief Read a field from a sample object, falling back to its metric record.
 * @param metric Owning metric record.
 * @param sample Optional sample override, or `nullptr`.
 * @param key Field name to read.
 * @return Sample text when present, otherwise metric text.
 */
std::string sampled_field(const ConfigValue &metric, const ConfigValue *sample,
                          std::string_view key) {
  if (sample != nullptr && sample->type() == ConfigValueType::object) {
    const ConfigValue *override = sample->find(key);
    if (override != nullptr) {
      return render_scalar(*override);
    }
  }
  return field_text(&metric, key);
}

/**
 * @brief Choose the value cell for one row.
 * @param metric Owning metric record.
 * @param sample Optional sample override, or `nullptr`.
 * @return Scalar sample text, sample value, or aggregate metric value.
 */
std::string sampled_value(const ConfigValue &metric, const ConfigValue *sample) {
  if (sample == nullptr) {
    return field_text(&metric, "value");
  }
  if (sample->type() != ConfigValueType::object) {
    return render_scalar(*sample);
  }
  return field_text(sample, "value");
}

/**
 * @brief Append one metric row and increment the data row count.
 * @param output CSV text to append to.
 * @param row_count Data row counter to increment.
 * @param context Repeated run-level fields.
 * @param metric Metric record, or `nullptr` for a metric-less run.
 * @param sample Optional sample override, or `nullptr`.
 */
void append_metric_row(std::string &output, std::size_t &row_count, const RowContext &context,
                       const ConfigValue *metric, const ConfigValue *sample) {
  std::vector<std::string> cells;
  cells.reserve(15U);
  cells.push_back(context.session_id);
  cells.push_back(context.run_id);
  cells.push_back(context.test_point);
  cells.push_back(field_text(metric, "id"));
  cells.push_back(field_text(metric, "name"));
  cells.push_back(metric == nullptr ? std::string{} : sampled_value(*metric, sample));
  cells.push_back(field_text(metric, "unit"));
  if (metric == nullptr) {
    cells.emplace_back();
    cells.emplace_back();
    cells.emplace_back();
    cells.emplace_back();
  } else {
    cells.push_back(sampled_field(*metric, sample, "status"));
    cells.push_back(sampled_field(*metric, sample, "source"));
    cells.push_back(sampled_field(*metric, sample, "method"));
    cells.push_back(field_text(metric, "reason"));
  }
  cells.push_back(context.warning_count);
  cells.push_back(context.failure_count);
  cells.push_back(context.run_status);
  cells.push_back(std::string(kCsvOmittedFields));
  output += make_row(cells);
  ++row_count;
}

} // namespace

std::string_view csv_row_mode_token(CsvRowMode mode) noexcept {
  switch (mode) {
  case CsvRowMode::per_test_point:
    return "per_test_point";
  case CsvRowMode::per_metric_sample:
    return "per_metric_sample";
  }
  return "unknown";
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
std::optional<CsvRowMode> parse_csv_row_mode(std::string_view token) noexcept {
  if (token == csv_row_mode_token(CsvRowMode::per_test_point)) {
    return CsvRowMode::per_test_point;
  }
  if (token == csv_row_mode_token(CsvRowMode::per_metric_sample)) {
    return CsvRowMode::per_metric_sample;
  }
  return std::nullopt;
}

CsvProjectionResult project_result_document(const ConfigValue &document,
                                            const CsvExportOptions &options) {
  CsvProjectionResult result;
  if (!validate_result_document(document, &result.error)) {
    return result;
  }

  std::string output(kCsvHeader);
  output.push_back('\n');
  std::size_t row_count = 0U;
  const RowContext context{
      field_text(document.find("session"), "id"), std::string{}, {}, {}, {}, {}};

  for (const ConfigValue &run : *(document.find("runs")->as_array())) {
    RowContext run_context = context;
    run_context.run_id = field_text(&run, "id");
    run_context.test_point = flatten_test_point(run.find("test_point"));
    run_context.warning_count = std::to_string(array_count(&run, "warnings"));
    run_context.failure_count = std::to_string(array_count(&run, "failures"));
    run_context.run_status = field_text(&run, "status");

    const ConfigValue *metrics = run.find("metrics");
    if (metrics == nullptr || metrics->type() != ConfigValueType::array ||
        metrics->as_array()->empty()) {
      append_metric_row(output, row_count, run_context, nullptr, nullptr);
      continue;
    }
    for (const ConfigValue &metric : *(metrics->as_array())) {
      const ConfigValue *samples = metric.find("samples");
      if (options.row_mode == CsvRowMode::per_metric_sample && samples != nullptr &&
          samples->type() == ConfigValueType::array && !samples->as_array()->empty()) {
        for (const ConfigValue &sample : *(samples->as_array())) {
          append_metric_row(output, row_count, run_context, &metric, &sample);
        }
      } else {
        append_metric_row(output, row_count, run_context, &metric, nullptr);
      }
    }
  }

  result.text = std::move(output);
  result.row_count = row_count;
  result.ok = true;
  return result;
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
CsvWriteResult write_result_csv(const std::filesystem::path &target, const ConfigValue &document,
                                const CsvExportOptions &options) {
  CsvWriteResult result;
  const CsvProjectionResult projection = project_result_document(document, options);
  if (!projection.valid()) {
    result.error = projection.error;
    return result;
  }
  const FileWriteResult file = write_file_atomic(target, projection.text);
  if (!file.ok) {
    result.error = file.error;
    return result;
  }
  result.row_count = projection.row_count;
  result.ok = true;
  return result;
}

} // namespace ztick
