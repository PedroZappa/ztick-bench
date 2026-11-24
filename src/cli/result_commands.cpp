// SPDX-License-Identifier: MIT
/**
 * @file result_commands.cpp
 * @brief Reading and text rendering for the `report` and `compare` commands.
 *
 * The CLI owns argument syntax, file access and presentation. Result parsing and
 * validation stay in `ztick_results`, raw comparison orchestration stays in
 * @ref ztick::compare_result_documents, and this module only renders the returned
 * documents without changing their evidence (BR §12.2, ARCHITECTURE §8).
 */

#include "ztick/result_commands.hpp"

#include "ztick/comparison.hpp"
#include "ztick/json.hpp"
#include "ztick/metadata.hpp"
#include "ztick/result_document.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ztick::cli {

namespace {

/**
 * @brief Render one stored value as stable plain text.
 * @param value Value to render.
 * @return String text as-is, compact JSON for other types, or an explicit marker
 *         for null and unrenderable values.
 */
std::string render_scalar(const ConfigValue &value) {
  if (value.type() == ConfigValueType::null) {
    return "<null>";
  }
  if (value.type() == ConfigValueType::string) {
    return *(value.as_string());
  }
  const JsonWriteResult json = json_write(value);
  return json.ok ? json.text : std::string("<unrenderable>");
}

/**
 * @brief Read one named field as stable plain text.
 * @param owner Object to inspect, or `nullptr`.
 * @param key Field name to read.
 * @return Rendered field text, or an explicit `<absent>` marker.
 */
std::string field_text(const ConfigValue *owner, std::string_view key) {
  const ConfigValue *field = owner == nullptr ? nullptr : owner->find(key);
  return field == nullptr ? std::string("<absent>") : render_scalar(*field);
}

/**
 * @brief Walk a dotted path of object members and render its leaf.
 * @param root Object to start from.
 * @param path Member names in traversal order.
 * @return Rendered leaf, or an explicit `<absent>` marker when a step is missing.
 */
std::string nested_text(const ConfigValue &root, std::initializer_list<std::string_view> path) {
  const ConfigValue *current = &root;
  for (const std::string_view key : path) {
    current = current->find(key);
    if (current == nullptr) {
      return "<absent>";
    }
  }
  return render_scalar(*current);
}

/**
 * @brief Replace empty text with an explicit marker for display.
 * @param text Text to display.
 * @return @p text, or `<empty>` when it has no characters.
 */
std::string display_text(std::string_view text) {
  return text.empty() ? std::string("<empty>") : std::string(text);
}

/**
 * @brief Read the length of an array value.
 * @param value Array value to inspect, or `nullptr`.
 * @return Element count, or zero when @p value is absent or not an array.
 */
std::size_t array_size(const ConfigValue *value) {
  return value == nullptr || value->as_array() == nullptr ? 0U : value->as_array()->size();
}

/**
 * @brief Format a finite real number for display.
 * @param value Number to format.
 * @return Compact general-format text, or `<non-finite>` for an invalid number.
 */
std::string format_number(double value) {
  if (!std::isfinite(value)) {
    return "<non-finite>";
  }
  char buffer[64];
  const auto [end, error] =
      std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
  if (error != std::errc{}) {
    return "<unrenderable>";
  }
  return std::string(buffer, static_cast<std::size_t>(end - buffer));
}

/**
 * @brief Format a signed delta with an explicit plus sign for positive values.
 * @param value Delta to format.
 * @return Number text with a leading `+` for positive finite values.
 */
std::string format_delta(double value) {
  const std::string text = format_number(value);
  return value > 0.0 && text.front() != '-' ? "+" + text : text;
}

/**
 * @brief Flatten a test-point object into a stable display string.
 * @param test_point Object to flatten, or `nullptr`.
 * @return `key=value` members separated by semicolons in stored order.
 */
std::string flatten_test_point(const ConfigValue *test_point) {
  if (test_point == nullptr || test_point->as_object() == nullptr) {
    return "<absent>";
  }
  if (test_point->as_object()->empty()) {
    return "<empty>";
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
 * @brief Append one availability-aware metadata record as a report line.
 * @param out Report text to extend.
 * @param record Metadata record `{status, value, reason}`.
 * @param qualified_name Name including its section prefix.
 * @param indentation Prefix to place before the line.
 */
void append_metadata_field(std::string &out, const ConfigValue *record,
                           std::string_view qualified_name, std::string_view indentation) {
  const std::string status_text = field_text(record, "status");
  const std::optional<ztick::MetadataStatus> status = parse_metadata_status(status_text);
  std::string token;
  if (status.has_value()) {
    token = std::string(metadata_status_token(*status));
  } else {
    token = "<invalid:" + display_text(status_text) + ">";
  }

  std::string detail;
  if (status.has_value() && *status == ztick::MetadataStatus::value) {
    detail = display_text(field_text(record, "value"));
  } else {
    detail = field_text(record, "reason");
    if (detail == "<absent>" || detail.empty()) {
      detail = "no reason recorded";
    }
  }
  out += indentation;
  out += qualified_name;
  out += ": ";
  out += token;
  out += " (";
  out += detail;
  out += ")\n";
}

/**
 * @brief Append every field of one attached metadata section.
 * @param out Report text to extend.
 * @param section Metadata section object, or `nullptr`.
 * @param section_name Stable section name.
 */
void append_metadata_section(std::string &out, const ConfigValue *section,
                             std::string_view section_name) {
  out += section_name;
  out += ":\n";
  if (section == nullptr || section->as_object() == nullptr) {
    out += "  <section>: unavailable (absent or malformed metadata section)\n";
    return;
  }
  if (section->as_object()->empty()) {
    out += "  <empty>: not_applicable (metadata section contains no fields)\n";
    return;
  }
  for (const auto &[name, record] : *(section->as_object())) {
    append_metadata_field(out, &record, std::string(section_name) + "." + name, "  ");
  }
}

/**
 * @brief Append a warning or failure array to the report.
 * @param out Report text to extend.
 * @param label Stable array label.
 * @param records Array of classified records, or `nullptr`.
 * @param indentation Prefix to place before every generated line.
 */
void append_records(std::string &out, std::string_view label, const ConfigValue *records,
                    std::string_view indentation) {
  const std::size_t count = array_size(records);
  out += indentation;
  out += label;
  out += ": ";
  out += std::to_string(count);
  out += '\n';
  if (records == nullptr || records->as_array() == nullptr) {
    return;
  }
  for (std::size_t index = 0U; index < count; ++index) {
    const ConfigValue &record = (*(records->as_array()))[index];
    out += indentation;
    out += "  ";
    out += label;
    out += '[';
    out += std::to_string(index);
    out += "]: ";
    out += display_text(field_text(&record, "category"));
    out += " | ";
    out += display_text(field_text(&record, "message"));
    out += '\n';
  }
}

/**
 * @brief Append one metric record to the report.
 * @param out Report text to extend.
 * @param metric Metric record to render.
 * @param indentation Prefix to place before every generated line.
 */
void append_metric(std::string &out, const ConfigValue &metric, std::string_view indentation) {
  const std::string identifier = display_text(field_text(&metric, "id"));
  const std::string name = display_text(field_text(&metric, "name"));
  const std::string status = field_text(&metric, "status");
  out += indentation;
  out += identifier;
  out += ' ';
  out += name;
  out += ": ";
  out += display_text(field_text(&metric, "value"));
  const std::string unit = field_text(&metric, "unit");
  if (unit != "<absent>" && unit != "<empty>") {
    out += ' ';
    out += unit;
  }
  out += " [";
  out += display_text(status);
  out += ']';
  const std::string reason = display_text(field_text(&metric, "reason"));
  if (reason != "<absent>" && reason != "<empty>") {
    out += " (";
    out += reason;
    out += ')';
  }
  out += '\n';
  out += indentation;
  out += "  source=";
  out += field_text(&metric, "source");
  out += " method=";
  out += field_text(&metric, "method");
  out += '\n';
}

/**
 * @brief Append one run record to the report.
 * @param out Report text to extend.
 * @param run Run record to render.
 * @param index Zero-based run position in the document.
 */
void append_run(std::string &out, const ConfigValue &run, std::size_t index) {
  out += "  run[";
  out += std::to_string(index);
  out += "] ";
  out += display_text(field_text(&run, "id"));
  out += ": ";
  out += display_text(field_text(&run, "status"));
  out += '\n';
  out += "    test_point: ";
  out += flatten_test_point(run.find("test_point"));
  out += '\n';

  const ConfigValue *validation = run.find("validation");
  if (validation == nullptr || validation->as_object() == nullptr) {
    out += "    validation: unavailable (absent or malformed validation record)\n";
  } else {
    out += "    validation: level=";
    out += field_text(validation, "level");
    out += " cases_checked=";
    out += field_text(validation, "cases_checked");
    out += '\n';
  }

  append_records(out, "warnings", run.find("warnings"), "    ");
  append_records(out, "failures", run.find("failures"), "    ");

  const ConfigValue *metrics = run.find("metrics");
  if (metrics == nullptr || metrics->as_array() == nullptr) {
    out += "    metrics: unavailable (absent or malformed metric array)\n";
    return;
  }
  out += "    metrics: ";
  out += std::to_string(metrics->as_array()->size());
  out += '\n';
  for (const ConfigValue &metric : *(metrics->as_array())) {
    append_metric(out, metric, "      ");
  }
}

/**
 * @brief Read the numeric content of a rendered ConfigValue field.
 * @param field Field to inspect.
 * @return Finite number when the field stores a numeric scalar, otherwise nullopt.
 */
std::optional<double> numeric_field(const ConfigValue *field) {
  if (field == nullptr) {
    return std::nullopt;
  }
  if (const std::optional<std::int64_t> integer = field->as_integer(); integer.has_value()) {
    const double converted = static_cast<double>(*integer);
    return std::isfinite(converted) ? std::optional<double>{converted} : std::nullopt;
  }
  const std::optional<double> real = field->as_real();
  return real.has_value() && std::isfinite(*real) ? real : std::nullopt;
}

/**
 * @brief Append one status-bearing comparison field to the rendered record.
 * @param out Comparison text to extend.
 * @param label Field label to print.
 * @param note Object with `status`, `reason` and optional detail fields.
 * @note Applied fields print their retained details on an indented second line;
 *       not-applicable fields remain a single explicit line.
 */
void append_status_field(std::string &out, std::string_view label, const ConfigValue *note) {
  const std::string status = display_text(field_text(note, "status"));
  out += label;
  out += ": ";
  out += status;
  out += " (";
  out += display_text(field_text(note, "reason"));
  out += ")\n";
  if (status == "not_applicable" || note == nullptr) {
    return;
  }
  out += "  ";
  out += label;
  out += "_detail:";
  for (const std::string_view key :
       {"method", "alpha", "sided", "iterations", "seed", "multiplicity", "metrics_compared",
        "metrics_tested", "outliers_removed", "regressions", "improvements", "raw_differences",
        "within_variability"}) {
    const ConfigValue *field = note->find(key);
    if (field == nullptr) {
      continue;
    }
    out += ' ';
    out += key;
    out += '=';
    out += render_scalar(*field);
  }
  out += '\n';
}

/**
 * @brief Append one comparability warning from a comparison record.
 * @param out Comparison text to extend.
 * @param warning Warning record to render.
 * @param index Zero-based warning position.
 */
void append_comparability_warning(std::string &out, const ConfigValue &warning, std::size_t index) {
  out += "  [";
  out += std::to_string(index);
  out += "] ";
  out += field_text(&warning, "field");
  out += ": ";
  out += field_text(&warning, "left_status");
  out += '(';
  out += display_text(field_text(&warning, "left_value"));
  out += ") vs ";
  out += field_text(&warning, "right_status");
  out += '(';
  out += display_text(field_text(&warning, "right_value"));
  out += ") -- ";
  out += field_text(&warning, "message");
  out += '\n';
}

/**
 * @brief Append one metric comparison from a comparison record.
 * @param out Comparison text to extend.
 * @param record Metric comparison record to render.
 */
void append_comparison_metric(std::string &out, const ConfigValue &record) {
  const std::string metric =
      field_text(&record, "run_id") + " / " + field_text(&record, "metric_id");
  out += "  ";
  out += metric;
  out += ' ';
  out += display_text(field_text(&record, "name"));
  out += ": baseline=";
  out += field_text(&record, "baseline_value");
  out += " [";
  out += field_text(&record, "baseline_status");
  out += "] candidate=";
  out += field_text(&record, "candidate_value");
  out += " [";
  out += field_text(&record, "candidate_status");
  out += ']';

  const std::string status = field_text(&record, "comparison_status");
  if (status == "raw_difference") {
    const std::optional<double> delta = numeric_field(record.find("delta"));
    out += " delta=";
    out += delta.has_value() ? format_delta(*delta) : "<unavailable>";
    const std::string unit = field_text(&record, "unit");
    if (unit != "<absent>" && unit != "<empty>") {
      out += ' ';
      out += unit;
    }
    const std::optional<double> relative = numeric_field(record.find("relative_change_percent"));
    out += " relative_change=";
    if (relative.has_value()) {
      out += format_delta(*relative);
      out += "%";
    } else {
      out += "<unavailable> (";
      out += display_text(field_text(&record, "reason"));
      out += ')';
    }
  } else if (status == "unavailable") {
    out += " delta=<unavailable> (";
    out += display_text(field_text(&record, "reason"));
    out += ')';
  } else {
    out += " contribution=<unavailable> (";
    out += field_text(&record, "side");
    out += "_only)";
  }

  const ConfigValue *classification = record.find("classification");
  if (classification != nullptr) {
    out += " classification=";
    out += render_scalar(*classification);
  }
  const ConfigValue *polarity = record.find("polarity");
  if (polarity != nullptr) {
    out += " polarity=";
    out += render_scalar(*polarity);
  }
  const ConfigValue *significance = record.find("significance");
  if (significance != nullptr) {
    const std::string significance_status = field_text(significance, "status");
    out += " significance=";
    out += significance_status;
    if (significance_status == "applied") {
      out += " significant=";
      out += field_text(significance, "significant");
      out += " interval=[";
      out += field_text(significance, "interval_low");
      out += ",";
      out += field_text(significance, "interval_high");
      out += ']';
    }
  }
  const ConfigValue *baseline_count = record.find("baseline_sample_count");
  const ConfigValue *candidate_count = record.find("candidate_sample_count");
  if (baseline_count != nullptr && candidate_count != nullptr) {
    out += " samples=";
    out += render_scalar(*baseline_count);
    out += "/";
    out += render_scalar(*candidate_count);
  }
  out += '\n';
}

/**
 * @brief Parse one finite command-line real number.
 * @param text Text to parse.
 * @param value Receiver for the parsed number.
 * @return True when the whole text is a finite real number.
 */
bool parse_policy_number(std::string_view text, double &value) {
  if (text.empty()) {
    return false;
  }
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
         std::isfinite(value);
}

/**
 * @brief Read an option value supplied as a separate argument.
 * @param args Complete argument list.
 * @param index Index of the option token; advanced to the value token on success.
 * @param option Option name used in error messages.
 * @param error Receiver for a usage error.
 * @return Option value, or `std::nullopt` when absent or empty.
 */
std::optional<std::string_view> option_value(const std::vector<std::string_view> &args,
                                             std::size_t &index, std::string_view option,
                                             std::string &error) {
  if (index + 1U >= args.size()) {
    error = "option " + std::string(option) + " requires a value";
    return std::nullopt;
  }
  const std::string_view value = args[++index];
  if (value.empty()) {
    error = "option " + std::string(option) + " requires a non-empty value";
    return std::nullopt;
  }
  return value;
}

/**
 * @brief Load and validate one stored result through an injected reader.
 * @param reader Reader to invoke.
 * @param path Path passed to @p reader.
 * @param label Role name such as `baseline` or `candidate`.
 * @param document Receiver for the validated document.
 * @param error Receiver for a human-readable failure explanation.
 * @return True when a validated document was loaded.
 */
bool load_result(const ResultReader &reader, const std::filesystem::path &path,
                 std::string_view label, ConfigValue &document, std::string &error) {
  const std::string path_text = path.string();
  const ResultParseResult parsed = reader(path);
  if (!parsed.ok) {
    error = "cannot read " + std::string(label) + " result '" + path_text +
            "': " + (parsed.error.empty() ? std::string("unknown read failure") : parsed.error);
    return false;
  }
  std::string validation_error;
  if (!validate_result_document(parsed.document, &validation_error)) {
    error =
        "invalid " + std::string(label) + " result '" + path_text + "': " +
        (validation_error.empty() ? std::string("unknown validation failure") : validation_error);
    return false;
  }
  document = parsed.document;
  return true;
}

} // namespace

ResultReader default_result_reader() {
  return [](const std::filesystem::path &path) { return read_result_document(path); };
}

std::optional<CompareInvocation> parse_compare_arguments(const std::vector<std::string_view> &args,
                                                         std::string *error) {
  std::vector<std::string_view> paths;
  CompareInvocation invocation;
  bool threshold_seen = false;
  bool alpha_seen = false;
  bool sided_seen = false;

  for (std::size_t index = 0U; index < args.size(); ++index) {
    const std::string_view argument = args[index];
    std::string_view name = argument;
    std::string_view inline_value;
    bool has_inline_value = false;
    if (const std::size_t separator = argument.find('='); separator != std::string_view::npos) {
      name = argument.substr(0U, separator);
      inline_value = argument.substr(separator + 1U);
      has_inline_value = true;
    }

    if (name != "--threshold" && name != "--alpha" && name != "--sided") {
      if (argument.starts_with('-')) {
        if (error != nullptr) {
          *error = "unknown option '" + std::string(argument) + "'";
        }
        return std::nullopt;
      }
      paths.push_back(argument);
      continue;
    }

    std::string message;
    if ((name == "--threshold" && threshold_seen) || (name == "--alpha" && alpha_seen) ||
        (name == "--sided" && sided_seen)) {
      if (error != nullptr) {
        *error = "option " + std::string(name) + " was specified more than once";
      }
      return std::nullopt;
    }
    std::string_view value = inline_value;
    if (!has_inline_value) {
      const auto separate = option_value(args, index, name, message);
      if (!separate.has_value()) {
        if (error != nullptr) {
          *error = message;
        }
        return std::nullopt;
      }
      value = *separate;
    }
    if (value.empty()) {
      if (error != nullptr) {
        *error = "option " + std::string(name) + " requires a non-empty value";
      }
      return std::nullopt;
    }

    if (name == "--sided") {
      if (value == "one") {
        invocation.policy.sidedness = ztick::SignificanceSidedness::one_sided;
      } else if (value == "two") {
        invocation.policy.sidedness = ztick::SignificanceSidedness::two_sided;
      } else {
        if (error != nullptr) {
          *error = "option --sided accepts 'one' or 'two'";
        }
        return std::nullopt;
      }
      sided_seen = true;
      continue;
    }

    double number = 0.0;
    if (!parse_policy_number(value, number)) {
      if (error != nullptr) {
        *error = "option " + std::string(name) + " requires a finite number";
      }
      return std::nullopt;
    }
    if (name == "--threshold") {
      if (number < 0.0) {
        if (error != nullptr) {
          *error = "option --threshold requires a non-negative percentage";
        }
        return std::nullopt;
      }
      invocation.policy.threshold_percent = number;
      threshold_seen = true;
    } else {
      if (number <= 0.0 || number >= 1.0) {
        if (error != nullptr) {
          *error = "option --alpha must be greater than 0 and less than 1";
        }
        return std::nullopt;
      }
      invocation.policy.alpha = number;
      alpha_seen = true;
    }
  }

  if (paths.size() != 2U) {
    if (error != nullptr) {
      *error = "compare requires a baseline and a candidate path";
    }
    return std::nullopt;
  }
  invocation.baseline = std::filesystem::path(paths.front());
  invocation.candidate = std::filesystem::path(paths[1U]);
  return invocation;
}

std::string render_result_report(const ConfigValue &document) {
  std::string out = "ztick-bench: result report\n";
  ConfigValue normalized = document;
  std::string error;
  if (!finalize_result_document(normalized, &error)) {
    out += "status: unavailable (";
    out += error.empty() ? "invalid stored result" : error;
    out += ")\n";
    return out;
  }

  out += "schema_version: ";
  out += field_text(&document, "schema_version");
  out += "\nsdk_version: ";
  out += field_text(&document, "sdk_version");
  out += "\nsession: ";
  out += nested_text(document, {"session", "id"});
  out += "\nstarted_at: ";
  out += nested_text(document, {"session", "started_at"});
  out += "\nended_at: ";
  out += nested_text(document, {"session", "ended_at"});
  out += "\nstatus: ";
  out += field_text(&normalized, "status");
  out += "\nstored_status: ";
  out += field_text(&document, "status");
  if (field_text(&normalized, "status") != field_text(&document, "status")) {
    out += "\nstatus_mismatch: stored status is not supported by recorded run evidence";
  }
  out += '\n';

  append_metadata_section(out, document.find("build"), "build");
  append_metadata_section(out, document.find("machine"), "machine");
  append_metadata_section(out, document.find("environment"), "environment");

  out += "runs: ";
  out += std::to_string(document.find("runs")->as_array()->size());
  out += '\n';
  const ConfigValue *runs = normalized.find("runs");
  for (std::size_t index = 0U; index < runs->as_array()->size(); ++index) {
    append_run(out, (*(runs->as_array()))[index], index);
  }

  append_records(out, "warnings", document.find("warnings"), "");
  append_records(out, "failures", document.find("failures"), "");
  return out;
}

std::string render_result_comparison(const ConfigValue &comparison) {
  std::string out = "ztick-bench: stored result comparison\n";
  const ConfigValue *baseline = comparison.find("baseline");
  const ConfigValue *candidate = comparison.find("candidate");
  out += "baseline: ";
  out += field_text(baseline, "reference");
  out += " session=";
  out += field_text(baseline, "session_id");
  out += " status=";
  out += field_text(baseline, "status");
  out += '\n';
  out += "candidate: ";
  out += field_text(candidate, "reference");
  out += " session=";
  out += field_text(candidate, "session_id");
  out += " status=";
  out += field_text(candidate, "status");
  out += '\n';
  out += "level: ";
  out += field_text(&comparison, "level");
  out += "\nclassification: ";
  out += field_text(&comparison, "classification");
  out += '\n';
  out += "threshold_percent: ";
  out += field_text(&comparison, "threshold_percent");
  out += " (";
  out += field_text(comparison.find("threshold_status"), "status");
  out += ")\n";
  append_status_field(out, "statistics", comparison.find("statistics"));
  append_status_field(out, "significance", comparison.find("significance"));

  const ConfigValue *warnings = comparison.find("comparability_warnings");
  out += "comparability_warnings: ";
  out += std::to_string(array_size(warnings));
  out += '\n';
  if (warnings != nullptr && warnings->as_array() != nullptr) {
    for (std::size_t index = 0U; index < warnings->as_array()->size(); ++index) {
      append_comparability_warning(out, (*(warnings->as_array()))[index], index);
    }
  }

  const ConfigValue *metrics = comparison.find("metrics");
  out += "metrics: ";
  out += std::to_string(array_size(metrics));
  out += '\n';
  if (metrics != nullptr && metrics->as_array() != nullptr) {
    for (const ConfigValue &metric : *(metrics->as_array())) {
      append_comparison_metric(out, metric);
    }
  }
  return out;
}

CommandResult execute_report(const std::vector<std::string_view> &args,
                             const ResultReader &reader) {
  if (args.size() != 1U) {
    return CommandResult{
        ExitCode::usage_error, {}, "ztick-bench: report requires exactly one result path\n"};
  }
  if (!reader) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: result reader is missing\n"};
  }

  ConfigValue document = ConfigValue::null();
  std::string error;
  if (!load_result(reader, std::filesystem::path(args.front()), "report", document, error)) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: " + error + "\n"};
  }
  return CommandResult{ExitCode::success, render_result_report(document), {}};
}

