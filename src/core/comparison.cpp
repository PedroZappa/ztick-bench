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
#include <limits>
#include <numeric>
#include <optional>
#include <set>
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
 * @brief Build an explicit not-applicable record for a comparison field.
 * @param reason Explanation of why the field does not apply.
 * @return Object with `status` and `reason` members.
 */
ConfigValue not_applicable(std::string_view reason) {
  ConfigValue record = ConfigValue::object();
  record.set("status", ConfigValue::string("not_applicable"));
  record.set("reason", ConfigValue::string(std::string(reason)));
  return record;
}

/**
 * @brief Build a comparison status object with an explicit status token.
 * @param status Status token, such as `applied` or `not_applicable`.
 * @param reason Human-readable explanation retained with the status.
 * @return Object with `status` and `reason` members.
 */
ConfigValue status_record(std::string_view status, std::string reason) {
  ConfigValue record = ConfigValue::object();
  record.set("status", ConfigValue::string(std::string(status)));
  record.set("reason", ConfigValue::string(std::move(reason)));
  return record;
}

/** @brief Preferred direction of a metric; lower or higher values are better. */
enum class MetricPolarity {
  /** @brief Smaller measured values are favourable, such as durations. */
  lower_is_better,
  /** @brief Larger measured values are favourable, such as throughput. */
  higher_is_better,
  /** @brief The metric has no registered preference and is not classified. */
  unspecified,
};

/**
 * @brief Derive metric polarity from its stored identifier and unit.
 * @param id Stable metric identifier.
 * @param unit Recorded metric unit.
 * @return Registered polarity, or `unspecified` for unknown metrics.
 * @note MET-11 throughput variants and MET-15 scaling efficiency are
 *       higher-is-better; other registered MET identifiers are lower-is-better.
 */
MetricPolarity metric_polarity(std::string_view id, std::string_view unit) {
  if (id.starts_with("MET-11") || id == "MET-15" || unit == "frames/s" || unit == "samples/s" ||
      unit == "blocks/s") {
    return MetricPolarity::higher_is_better;
  }
  static constexpr std::string_view kKnownLowerIds[] = {
      "MET-01", "MET-02", "MET-03", "MET-04", "MET-05", "MET-06", "MET-07", "MET-08",
      "MET-09", "MET-10", "MET-12", "MET-13", "MET-14", "MET-16", "MET-17"};
  const bool known = std::any_of(
      std::begin(kKnownLowerIds), std::end(kKnownLowerIds), [&id](std::string_view candidate) {
        return id == candidate || (id.starts_with(candidate) && id.size() > candidate.size() &&
                                   id[candidate.size()] == '.');
      });
  return known ? MetricPolarity::lower_is_better : MetricPolarity::unspecified;
}

/**
 * @brief Render one metric polarity token for a comparison record.
 * @param polarity Polarity to render.
 * @return Stable token stored in comparison records.
 */
std::string_view polarity_token(MetricPolarity polarity) {
  switch (polarity) {
  case MetricPolarity::lower_is_better:
    return "lower_is_better";
  case MetricPolarity::higher_is_better:
    return "higher_is_better";
  case MetricPolarity::unspecified:
    return "unspecified";
  }
  return "unspecified";
}

/**
 * @brief Render one significance sidedness token for a comparison record.
 * @param sidedness Sidedness to render.
 * @return Stable token stored in comparison records.
 */
std::string_view sidedness_token(SignificanceSidedness sidedness) {
  return sidedness == SignificanceSidedness::one_sided ? "one" : "two";
}

/**
 * @brief Validate a comparison policy before any statistical work starts.
 * @param policy Policy to validate.
 * @param error Optional receiver for a human-readable explanation.
 * @return True when every policy field is usable.
 */
bool validate_policy(const ComparisonPolicy &policy, std::string *error) {
  if (!std::isfinite(policy.threshold_percent) || policy.threshold_percent < 0.0) {
    record_error(error, "threshold_percent must be finite and non-negative");
    return false;
  }
  if (!std::isfinite(policy.alpha) || policy.alpha <= 0.0 || policy.alpha >= 1.0) {
    record_error(error, "alpha must be finite and between 0 and 1");
    return false;
  }
  if (policy.bootstrap_iterations == 0U) {
    record_error(error, "bootstrap_iterations must be non-zero");
    return false;
  }
  return true;
}

