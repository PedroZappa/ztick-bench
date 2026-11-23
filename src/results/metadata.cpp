// SPDX-License-Identifier: MIT
/**
 * @file metadata.cpp
 * @brief Implementation of the metadata attachment module declared in
 *        @ref metadata.hpp.
 */

#include "ztick/metadata.hpp"

#include "ztick/json.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @brief Git commit captured by CMake, or `unavailable` when Git is absent.
 */
#ifndef ZTICK_BUILD_GIT_COMMIT
#define ZTICK_BUILD_GIT_COMMIT "unavailable"
#endif

/**
 * @brief C++ compiler identifier captured by CMake.
 */
#ifndef ZTICK_BUILD_COMPILER
#define ZTICK_BUILD_COMPILER "unavailable"
#endif

/**
 * @brief C++ compiler version captured by CMake.
 */
#ifndef ZTICK_BUILD_COMPILER_VERSION
#define ZTICK_BUILD_COMPILER_VERSION "unavailable"
#endif

/**
 * @brief Optimization and warning flags captured by CMake.
 */
#ifndef ZTICK_BUILD_FLAGS
#define ZTICK_BUILD_FLAGS "unavailable"
#endif

/**
 * @brief Build configuration captured by CMake.
 */
#ifndef ZTICK_BUILD_CONFIGURATION
#define ZTICK_BUILD_CONFIGURATION "unavailable"
#endif

/**
 * @brief Tool version captured by CMake.
 */
#ifndef ZTICK_BUILD_PROJECT_VERSION
#define ZTICK_BUILD_PROJECT_VERSION "unavailable"
#endif

