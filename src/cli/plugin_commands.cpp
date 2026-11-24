// SPDX-License-Identifier: MIT
/**
 * @file plugin_commands.cpp
 * @brief Implementation of the `plugin list` and `plugin inspect` commands.
 *
 * Both commands stay metadata-only: discovery enumerates candidate paths, the
 * host loader resolves a read-only descriptor data symbol, and the trust list is
 * read without prompting. Every unavailable or malformed input is rendered as an
 * explicit state rather than silently dropped (BR §6.1, §6.2, §17).
 */

#include "ztick/plugin_commands.hpp"

#include <ztick/sdk.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztick::cli {

namespace {

/**
 * @brief Render one SDK status code as a stable token.
 * @param status `ztick_status` code to name.
 * @return Stable lowercase token, or `unknown` for an unrecognized code.
 */
std::string_view status_token(std::int32_t status) noexcept {
  switch (status) {
  case ztick_status_success:
    return "success";
  case ztick_status_invalid_argument:
    return "invalid_argument";
  case ztick_status_incompatible:
    return "incompatible";
  case ztick_status_unsupported:
    return "unsupported";
  case ztick_status_internal_error:
    return "internal_error";
  }
  return "unknown";
}

/**
 * @brief Provide display text for an optional static reason string.
 * @param reason Reason to display, possibly null.
 * @return Reason text, or an explicit placeholder.
 */
std::string reason_text(const char *reason) {
  return reason == nullptr || reason[0] == '\0' ? std::string("no reason recorded")
                                                : std::string(reason);
}

/**
 * @brief Format one three-part version.
 * @param major Major component.
 * @param minor Minor component.
 * @param patch Patch component.
 * @return Dotted version text.
 */
std::string version_text(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
  return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

/**
 * @brief Format one capability bitmask as a fixed-width hexadecimal string.
 * @param capabilities Bitmask to render.
 * @return Text of the form `0x` followed by 16 hexadecimal digits.
 */
std::string capabilities_text(std::uint64_t capabilities) {
  char buffer[19U];
  std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(capabilities));
  return std::string(buffer);
}

/**
 * @brief Decide whether an identifier is present in the trusted list.
 * @param trust Trust-list result to search.
 * @param identifier Stable identifier to find.
 * @return True only when the list was read successfully and contains the identifier.
 */
bool is_trusted(const ztick::plugin::TrustListResult &trust, const std::string &identifier) {
  if (!trust.status.ok()) {
    return false;
  }
  return std::any_of(trust.entries.begin(), trust.entries.end(),
                     [&identifier](const ztick::plugin::PluginIdentity &entry) {
                       return entry.identifier == identifier;
                     });
}

/**
 * @brief Render one trust state for a plugin.
 * @param trust Trust-list result.
 * @param identifier Stable identifier to find.
 * @return `trusted`, `untrusted` or `unknown` when the list is unavailable.
 */
std::string_view trust_state(const ztick::plugin::TrustListResult &trust,
                             const std::string &identifier) {
  if (!trust.status.ok()) {
    return "unknown";
  }
  return is_trusted(trust, identifier) ? "trusted" : "untrusted";
}

/**
 * @brief Build the structured trust-list object shared by both commands.
 * @param trust Trust-list result to encode.
 * @return Object with status, reason and every trusted identity.
 */
ztick::ConfigValue trust_data(const ztick::plugin::TrustListResult &trust) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("status", ztick::ConfigValue::string(trust.status.ok() ? "available" : "unavailable"));
  data.set("reason",
           ztick::ConfigValue::string(trust.status.ok() ? "" : reason_text(trust.status.reason)));
  ztick::ConfigValue entries = ztick::ConfigValue::array();
  for (const ztick::plugin::PluginIdentity &entry : trust.entries) {
    ztick::ConfigValue identity = ztick::ConfigValue::object();
    identity.set("identifier", ztick::ConfigValue::string(entry.identifier));
    identity.set("name", ztick::ConfigValue::string(entry.name));
    identity.set("version", ztick::ConfigValue::string(version_text(
                                entry.adapter_major, entry.adapter_minor, entry.adapter_patch)));
    identity.set("sdk_version", ztick::ConfigValue::string(version_text(
                                    entry.sdk_major, entry.sdk_minor, entry.sdk_patch)));
    identity.set("capabilities", ztick::ConfigValue::string(capabilities_text(entry.capabilities)));
    entries.push_back(std::move(identity));
  }
  data.set("entries", std::move(entries));
  return data;
}