/**
 * @brief Replace an unusable zero bootstrap seed with the built-in seed.
 * @param policy Policy to normalise.
 * @return Policy with a non-zero seed.
 */
ComparisonPolicy normalized_policy(const ComparisonPolicy &policy) {
  ComparisonPolicy result = policy;
  if (result.seed == 0U) {
    result.seed = 0x5EED1234U;
  }
  return result;
}

/**
 * @brief Collect finite run-level values for one metric identifier.
 * @param document Validated result document.
 * @param metric_id Metric identifier to collect.
 * @return One numeric value per run that carries the metric, in stored order.
 * @note Each run is one repetition; per-block timing samples are deliberately not
 *       used because they are not independent observations (METRICS §6).
 */
std::vector<double> run_level_samples(const ConfigValue &document, std::string_view metric_id) {
  std::vector<double> values;
  const ConfigValue *runs = document.find("runs");
  if (runs == nullptr || runs->as_array() == nullptr) {
    return values;
  }
  std::for_each(runs->as_array()->begin(), runs->as_array()->end(),
                [&values, &metric_id](const ConfigValue &run) {
                  const ConfigValue *metrics = run.find("metrics");
                  if (metrics == nullptr || metrics->as_array() == nullptr) {
                    return;
                  }
                  std::for_each(metrics->as_array()->begin(), metrics->as_array()->end(),
                                [&values, &metric_id](const ConfigValue &metric) {
                                  if (field_text(&metric, "id") == metric_id) {
                                    if (const std::optional<double> value = metric_number(&metric);
                                        value.has_value()) {
                                      values.push_back(*value);
                                    }
                                  }
                                });
                });
  return values;
}

/**
 * @brief Convert run-level samples into a stored numeric array.
 * @param values Samples to copy.
 * @return Config-shaped array of finite numbers.
 */
ConfigValue numeric_array(const std::vector<double> &values) {
  ConfigValue array = ConfigValue::array();
  std::for_each(values.begin(), values.end(),
                [&array](double value) { array.push_back(ConfigValue::real(value)); });
  return array;
}

/**
 * @brief Deterministic 32-bit generator used for bootstrap resampling.
 * @note The xorshift algorithm is fixed so a policy and document pair always
 *       produce the same comparison record.
 */
class Xorshift32 {
public:
  /** @brief Construct a generator with a non-zero seed. @param seed Initial state. */
  explicit Xorshift32(std::uint32_t seed) noexcept : state_(seed == 0U ? 0x5EED1234U : seed) {}

  /** @brief Advance the generator and return the next unsigned value. @return Next state value. */
  std::uint32_t next() noexcept {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 17U;
    state_ ^= state_ << 5U;
    return state_;
  }

private:
  /** @brief Current generator state; never zero. */
  std::uint32_t state_;
};

/**
 * @brief Compute one linearly interpolated quantile of a sorted sample.
 * @param sorted Samples in ascending order.
 * @param fraction Quantile fraction in `[0, 1]`.
 * @return Interpolated quantile, or a quiet NaN for an empty sample.
 */