CommandResult execute_report(const std::vector<std::string_view> &args) {
  return execute_report(args, default_result_reader());
}

CommandResult execute_compare(const std::vector<std::string_view> &args,
                              const ResultReader &reader) {
  std::string usage_error;
  const std::optional<CompareInvocation> invocation = parse_compare_arguments(args, &usage_error);
  if (!invocation.has_value()) {
    return CommandResult{ExitCode::usage_error, {}, "ztick-bench: " + usage_error + "\n"};
  }
  if (!reader) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: result reader is missing\n"};
  }

  const std::filesystem::path &baseline_path = invocation->baseline;
  const std::filesystem::path &candidate_path = invocation->candidate;
  ConfigValue baseline = ConfigValue::null();
  ConfigValue candidate = ConfigValue::null();
  std::string error;
  if (!load_result(reader, baseline_path, "baseline", baseline, error)) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: " + error + "\n"};
  }
  if (!load_result(reader, candidate_path, "candidate", candidate, error)) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: " + error + "\n"};
  }

  const std::optional<ConfigValue> comparison = compare_result_documents_with_policy(
      baseline, candidate, ComparisonReferences{baseline_path.string(), candidate_path.string()},
      invocation->policy, &error);
  if (!comparison.has_value()) {
    return CommandResult{ExitCode::failure,
                         {},
                         "ztick-bench: comparison failed: " +
                             (error.empty() ? std::string("unknown comparison failure") : error) +
                             "\n"};
  }
  return CommandResult{ExitCode::success, render_result_comparison(*comparison), {}};
}

CommandResult execute_compare(const std::vector<std::string_view> &args) {
  return execute_compare(args, default_result_reader());
}

} // namespace ztick::cli
