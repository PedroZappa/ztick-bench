// SPDX-License-Identifier: MIT
/**
 * @file comparison.cpp
 * @brief Implementation of the core comparison orchestration in
 *        @ref comparison.hpp.
 */

#include "ztick/comparison.hpp"

#include "ztick/json.hpp"
#include "ztick/metadata.hpp"
#include "ztick/result_document.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ztick {

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
 * @brief Record a comparison failure in an optional error receiver.
 * @param error Receiver to update, or `nullptr`.
 * @param message Failure explanation.
 */
void record_error(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

/**
 * @brief Read one metric's numeric aggregate value.
 * @param metric Metric record to inspect.
 * @return Numeric value when present and finite, otherwise `std::nullopt`.
 */
std::optional<double> metric_number(const ConfigValue *metric) {
  if (metric == nullptr) {
    return std::nullopt;
  }
  const ConfigValue *value = metric->find("value");
  if (value == nullptr) {
    return std::nullopt;
  }
  if (const std::optional<std::int64_t> integer = value->as_integer(); integer.has_value()) {
    const double converted = static_cast<double>(*integer);
    return std::isfinite(converted) ? std::optional<double>{converted} : std::nullopt;
  }
  const std::optional<double> real = value->as_real();
  if (!real.has_value() || !std::isfinite(*real)) {
    return std::nullopt;
  }
  return real;
}

/** @brief One metric located in a stored result document. */
struct MetricView {
  /** @brief Identifier of the owning run. */
  std::string run_id;
  /** @brief Stable metric identifier. */
  std::string metric_id;
  /** @brief Human-readable metric name. */
  std::string name;
  /** @brief Unit recorded with the metric. */
  std::string unit;
  /** @brief Owning metric record, or `nullptr`. */
  const ConfigValue *metric{nullptr};
};

/**
 * @brief Collect every metric from a result document in stored order.
 * @param document Validated result document.
 * @return Metric views ordered by run and then metric array order.
 */
std::vector<MetricView> collect_metrics(const ConfigValue &document) {
  std::vector<MetricView> views;
  const ConfigValue *runs = document.find("runs");
  if (runs == nullptr || runs->as_array() == nullptr) {
    return views;
  }
  for (const ConfigValue &run : *(runs->as_array())) {
    const std::string run_id = display_text(field_text(&run, "id"));
    const ConfigValue *metrics = run.find("metrics");
    if (metrics == nullptr || metrics->as_array() == nullptr) {
      continue;
    }
    const auto *metric_array = metrics->as_array();
    std::transform(metric_array->begin(), metric_array->end(), std::back_inserter(views),
                   [&run_id](const ConfigValue &metric) {
                     return MetricView{run_id, display_text(field_text(&metric, "id")),
                                       display_text(field_text(&metric, "name")),
                                       display_text(field_text(&metric, "unit")), &metric};
                   });
  }
  return views;
}

/**
 * @brief Find the first unused metric with the same run and metric identifiers.
 * @param views Candidate metric views in stable order.
 * @param target Metric view to match.
 * @param used Flags marking already matched candidate views.
 * @return Matching metric record, or `nullptr` when there is no unused match.
 */
const ConfigValue *find_metric(const std::vector<MetricView> &views, const MetricView &target,
                               std::vector<bool> &used) {
  for (std::size_t index = 0U; index < views.size(); ++index) {
    if (!used[index] && views[index].run_id == target.run_id &&
        views[index].metric_id == target.metric_id) {
      used[index] = true;
      return views[index].metric;
    }
  }
  return nullptr;
}

/**
 * @brief Build one explicit record for a metric present on only one side.
 * @param view Metric view to describe.
 * @param side Label of the side that owns the metric: `baseline` or `candidate`.
 * @return Unmatched metric record retaining its value, status and reason.
 */
ConfigValue make_unmatched(const MetricView &view, std::string_view side) {
  const bool baseline_side = side == "baseline";
  const ConfigValue *value = view.metric == nullptr ? nullptr : view.metric->find("value");

  ConfigValue record = ConfigValue::object();
  record.set("run_id", ConfigValue::string(view.run_id));
  record.set("metric_id", ConfigValue::string(view.metric_id));
  record.set("name", ConfigValue::string(view.name));
  record.set("unit", ConfigValue::string(view.unit));
  record.set("baseline_status",
             ConfigValue::string(baseline_side ? field_text(view.metric, "status")
                                               : std::string("<absent>")));
  record.set("candidate_status",
             ConfigValue::string(baseline_side ? std::string("<absent>")
                                               : field_text(view.metric, "status")));
  record.set("baseline_value", baseline_side && value != nullptr ? *value : ConfigValue::null());
  record.set("candidate_value", !baseline_side && value != nullptr ? *value : ConfigValue::null());
  record.set("baseline_reason",
             ConfigValue::string(baseline_side ? field_text(view.metric, "reason")
                                               : std::string("<absent>")));
  record.set("candidate_reason",
             ConfigValue::string(baseline_side ? std::string("<absent>")
                                               : field_text(view.metric, "reason")));
  record.set("comparison_status", ConfigValue::string("unmatched"));
  record.set("side", ConfigValue::string(std::string(side)));
  record.set("reason",
             ConfigValue::string(std::string("no corresponding metric in the ") +
                                 (baseline_side ? "candidate" : "baseline") + " document"));
  return record;
}

/**
 * @brief Append unmatched metrics from one side to the comparison's metric array.
 * @param metrics Metric comparison array to extend.
 * @param views Metric views to inspect.
 * @param used Flags marking already matched views.
 * @param side Label of the side that owns the unmatched metrics.
 */
void append_unmatched(ConfigValue &metrics, const std::vector<MetricView> &views,
                      const std::vector<bool> &used, std::string_view side) {
  std::size_t index = 0U;
  std::for_each(views.begin(), views.end(), [&](const MetricView &view) {
    if (!used[index]) {
      metrics.push_back(make_unmatched(view, side));
    }
    ++index;
  });
}

/**
 * @brief Build one raw-difference record for a baseline/candidate metric pair.
 * @param baseline Baseline metric view.
 * @param candidate Matching candidate metric record, or `nullptr`.
 * @return Raw comparison record, or `std::nullopt` when a delta is not finite.
 */
[[nodiscard]] std::optional<ConfigValue> compare_metric(const MetricView &baseline,
                                                        const ConfigValue *candidate) {
  ConfigValue record = ConfigValue::object();
  record.set("run_id", ConfigValue::string(baseline.run_id));
  record.set("metric_id", ConfigValue::string(baseline.metric_id));
  record.set("name", ConfigValue::string(baseline.name));
  record.set("unit", ConfigValue::string(baseline.unit));
  record.set("baseline_status", ConfigValue::string(field_text(baseline.metric, "status")));
  record.set("candidate_status", ConfigValue::string(field_text(candidate, "status")));
  record.set("baseline_reason", ConfigValue::string(field_text(baseline.metric, "reason")));
  record.set("candidate_reason", ConfigValue::string(field_text(candidate, "reason")));

  const ConfigValue *baseline_value =
      baseline.metric == nullptr ? nullptr : baseline.metric->find("value");
  const ConfigValue *candidate_value = candidate == nullptr ? nullptr : candidate->find("value");
  record.set("baseline_value", baseline_value == nullptr ? ConfigValue::null() : *baseline_value);
  record.set("candidate_value",
             candidate_value == nullptr ? ConfigValue::null() : *candidate_value);

  const std::optional<double> left = metric_number(baseline.metric);
  const std::optional<double> right = metric_number(candidate);
  if (!left.has_value() || !right.has_value()) {
    std::string reason;
    if (!left.has_value()) {
      reason = "baseline value is not a finite number";
    }
    if (!right.has_value()) {
      reason += reason.empty() ? std::string{} : std::string("; ");
      reason += "candidate value is not a finite number";
    }
    record.set("delta", ConfigValue::null());
    record.set("relative_change_percent", ConfigValue::null());
    record.set("comparison_status", ConfigValue::string("unavailable"));
    record.set("reason", ConfigValue::string(std::move(reason)));
    return record;
  }

  const double delta = *right - *left;
  if (!std::isfinite(delta)) {
    return std::nullopt;
  }
  std::optional<double> relative;
  if (*left != 0.0) {
    relative = delta / *left * 100.0;
    if (!std::isfinite(*relative)) {
      return std::nullopt;
    }
  }
  record.set("delta", ConfigValue::real(delta));
  record.set("relative_change_percent",
             relative.has_value() ? ConfigValue::real(*relative) : ConfigValue::null());
  record.set("comparison_status", ConfigValue::string("raw_difference"));
  record.set("reason",
             ConfigValue::string(relative.has_value() ? std::string{}
                                                      : std::string("baseline value is zero")));
  return record;
}

/**
 * @brief Build an explicit not-applicable record for deferred comparison fields.
 * @param reason Explanation of why the field does not apply in P4.8.
 * @return Object with `status` and `reason` members.
 */
ConfigValue not_applicable(std::string_view reason) {
  ConfigValue record = ConfigValue::object();
  record.set("status", ConfigValue::string("not_applicable"));
  record.set("reason", ConfigValue::string(std::string(reason)));
  return record;
}

/**
 * @brief Build the self-contained reference object for one compared document.
 * @param document Validated result document.
 * @param label Caller-owned reference text.
 * @return Reference object with label, session identity, status and metric counts.
 */
ConfigValue document_reference(const ConfigValue &document, const std::string &label) {
  ConfigValue reference = ConfigValue::object();
  reference.set("reference", ConfigValue::string(display_text(label)));
  reference.set("session_id",
                ConfigValue::string(display_text(nested_text(document, {"session", "id"}))));
  reference.set("status", ConfigValue::string(display_text(field_text(&document, "status"))));
  const std::size_t run_count = document.find("runs")->as_array()->size();
  std::size_t metric_count = 0U;
  for (const ConfigValue &run : *(document.find("runs")->as_array())) {
    const ConfigValue *metrics = run.find("metrics");
    if (metrics != nullptr && metrics->as_array() != nullptr) {
      metric_count += metrics->as_array()->size();
    }
  }
  reference.set("run_count", ConfigValue::integer(static_cast<std::int64_t>(run_count)));
  reference.set("metric_count", ConfigValue::integer(static_cast<std::int64_t>(metric_count)));
  return reference;
}

/**
 * @brief Return the metadata section prefix of one comparability warning.
 * @param field Qualified warning field such as `machine.cpu_model`.
 * @return Prefix before the first dot, or the whole field when there is no dot.
 */
std::string_view warning_prefix(std::string_view field) {
  const std::size_t separator = field.find('.');
  return separator == std::string_view::npos ? field : field.substr(0U, separator);
}

/**
 * @brief Derive the comparison level from metadata comparability warnings.
 * @param warnings Comparability warning array.
 * @return `machine_to_machine`, `build_to_build`, or `run_to_run`.
 */
std::string_view comparison_level(const ConfigValue &warnings) {
  bool build_or_environment_mismatch = false;
  for (const ConfigValue &warning : *(warnings.as_array())) {
    const std::string field = field_text(&warning, "field");
    const std::string_view prefix = warning_prefix(field);
    if (prefix == "machine") {
      return "machine_to_machine";
    }
    if (prefix == "build" || prefix == "environment") {
      build_or_environment_mismatch = true;
    }
  }
  return build_or_environment_mismatch ? "build_to_build" : "run_to_run";
}

} // namespace