/**
 * @brief Build the structured metadata object for one inspected plugin.
 * @param metadata Owning metadata from a successful inspection.
 * @return Object with identity, versions and declared capabilities.
 */
ztick::ConfigValue metadata_data(const ztick::plugin::AdapterMetadata &metadata) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("identifier", ztick::ConfigValue::string(metadata.identifier));
  data.set("name", ztick::ConfigValue::string(metadata.name));
  data.set("version", ztick::ConfigValue::string(version_text(
                          metadata.adapter_major, metadata.adapter_minor, metadata.adapter_patch)));
  data.set("sdk_version", ztick::ConfigValue::string(version_text(
                              metadata.sdk_major, metadata.sdk_minor, metadata.sdk_patch)));
  data.set("capabilities", ztick::ConfigValue::string(capabilities_text(metadata.capabilities)));
  data.set("metadata", ztick::ConfigValue::string(metadata.metadata));
  return data;
}

} // namespace

std::optional<PluginListInvocation>
parse_plugin_list_arguments(const std::vector<std::string_view> &args, std::string *error) {
  PluginListInvocation invocation;
  for (std::size_t index = 0U; index < args.size(); ++index) {
    const std::string_view argument = args[index];
    std::string_view path;
    if (argument == "--path") {
      if (index + 1U >= args.size()) {
        if (error != nullptr) {
          *error = "option --path requires a value";
        }
        return std::nullopt;
      }
      path = args[++index];
    } else if (argument.starts_with("--path=")) {
      path = argument.substr(7U);
    } else if (argument.starts_with("--")) {
      if (error != nullptr) {
        *error = "unknown option '" + std::string(argument) + "'";
      }
      return std::nullopt;
    } else {
      if (error != nullptr) {
        *error = "'plugin list' accepts only --path options";
      }
      return std::nullopt;
    }
    if (path.empty()) {
      if (error != nullptr) {
        *error = "option --path requires a non-empty value";
      }
      return std::nullopt;
    }
    invocation.explicit_paths.emplace_back(path);
  }
  return invocation;
}

PluginCommandServices default_plugin_command_services() {
  static const ztick::plugin::FileTrustStore store(
      ztick::plugin::default_trust_list_path().value_or(""));
  PluginCommandServices services;
  services.sources = ztick::plugin::default_plugin_discovery_sources();
  services.loader = &ztick::plugin::platform_library_loader();
  services.enumerator = &ztick::plugin::platform_plugin_directory_enumerator();
  services.trust_store = &store;
  return services;
}

std::string render_plugin_list(const ztick::plugin::DiscoveryResult &discovery,
                               const ztick::plugin::TrustListResult &trust) {
  std::string out = "ztick-bench: discovered plugins\n";
  out += "directories:\n";
  for (const ztick::plugin::PluginDirectoryStatus &directory : discovery.directories) {
    out += "  ";
    out += directory.path.empty() ? "<not configured>" : directory.path;
    out += ": ";
    out += directory.available ? "available" : "unavailable";
    out += " (";
    out += directory.reason.empty() ? (directory.available ? "no reason recorded" : "no reason")
                                    : directory.reason;
    out += ")\n";
  }
  out += "trust: ";
  out += trust.status.ok() ? "available" : "unavailable";
  out += " (";
  out += trust.status.ok() ? "no reason recorded" : reason_text(trust.status.reason);
  out += ")\n";
  out += "plugins: " + std::to_string(discovery.plugins.size()) + "\n";
  for (std::size_t index = 0U; index < discovery.plugins.size(); ++index) {
    const ztick::plugin::DiscoveredPlugin &plugin = discovery.plugins[index];
    out += "  [" + std::to_string(index) + "] " + plugin.path +
           " source=" + std::string(ztick::plugin::plugin_source_token(plugin.source)) +
           " status=" + std::string(status_token(plugin.inspection.status)) + "\n";
    if (plugin.inspection.has_metadata()) {
      const ztick::plugin::AdapterMetadata &metadata = plugin.inspection.metadata;
      out += "      identifier=" + metadata.identifier + " name=" + metadata.name + "\n";
      out += "      version=" +
             version_text(metadata.adapter_major, metadata.adapter_minor, metadata.adapter_patch) +
             " sdk=" + version_text(metadata.sdk_major, metadata.sdk_minor, metadata.sdk_patch) +
             " capabilities=" + capabilities_text(metadata.capabilities) +
             " trust=" + std::string(trust_state(trust, metadata.identifier)) + "\n";
    } else {
      out += "      reason=" + reason_text(plugin.inspection.reason) + "\n";
    }
  }
  return out;
}

