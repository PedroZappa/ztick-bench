// SPDX-License-Identifier: MIT
/**
 * @file system_command.cpp
 * @brief Implementation of the `ztick-bench system` command.
 *
 * The command owns only argument validation and text rendering. Collection stays
 * in @ref ztick::platform and the inventory-to-metadata conversion stays in
 * @ref ztick::platform::system_info_metadata, so no availability mapping is
 * duplicated in the CLI (PLATFORM_MONITORING §6, ARCHITECTURE §7).
 */

#include "ztick/system_command.hpp"

#include "ztick/platform/linux_system_info.hpp"
#include "ztick/system_info_metadata.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztick::cli {

namespace {

/**
 * @brief Render one availability-aware metadata field as a report line.
 * @param field Field to render.
 * @return Line of the form `name: status (detail)`.
 */
std::string render_field(const MetadataField &field) {
  std::string line = field.name;
  line += ": ";
  line.append(metadata_status_token(field.status));
  line += " (";
  if (field.status == MetadataStatus::value) {
    line += field.value.empty() ? "<empty>" : field.value;
  } else {
    line += field.reason.empty() ? "no reason recorded" : field.reason;
  }
  line += ')';
  return line;
}

/**
 * @brief Build one metadata field describing a failed whole-inventory collection.
 * @param collected Collector result with a non-value state.
 * @return Field named `system_inventory` preserving the state and reason.
 * @note Unknown integer states degrade to `unavailable` rather than being omitted.
 */
MetadataField inventory_field(const platform::Result<platform::SystemInfo> &collected) {
  const char *reason = collected.reason != nullptr ? collected.reason : "no reason recorded";
  switch (collected.state) {
  case ztick_availability_value:
    return metadata_value("system_inventory", "value");
  case ztick_availability_unavailable:
    return metadata_unavailable("system_inventory", reason);
  case ztick_availability_unsupported:
    return metadata_unsupported("system_inventory", reason);
  case ztick_availability_not_applicable:
    return metadata_not_applicable("system_inventory", reason);
  }
  return metadata_unavailable("system_inventory", reason);
}

} // namespace

SystemInfoCollector default_system_info_collector() {
  return []() -> platform::Result<platform::SystemInfo> {
    const auto provider = platform::linux_sysinfo::make_linux_system_info_provider();
    if (!provider) {
      return platform::make_unavailable<platform::SystemInfo>(
          "system information provider unavailable");
    }
    return provider->collect();
  };
}

std::string render_system_metadata(const std::vector<MetadataField> &fields) {
  std::string out = "ztick-bench: system information (availability states)\n";
  for (const MetadataField &field : fields) {
    out += render_field(field);
    out += '\n';
  }
  return out;
}

CommandResult execute_system(const std::vector<std::string_view> &args,
                             const SystemInfoCollector &collector) {
  if (!args.empty()) {
    return CommandResult{ExitCode::usage_error, {}, "ztick-bench: 'system' accepts no arguments\n"};
  }
  if (!collector) {
    return CommandResult{
        ExitCode::failure, {}, "ztick-bench: system information collector is missing\n"};
  }

  const auto collected = collector();
  std::vector<MetadataField> fields;
  if (collected.has_value()) {
    fields = platform::system_info_metadata(collected.value);
  } else {
    fields.push_back(inventory_field(collected));
  }

  const std::string output = render_system_metadata(fields);
  if (collected.has_value()) {
    return CommandResult{ExitCode::success, output, {}};
  }

  const MetadataField &failure = fields.front();
  return CommandResult{ExitCode::failure, output,
                       "ztick-bench: system inventory " +
                           std::string(metadata_status_token(failure.status)) + ": " +
                           failure.reason + "\n"};
}

CommandResult execute_system(const std::vector<std::string_view> &args) {
  return execute_system(args, default_system_info_collector());
}

} // namespace ztick::cli