std::optional<ConfigValue> compare_result_documents(const ConfigValue &baseline,
                                                    const ConfigValue &candidate,
                                                    const ComparisonReferences &references,
                                                    std::string *error) {
  std::string validation_error;
  if (!validate_result_document(baseline, &validation_error)) {
    record_error(error, "invalid baseline result: " + validation_error);
    return std::nullopt;
  }
  if (!validate_result_document(candidate, &validation_error)) {
    record_error(error, "invalid candidate result: " + validation_error);
    return std::nullopt;
  }

  const MetadataComparison metadata = compare_metadata(baseline, candidate);
  const std::vector<MetricView> baseline_metrics = collect_metrics(baseline);
  const std::vector<MetricView> candidate_metrics = collect_metrics(candidate);
  std::vector<bool> candidate_used(candidate_metrics.size(), false);

  ConfigValue metrics = ConfigValue::array();
  std::string metric_error;
  std::for_each(baseline_metrics.begin(), baseline_metrics.end(), [&](const MetricView &view) {
    if (!metric_error.empty()) {
      return;
    }
    const ConfigValue *match = find_metric(candidate_metrics, view, candidate_used);
    if (match == nullptr) {
      metrics.push_back(make_unmatched(view, "baseline"));
      return;
    }
    const std::optional<ConfigValue> compared = compare_metric(view, match);
    if (!compared.has_value()) {
      metric_error = "comparison produced a non-finite metric delta for " + view.run_id + " / " +
                     view.metric_id;
      return;
    }
    metrics.push_back(*compared);
  });
  if (!metric_error.empty()) {
    record_error(error, metric_error);
    return std::nullopt;
  }
  append_unmatched(metrics, candidate_metrics, candidate_used, "candidate");

  ConfigValue context = ConfigValue::object();
  for (const std::string_view section : {"build", "machine", "environment"}) {
    context.set("baseline_" + std::string(section), *baseline.find(section));
    context.set("candidate_" + std::string(section), *candidate.find(section));
  }

  ConfigValue record = ConfigValue::object();
  record.set("baseline", document_reference(baseline, references.baseline));
  record.set("candidate", document_reference(candidate, references.candidate));
  record.set("level", ConfigValue::string(std::string(comparison_level(metadata.warnings))));
  record.set("threshold_percent", ConfigValue::null());
  record.set("threshold_status", not_applicable("threshold checks are owned by P4.9"));
  record.set("statistics", not_applicable("variability statistics are owned by P4.9"));
  record.set("significance", not_applicable("significance tests are owned by P4.9"));
  record.set("classification", ConfigValue::string("raw_difference"));
  record.set("comparability_warnings", metadata.warnings);
  record.set("metrics", std::move(metrics));
  record.set("context", std::move(context));
  return record;
}

} // namespace ztick