std::string render_plugin_inspection(std::string_view path,
                                     const ztick::plugin::InspectionResult &inspection,
                                     const ztick::plugin::NegotiationResult &compatibility,
                                     const ztick::plugin::TrustListResult &trust) {
  std::string out = "ztick-bench: plugin inspection\n";
  out += "path: " + std::string(path) + "\n";
  out += "status: " + std::string(status_token(inspection.status)) + " (" +
         reason_text(inspection.reason) + ")\n";
  if (inspection.has_metadata()) {
    const ztick::plugin::AdapterMetadata &metadata = inspection.metadata;
    out += "identifier: " + metadata.identifier + "\n";
    out += "name: " + metadata.name + "\n";
    out += "version: " +
           version_text(metadata.adapter_major, metadata.adapter_minor, metadata.adapter_patch) +
           "\n";
    out +=
        "sdk: " + version_text(metadata.sdk_major, metadata.sdk_minor, metadata.sdk_patch) + "\n";
    out += "capabilities: " + capabilities_text(metadata.capabilities) + "\n";
    out += "metadata: " + (metadata.metadata.empty() ? std::string("<empty>") : metadata.metadata) +
           "\n";
    out += "trust: " + std::string(trust_state(trust, metadata.identifier)) + " (";
    out += trust.status.ok() ? "no reason recorded" : reason_text(trust.status.reason);
    out += ")\n";
  }
  out += "compatibility: " + std::string(status_token(compatibility.status)) + " (" +
         reason_text(compatibility.reason) + ")\n";
  return out;
}

ztick::ConfigValue plugin_list_data(const ztick::plugin::DiscoveryResult &discovery,
                                    const ztick::plugin::TrustListResult &trust) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  ztick::ConfigValue directories = ztick::ConfigValue::array();
  for (const ztick::plugin::PluginDirectoryStatus &directory : discovery.directories) {
    ztick::ConfigValue entry = ztick::ConfigValue::object();
    entry.set("path", ztick::ConfigValue::string(directory.path));
    entry.set("status",
              ztick::ConfigValue::string(directory.available ? "available" : "unavailable"));
    entry.set("reason", ztick::ConfigValue::string(directory.reason));
    directories.push_back(std::move(entry));
  }
  data.set("directories", std::move(directories));
  data.set("trust", trust_data(trust));

  ztick::ConfigValue plugins = ztick::ConfigValue::array();
  for (const ztick::plugin::DiscoveredPlugin &plugin : discovery.plugins) {
    ztick::ConfigValue entry = ztick::ConfigValue::object();
    entry.set("path", ztick::ConfigValue::string(plugin.path));
    entry.set("source", ztick::ConfigValue::string(
                            std::string(ztick::plugin::plugin_source_token(plugin.source))));
    entry.set("status",
              ztick::ConfigValue::string(std::string(status_token(plugin.inspection.status))));
    entry.set("reason", ztick::ConfigValue::string(plugin.inspection.has_metadata()
                                                       ? ""
                                                       : reason_text(plugin.inspection.reason)));
    if (plugin.inspection.has_metadata()) {
      const ztick::plugin::AdapterMetadata &metadata = plugin.inspection.metadata;
      entry.set("identifier", ztick::ConfigValue::string(metadata.identifier));
      entry.set("name", ztick::ConfigValue::string(metadata.name));
      entry.set("version",
                ztick::ConfigValue::string(version_text(
                    metadata.adapter_major, metadata.adapter_minor, metadata.adapter_patch)));
      entry.set("sdk_version", ztick::ConfigValue::string(version_text(
                                   metadata.sdk_major, metadata.sdk_minor, metadata.sdk_patch)));
      entry.set("capabilities",
                ztick::ConfigValue::string(capabilities_text(metadata.capabilities)));
      entry.set("trust",
                ztick::ConfigValue::string(std::string(trust_state(trust, metadata.identifier))));
    } else {
      entry.set("trust", ztick::ConfigValue::string("unknown"));
    }
    plugins.push_back(std::move(entry));
  }
  data.set("plugins", std::move(plugins));
  return data;
}