namespace ztick {

namespace {

/**
 * @brief Render one config value as plain text.
 * @param value Value to render.
 * @return String text, compact JSON for containers, empty for null.
 * @note Numbers and booleans use the JSON writer so text is stable and directly
 *       comparable with the canonical document.
 */
std::string render_value(const ConfigValue &value) {
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
 * @brief Read one named field as plain text.
 * @param owner Object to inspect, or `nullptr`.
 * @param key Field name to read.
 * @return Rendered field text, or empty when absent.
 */
std::string field_text(const ConfigValue *owner, std::string_view key) {
  if (owner == nullptr) {
    return {};
  }
  const ConfigValue *field = owner->find(key);
  return field == nullptr ? std::string{} : render_value(*field);
}

/**
 * @brief Walk a dotted field path and render its leaf.
 * @param root Object to start from.
 * @param path Field names in traversal order.
 * @return Rendered leaf text, or empty when any step is absent.
 */
std::string nested_text(const ConfigValue &root, std::initializer_list<std::string_view> path) {
  const ConfigValue *current = &root;
  for (const std::string_view key : path) {
    current = current->find(key);
    if (current == nullptr) {
      return {};
    }
  }
  return render_value(*current);
}

/**
 * @brief Turn config text into a present or unavailable metadata field.
 * @param name Stable field name.
 * @param text Rendered value text.
 * @return Value field when @p text is non-empty, otherwise unavailable with reason.
 */
MetadataField present_or_unavailable(std::string name, std::string text) {
  if (text.empty()) {
    return metadata_unavailable(std::move(name), "not present in config snapshot");
  }
  return metadata_value(std::move(name), std::move(text));
}

/**
 * @brief Turn a captured build string into a present or unavailable field.
 * @param name Stable field name.
 * @param text Captured build value.
 * @return Value field when @p text is non-empty, otherwise unavailable with reason.
 */
MetadataField build_or_unavailable(std::string name, std::string text) {
  if (text.empty()) {
    return metadata_unavailable(std::move(name), "not discoverable at build time");
  }
  return metadata_value(std::move(name), std::move(text));
}

/**
 * @brief Read the status and value of one metadata record.
 * @note An empty status means the record is absent and must be ignored.
 */
struct RecordView {
  /** @brief Stored status token, or empty when the record is absent. */
  std::string status;
  /** @brief Stored value text, or empty for a non-value record. */
  std::string value;
};

/**
 * @brief Extract one record from a metadata object.
 * @param section Metadata object to inspect, or `nullptr`.
 * @param name Field name to read.
 * @return Status and value view with an empty status when absent.
 */
RecordView record_view(const ConfigValue *section, std::string_view name) {
  const ConfigValue *record = section == nullptr ? nullptr : section->find(name);
  if (record == nullptr || record->type() != ConfigValueType::object) {
    return {};
  }
  return RecordView{field_text(record, "status"), field_text(record, "value")};
}

/**
 * @brief Build one comparability warning record.
 * @param field Qualified field name such as `machine.cpu_model`.
 * @param left Baseline record view.
 * @param right Candidate record view.
 * @param message Human-readable explanation of the mismatch.
 * @return Warning record with category, field, both sides and message.
 */
ConfigValue comparison_warning(std::string_view field, const RecordView &left,
                               const RecordView &right, std::string message) {
  ConfigValue warning = ConfigValue::object();
  warning.set("category", ConfigValue::string("comparability"));
  warning.set("field", ConfigValue::string(std::string(field)));
  warning.set("left_status", ConfigValue::string(left.status));
  warning.set("left_value", ConfigValue::string(left.value));
  warning.set("right_status", ConfigValue::string(right.status));
  warning.set("right_value", ConfigValue::string(right.value));
  warning.set("message", ConfigValue::string(std::move(message)));
  return warning;
}

/**
 * @brief Compare one metadata section and append warnings for differences.
 * @param section_name Qualified prefix such as `machine`.
 * @param left_section Baseline metadata object, or `nullptr`.
 * @param right_section Candidate metadata object, or `nullptr`.
 * @param comparison Comparison result to update.
 */
void compare_section(std::string_view section_name, const ConfigValue *left_section,
                     const ConfigValue *right_section, MetadataComparison &comparison) {
  const ConfigValue *sections[2]{left_section, right_section};
  std::vector<std::string> names;
  for (const ConfigValue *section : sections) {
    if (section == nullptr || section->type() != ConfigValueType::object) {
      continue;
    }
    for (const auto &[name, value] : *(section->as_object())) {
      (void)value;
      const bool seen = std::any_of(names.begin(), names.end(),
                                    [&name](const std::string &known) { return known == name; });
      if (!seen) {
        names.push_back(name);
      }
    }
  }

  for (const std::string &name : names) {
    const RecordView left = record_view(left_section, name);
    const RecordView right = record_view(right_section, name);
    if (left.status.empty() || right.status.empty()) {
      continue;
    }

    const std::string qualified = std::string(section_name) + "." + name;
    std::string message;
    if (left.status == "value" && right.status == "value") {
      if (left.value != right.value) {
        message = qualified + " differs between documents";
      }
    } else if (left.status != right.status) {
      message = qualified + " availability differs between documents";
    }
    if (message.empty()) {
      continue;
    }

    comparison.warnings.push_back(comparison_warning(qualified, left, right, std::move(message)));
    ++comparison.difference_count;
    comparison.comparable = false;
  }
}

} // namespace

std::string_view metadata_status_token(MetadataStatus status) noexcept {
  switch (status) {
  case MetadataStatus::value:
    return "value";
  case MetadataStatus::unavailable:
    return "unavailable";
  case MetadataStatus::unsupported:
    return "unsupported";
  case MetadataStatus::not_applicable:
    return "not_applicable";
  }
  return "unavailable";
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
std::optional<MetadataStatus> parse_metadata_status(std::string_view token) noexcept {
  for (const MetadataStatus status :
       {MetadataStatus::value, MetadataStatus::unavailable, MetadataStatus::unsupported,
        MetadataStatus::not_applicable}) {
    if (token == metadata_status_token(status)) {
      return status;
    }
  }
  return std::nullopt;
}

MetadataField metadata_value(std::string name, std::string value) {
  return MetadataField{std::move(name), MetadataStatus::value, std::move(value), {}};
}

MetadataField metadata_unavailable(std::string name, std::string reason) {
  return MetadataField{std::move(name), MetadataStatus::unavailable, {}, std::move(reason)};
}

MetadataField metadata_unsupported(std::string name, std::string reason) {
  return MetadataField{std::move(name), MetadataStatus::unsupported, {}, std::move(reason)};
}

MetadataField metadata_not_applicable(std::string name, std::string reason) {
  return MetadataField{std::move(name), MetadataStatus::not_applicable, {}, std::move(reason)};
}

ConfigValue make_metadata_object(const std::vector<MetadataField> &fields) {
  ConfigValue object = ConfigValue::object();
  for (const MetadataField &field : fields) {
    ConfigValue record = ConfigValue::object();
    record.set("status", ConfigValue::string(std::string(metadata_status_token(field.status))));
    record.set("value", ConfigValue::string(field.value));
    record.set("reason", ConfigValue::string(field.reason));
    object.set(field.name, std::move(record));
  }
  return object;
}

BuildInfo current_build_info() {
  return BuildInfo{ZTICK_BUILD_GIT_COMMIT, ZTICK_BUILD_COMPILER,      ZTICK_BUILD_COMPILER_VERSION,
                   ZTICK_BUILD_FLAGS,      ZTICK_BUILD_CONFIGURATION, ZTICK_BUILD_PROJECT_VERSION};
}

ConfigValue build_metadata(const BuildInfo &info) {
  const std::vector<MetadataField> fields{
      build_or_unavailable("git_commit", info.git_commit),
      build_or_unavailable("compiler", info.compiler),
      build_or_unavailable("compiler_version", info.compiler_version),
      build_or_unavailable("optimization_flags", info.optimization_flags),
      build_or_unavailable("build_configuration", info.build_configuration),
      build_or_unavailable("project_version", info.project_version),
  };
  return make_metadata_object(fields);
}

ConfigValue build_metadata() { return build_metadata(current_build_info()); }

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ConfigValue workload_metadata(const ConfigValue &config_snapshot) {
  const std::vector<MetadataField> fields{
      present_or_unavailable("integration_id", nested_text(config_snapshot, {"integration", "id"})),
      present_or_unavailable("integration_version",
                             nested_text(config_snapshot, {"integration", "version"})),
      present_or_unavailable("workload_level", nested_text(config_snapshot, {"workload", "level"})),
      present_or_unavailable("processors",
                             nested_text(config_snapshot, {"workload", "processors"})),
      present_or_unavailable("generator_type",
                             nested_text(config_snapshot, {"workload", "generator", "type"})),
      present_or_unavailable("sample_rate_hz",
                             nested_text(config_snapshot, {"dimensions", "sample_rate_hz"})),
      present_or_unavailable("block_size",
                             nested_text(config_snapshot, {"dimensions", "block_size"})),
      present_or_unavailable("channels", nested_text(config_snapshot, {"dimensions", "channels"})),
      present_or_unavailable("oversampling",
                             nested_text(config_snapshot, {"dimensions", "oversampling"})),
      present_or_unavailable("instances",
                             nested_text(config_snapshot, {"dimensions", "instances"})),
  };
  return make_metadata_object(fields);
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
ConfigValue environment_metadata(const ConfigValue &config_snapshot) {
  const std::vector<MetadataField> fields{
      present_or_unavailable("isolation", nested_text(config_snapshot, {"execution", "isolation"})),
      present_or_unavailable("duration_mode",
                             nested_text(config_snapshot, {"execution", "duration_mode"})),
      present_or_unavailable("repetitions",
                             nested_text(config_snapshot, {"execution", "repetitions"})),
      present_or_unavailable("workers", nested_text(config_snapshot, {"parallelism", "workers"})),
      present_or_unavailable("affinity", nested_text(config_snapshot, {"parallelism", "affinity"})),
      present_or_unavailable("detail", nested_text(config_snapshot, {"measurement", "detail"})),
  };
  return make_metadata_object(fields);
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
bool attach_metadata(ConfigValue &document, const ConfigValue &build, const ConfigValue &machine,
                     const ConfigValue &environment, std::string *error) {
  if (document.type() != ConfigValueType::object) {
    if (error != nullptr) {
      *error = "result document must be an object";
    }
    return false;
  }
  const std::array<std::pair<std::string_view, const ConfigValue *>, 3> sections{{
      {"build", &build},
      {"machine", &machine},
      {"environment", &environment},
  }};
  for (const auto &[name, section] : sections) {
    if (section->type() != ConfigValueType::object) {
      if (error != nullptr) {
        *error = std::string(name) + " metadata must be an object";
      }
      return false;
    }
  }

  document.set("build", build);
  document.set("machine", machine);
  document.set("environment", environment);
  return true;
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
MetadataComparison compare_metadata(const ConfigValue &left, const ConfigValue &right) {
  MetadataComparison comparison;
  const ConfigValue *sections[2]{&left, &right};
  const bool invalid = std::any_of(sections, sections + 2, [](const ConfigValue *document) {
    return document->type() != ConfigValueType::object;
  });
  if (invalid) {
    ConfigValue warning = ConfigValue::object();
    warning.set("category", ConfigValue::string("comparability"));
    warning.set("field", ConfigValue::string("document"));
    warning.set("message", ConfigValue::string("result document must be an object"));
    comparison.warnings.push_back(std::move(warning));
    ++comparison.difference_count;
    comparison.comparable = false;
    return comparison;
  }

  compare_section("build", left.find("build"), right.find("build"), comparison);
  compare_section("machine", left.find("machine"), right.find("machine"), comparison);
  compare_section("environment", left.find("environment"), right.find("environment"), comparison);
  return comparison;
}

} // namespace ztick