double interpolated_quantile(const std::vector<double> &sorted, double fraction) {
  if (sorted.empty()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (sorted.size() == 1U) {
    return sorted.front();
  }
  const double clamped = std::clamp(fraction, 0.0, 1.0);
  const double position = clamped * static_cast<double>(sorted.size() - 1U);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = std::min(lower + 1U, sorted.size() - 1U);
  const double weight = position - static_cast<double>(lower);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * weight;
}

/**
 * @brief Compute the mean of one bootstrap resample drawn with replacement.
 * @param values Original run-level values.
 * @param generator Generator supplying resample indices.
 * @return Arithmetic mean of the drawn values.
 * @pre @p values is non-empty.
 */
double resampled_mean(const std::vector<double> &values, Xorshift32 &generator) {
  std::vector<double> resample;
  resample.reserve(values.size());
  std::generate_n(std::back_inserter(resample), values.size(),
                  [&values, &generator]() { return values[generator.next() % values.size()]; });
  return std::accumulate(resample.begin(), resample.end(), 0.0) /
         static_cast<double>(resample.size());
}

/** @brief Outcome of one bootstrap mean-difference significance check. */
struct BootstrapOutcome {
  /** @brief Whether the check could be applied to the supplied samples. */
  bool applied{false};
  /** @brief Whether the bootstrap interval supports the observed direction. */
  bool significant{false};
  /** @brief Lower interval endpoint, when applicable. */
  std::optional<double> low;
  /** @brief Upper interval endpoint, when applicable. */
  std::optional<double> high;
  /** @brief Explanation retained with the outcome. */
  std::string reason;
};

/**
 * @brief Test a run-level mean difference with a percentile bootstrap interval.
 * @param baseline Baseline run-level values.
 * @param candidate Candidate run-level values.
 * @param policy Validated comparison policy.
 * @param observed_delta Aggregate candidate-minus-baseline delta.
 * @return Bootstrap interval and significance decision.
 * @note Runs, not per-block samples, are the independent unit. Two-sided checks
 *       use `alpha/2` tails; one-sided checks use an `alpha` tail in the observed
 *       direction. Polarity affects classification, not whether the interval
 *       excludes zero in the observed direction. The original values are copied
 *       into the comparison record unchanged.
 */
BootstrapOutcome bootstrap_mean_difference(const std::vector<double> &baseline,
                                           const std::vector<double> &candidate,
                                           const ComparisonPolicy &policy, double observed_delta) {
  BootstrapOutcome outcome;
  if (baseline.size() < 2U || candidate.size() < 2U) {
    outcome.reason = "at least two run-level samples are required on each side";
    return outcome;
  }

  Xorshift32 generator(policy.seed);
  std::vector<double> differences;
  differences.reserve(policy.bootstrap_iterations);
  std::generate_n(std::back_inserter(differences), policy.bootstrap_iterations,
                  [&baseline, &candidate, &generator]() {
                    return resampled_mean(candidate, generator) -
                           resampled_mean(baseline, generator);
                  });
  std::sort(differences.begin(), differences.end());

  const bool one_sided = policy.sidedness == SignificanceSidedness::one_sided;
  const double tail = one_sided ? policy.alpha : policy.alpha / 2.0;
  outcome.low = interpolated_quantile(differences, tail);
  outcome.high = interpolated_quantile(differences, 1.0 - tail);
  if (!std::isfinite(*outcome.low) || !std::isfinite(*outcome.high)) {
    outcome.reason = "bootstrap interval is not finite";
    return outcome;
  }

  if (one_sided) {
    if (observed_delta > 0.0) {
      outcome.significant = *outcome.low > 0.0;
    } else if (observed_delta < 0.0) {
      outcome.significant = *outcome.high < 0.0;
    }
  } else {
    outcome.significant = *outcome.low > 0.0 || *outcome.high < 0.0;
  }
  outcome.applied = true;
  outcome.reason = one_sided ? "one-sided bootstrap percentile interval"
                             : "two-sided bootstrap percentile interval";
  return outcome;
}

/**
 * @brief Classify one metric from its threshold and significance decisions.
 * @param threshold_exceeded Whether the absolute relative change meets the threshold.
 * @param outcome Bootstrap significance outcome.
 * @param observed_delta Aggregate candidate-minus-baseline delta.
 * @param polarity Registered metric polarity.
 * @return Stable classification token stored in the record.
 */
std::string_view classify_metric(bool threshold_exceeded, const BootstrapOutcome &outcome,
                                 double observed_delta, MetricPolarity polarity) {
  if (!outcome.applied) {
    return "within_variability";
  }
  if (threshold_exceeded && outcome.significant && observed_delta != 0.0) {
    const bool adverse =
        polarity == MetricPolarity::lower_is_better ? observed_delta > 0.0 : observed_delta < 0.0;
    return adverse ? "regression" : "improvement";
  }
  return outcome.significant ? "raw_difference" : "within_variability";
}

/** @brief Counts of per-metric decisions used to derive the overall classification. */
struct PolicyCounts {
  /** @brief Number of finite metric pairs whose significance could be tested. */
  std::size_t tested{0U};
  /** @brief Number of matched metric pairs receiving a policy decision. */
  std::size_t compared{0U};
  /** @brief Number of metrics classified as regressions. */
  std::size_t regressions{0U};
  /** @brief Number of metrics classified as improvements. */
  std::size_t improvements{0U};
  /** @brief Number of significant but sub-threshold differences. */
  std::size_t raw_differences{0U};
  /** @brief Number of differences within normal variability or not applicable. */
  std::size_t within_variability{0U};
};

/**
 * @brief Derive the overall comparison classification from per-metric counts.
 * @param counts Aggregated per-metric decisions.
 * @return Stable overall classification token.
 * @note Any regression dominates; the other tokens follow the METRICS §6 order.
 */
std::string_view overall_classification(const PolicyCounts &counts) {
  if (counts.regressions > 0U) {
    return "regression";
  }
  if (counts.improvements > 0U) {
    return "improvement";
  }
  if (counts.raw_differences > 0U) {
    return "raw_difference";
  }
  return "within_variability";
}

/**
 * @brief Add a numeric field to a status object when a value is present.
 * @param record Status object to extend.
 * @param key Field name.
 * @param value Optional finite number.
 */
void add_optional_number(ConfigValue &record, std::string_view key,
                         const std::optional<double> &value) {
  if (value.has_value() && std::isfinite(*value)) {
    record.set(std::string(key), ConfigValue::real(*value));
  } else {
    record.set(std::string(key), ConfigValue::null());
  }
}

/**
 * @brief Compute the arithmetic mean of finite run-level samples.
 * @param samples Samples to aggregate.
 * @return Sample mean, or `std::nullopt` for an empty sample set.
 */
std::optional<double> sample_mean(const std::vector<double> &samples) {
  if (samples.empty()) {
    return std::nullopt;
  }
  return std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
}

/**
 * @brief Apply threshold and significance decisions to one metric record.
 * @param record Raw metric comparison record to extend.
 * @param baseline Baseline result document.
 * @param candidate Candidate result document.
 * @param policy Validated comparison policy.
 * @param counts Counters to update once per distinct metric identifier.
 * @param counted_metrics Metric identifiers already included in @p counts.
 * @note All run pairs of one metric receive the same aggregate decision, while
 *       their original per-run `delta` and `relative_change_percent` stay intact.
 */
void apply_policy_to_metric(ConfigValue &record, const ConfigValue &baseline,
                            const ConfigValue &candidate, const ComparisonPolicy &policy,
                            PolicyCounts &counts, std::set<std::string> &counted_metrics) {
  const std::string metric_id = field_text(&record, "metric_id");
  const std::vector<double> baseline_samples = run_level_samples(baseline, metric_id);
  const std::vector<double> candidate_samples = run_level_samples(candidate, metric_id);
  record.set("baseline_samples", numeric_array(baseline_samples));
  record.set("candidate_samples", numeric_array(candidate_samples));
  record.set("baseline_sample_count",
             ConfigValue::integer(static_cast<std::int64_t>(baseline_samples.size())));
  record.set("candidate_sample_count",
             ConfigValue::integer(static_cast<std::int64_t>(candidate_samples.size())));

  const std::string unit = field_text(&record, "unit");
  const MetricPolarity polarity = metric_polarity(metric_id, unit);
  record.set("polarity", ConfigValue::string(std::string(polarity_token(polarity))));

  const std::string comparison_status = field_text(&record, "comparison_status");
  const bool matched = comparison_status == "raw_difference";
  const bool count_this = matched && counted_metrics.insert(metric_id).second;
  if (count_this) {
    ++counts.compared;
  }

  const std::optional<double> baseline_mean = sample_mean(baseline_samples);
  const std::optional<double> candidate_mean = sample_mean(candidate_samples);
  std::optional<double> aggregate_delta;
  std::optional<double> aggregate_relative;
  std::string threshold_reason;
  if (baseline_mean.has_value() && candidate_mean.has_value()) {
    const double delta = *candidate_mean - *baseline_mean;
    if (std::isfinite(delta)) {
      aggregate_delta = delta;
      if (*baseline_mean != 0.0) {
        const double relative = delta / *baseline_mean * 100.0;
        if (std::isfinite(relative)) {
          aggregate_relative = relative;
        }
      }
    }
    if (!aggregate_relative.has_value()) {
      threshold_reason = "baseline sample mean is zero or the relative change is not finite";
    }
  } else {
    threshold_reason = "no finite run-level samples on both sides";
  }
  add_optional_number(record, "baseline_mean", baseline_mean);
  add_optional_number(record, "candidate_mean", candidate_mean);
  add_optional_number(record, "aggregate_delta", aggregate_delta);
  add_optional_number(record, "aggregate_relative_change_percent", aggregate_relative);

  if (!matched) {
    const std::string reason = comparison_status == "unmatched"
                                   ? "unmatched metrics are not significance-tested"
                                   : "no finite aggregate value to compare";
    record.set("threshold_status", status_record("not_applicable", reason));
    record.set("threshold_exceeded", ConfigValue::boolean(false));
    record.set("significance", status_record("not_applicable", reason));
    record.set("classification", ConfigValue::string("within_variability"));
    return;
  }

  const bool threshold_exceeded =
      aggregate_relative.has_value() && std::fabs(*aggregate_relative) >= policy.threshold_percent;
  record.set("threshold_status",
             aggregate_relative.has_value()
                 ? status_record("applied", "absolute relative change of run-level sample means "
                                            "compared with the configured percentage threshold")
                 : status_record("not_applicable", threshold_reason));
  record.set("threshold_exceeded", ConfigValue::boolean(threshold_exceeded));

  BootstrapOutcome outcome;
  if (polarity == MetricPolarity::unspecified) {
    outcome.reason = "metric polarity is not registered";
  } else {
    outcome = bootstrap_mean_difference(baseline_samples, candidate_samples, policy,
                                        aggregate_delta.value_or(0.0));
  }
  ConfigValue significance =
      status_record(outcome.applied ? "applied" : "not_applicable", outcome.reason);
  significance.set("method", ConfigValue::string("bootstrap_percentile"));
  significance.set("alpha", ConfigValue::real(policy.alpha));
  significance.set("sided", ConfigValue::string(std::string(sidedness_token(policy.sidedness))));
  significance.set("significant", ConfigValue::boolean(outcome.significant));
  add_optional_number(significance, "interval_low", outcome.low);
  add_optional_number(significance, "interval_high", outcome.high);
  record.set("significance", std::move(significance));

  const std::string_view classification =
      classify_metric(threshold_exceeded, outcome, aggregate_delta.value_or(0.0), polarity);
  record.set("classification", ConfigValue::string(std::string(classification)));
  if (!count_this) {
    return;
  }
  if (outcome.applied) {
    ++counts.tested;
  }
  if (classification == "regression") {
    ++counts.regressions;
  } else if (classification == "improvement") {
    ++counts.improvements;
  } else if (classification == "raw_difference") {
    ++counts.raw_differences;
  } else {
    ++counts.within_variability;
  }
}

/**
 * @brief Fill threshold, significance and classification fields in a comparison.
 * @param record Raw comparison record produced by @ref compare_result_documents.
 * @param baseline Baseline result document.
 * @param candidate Candidate result document.
 * @param policy Validated comparison policy.
 * @note The original metric evidence and comparability warnings are preserved.
 */
void apply_policy(ConfigValue &record, const ConfigValue &baseline, const ConfigValue &candidate,
                  const ComparisonPolicy &policy) {
  PolicyCounts counts;
  std::set<std::string> counted_metrics;
  ConfigValue::Array *metrics = record.find("metrics")->as_array();
  std::for_each(metrics->begin(), metrics->end(), [&](ConfigValue &metric) {
    apply_policy_to_metric(metric, baseline, candidate, policy, counts, counted_metrics);
  });

  ConfigValue statistics = status_record("applied", "threshold and bootstrap checks applied once "
                                                    "per distinct metric identifier");
  statistics.set("method", ConfigValue::string("bootstrap_percentile"));
  statistics.set("threshold_percent", ConfigValue::real(policy.threshold_percent));
  statistics.set("alpha", ConfigValue::real(policy.alpha));
  statistics.set("sided", ConfigValue::string(std::string(sidedness_token(policy.sidedness))));
  statistics.set("iterations", ConfigValue::integer(policy.bootstrap_iterations));
  statistics.set("seed", ConfigValue::integer(static_cast<std::int64_t>(policy.seed)));
  statistics.set("multiplicity", ConfigValue::string("unadjusted"));
  statistics.set("metrics_compared",
                 ConfigValue::integer(static_cast<std::int64_t>(counts.compared)));
  statistics.set("metrics_tested", ConfigValue::integer(static_cast<std::int64_t>(counts.tested)));
  statistics.set("outliers_removed", ConfigValue::integer(0));
  record.set("statistics", std::move(statistics));

  ConfigValue significance = status_record(
      counts.tested == 0U ? "not_applicable" : "applied",
      counts.tested == 0U ? "no metric had enough run-level samples and a known polarity"
                          : "bootstrap percentile intervals over distinct run-level metric values");
  significance.set("method", ConfigValue::string("bootstrap_percentile"));
  significance.set("regressions",
                   ConfigValue::integer(static_cast<std::int64_t>(counts.regressions)));
  significance.set("improvements",
                   ConfigValue::integer(static_cast<std::int64_t>(counts.improvements)));
  significance.set("raw_differences",
                   ConfigValue::integer(static_cast<std::int64_t>(counts.raw_differences)));
  significance.set("within_variability",
                   ConfigValue::integer(static_cast<std::int64_t>(counts.within_variability)));
  record.set("significance", std::move(significance));

  record.set("threshold_percent", ConfigValue::real(policy.threshold_percent));
  record.set("threshold_status", status_record("applied", "configured percentage threshold applied "
                                                          "to absolute relative change"));
  record.set("classification", ConfigValue::string(std::string(overall_classification(counts))));
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

/**
 * @brief Build a comparison record, optionally classified by a policy.
 * @param baseline Baseline result document.
 * @param candidate Candidate result document.
 * @param references Caller-owned labels for the two documents.
 * @param policy Optional P4.9 classification policy.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return Comparison record on success, or `std::nullopt` on validation failure.
 */
std::optional<ConfigValue> compare_documents(const ConfigValue &baseline,
                                             const ConfigValue &candidate,
                                             const ComparisonReferences &references,
                                             const std::optional<ComparisonPolicy> &policy,
                                             std::string *error) {
  std::optional<ComparisonPolicy> effective_policy;
  if (policy.has_value()) {
    if (!validate_policy(*policy, error)) {
      return std::nullopt;
    }
    effective_policy = normalized_policy(*policy);
  }

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
  record.set("threshold_status", not_applicable("threshold checks were not requested"));
  record.set("statistics", not_applicable("variability statistics were not requested"));
  record.set("significance", not_applicable("significance tests were not requested"));
  record.set("classification", ConfigValue::string("raw_difference"));
  record.set("comparability_warnings", metadata.warnings);
  record.set("metrics", std::move(metrics));
  record.set("context", std::move(context));

  if (effective_policy.has_value()) {
    apply_policy(record, baseline, candidate, *effective_policy);
  }
  return record;
}

} // namespace

// Public raw-comparison entry point: exercised by the test target, which is
// outside the cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
std::optional<ConfigValue> compare_result_documents(const ConfigValue &baseline,
                                                    const ConfigValue &candidate,
                                                    const ComparisonReferences &references,
                                                    std::string *error) {
  return compare_documents(baseline, candidate, references, std::nullopt, error);
}

std::optional<ConfigValue>
compare_result_documents_with_policy(const ConfigValue &baseline, const ConfigValue &candidate,
                                     const ComparisonReferences &references,
                                     const ComparisonPolicy &policy, std::string *error) {
  return compare_documents(baseline, candidate, references, std::optional<ComparisonPolicy>{policy},
                           error);
}

} // namespace ztick