ztick::ConfigValue plugin_inspection_data(std::string_view path,
                                          const ztick::plugin::InspectionResult &inspection,
                                          const ztick::plugin::NegotiationResult &compatibility,
                                          const ztick::plugin::TrustListResult &trust) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("path", ztick::ConfigValue::string(std::string(path)));
  data.set("status", ztick::ConfigValue::string(std::string(status_token(inspection.status))));
  data.set("reason", ztick::ConfigValue::string(reason_text(inspection.reason)));
  data.set("metadata", inspection.has_metadata() ? metadata_data(inspection.metadata)
                                                 : ztick::ConfigValue::null());
  ztick::ConfigValue compat = ztick::ConfigValue::object();
  compat.set("status", ztick::ConfigValue::string(std::string(status_token(compatibility.status))));
  compat.set("reason", ztick::ConfigValue::string(reason_text(compatibility.reason)));
  data.set("compatibility", std::move(compat));
  ztick::ConfigValue trust_entry = trust_data(trust);
  trust_entry.set("state", ztick::ConfigValue::string(
                               inspection.has_metadata()
                                   ? std::string(trust_state(trust, inspection.metadata.identifier))
                                   : "unknown"));
  data.set("trust", std::move(trust_entry));
  return data;
}

CommandResult execute_plugin_list(const std::vector<std::string_view> &args,
                                  const PluginCommandServices &services) {
  std::string usage_error;
  const std::optional<PluginListInvocation> invocation =
      parse_plugin_list_arguments(args, &usage_error);
  if (!invocation.has_value()) {
    return CommandResult{ExitCode::usage_error, {}, "ztick-bench: " + usage_error + "\n"};
  }
  if (services.loader == nullptr || services.enumerator == nullptr ||
      services.trust_store == nullptr) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: plugin services are missing\n"};
  }
  ztick::plugin::PluginDiscoverySources sources = services.sources;
  sources.explicit_paths = invocation->explicit_paths;
  const ztick::plugin::DiscoveryResult discovery =
      ztick::plugin::discover_plugins(sources, *services.enumerator, *services.loader);
  const ztick::plugin::TrustListResult trust =
      ztick::plugin::read_trust_list(*services.trust_store);
  CommandResult result{ExitCode::success, render_plugin_list(discovery, trust), {}};
  result.data = plugin_list_data(discovery, trust);
  return result;
}

CommandResult execute_plugin_list(const std::vector<std::string_view> &args) {
  return execute_plugin_list(args, default_plugin_command_services());
}

CommandResult execute_plugin_inspect(const std::vector<std::string_view> &args,
                                     const ztick::plugin::SharedLibraryLoader &loader,
                                     const ztick::plugin::TrustStore &trust_store) {
  if (args.size() != 1U) {
    return CommandResult{
        ExitCode::usage_error, {}, "ztick-bench: 'plugin inspect' requires exactly one path\n"};
  }
  const std::string path(args.front());
  const ztick::plugin::InspectionResult inspection =
      ztick::plugin::inspect_dynamic_adapter(path.c_str(), loader);
  const ztick::plugin::TrustListResult trust = ztick::plugin::read_trust_list(trust_store);

  ztick::plugin::NegotiationResult compatibility;
  if (inspection.has_metadata()) {
    compatibility = ztick::plugin::negotiate_adapter(inspection.metadata, 0U);
  } else {
    compatibility.status = ztick_status_invalid_argument;
    compatibility.reason = "metadata inspection did not produce a descriptor";
  }

  CommandResult result{inspection.has_metadata() ? ExitCode::success : ExitCode::failure,
                       render_plugin_inspection(path, inspection, compatibility, trust),
                       inspection.has_metadata() ? std::string{}
                                                 : "ztick-bench: plugin inspection failed: " +
                                                       reason_text(inspection.reason) + "\n"};
  result.data = plugin_inspection_data(path, inspection, compatibility, trust);
  return result;
}

CommandResult execute_plugin_inspect(const std::vector<std::string_view> &args) {
  const PluginCommandServices services = default_plugin_command_services();
  if (services.loader == nullptr || services.trust_store == nullptr) {
    return CommandResult{ExitCode::failure, {}, "ztick-bench: plugin services are missing\n"};
  }
  return execute_plugin_inspect(args, *services.loader, *services.trust_store);
}

} // namespace ztick::cli
