// SPDX-License-Identifier: MIT
/**
 * @file plugin_commands.hpp
 * @brief Command-line surfaces of `ztick-bench plugin list` and `plugin inspect`.
 *
 * PROJECT_SPEC §3.3 owns both commands and BR §6.1 requires metadata inspection
 * without executing a plugin. These commands consume the host discovery and trust
 * modules, render explicit states for malformed or untrusted modules, and expose
 * structured data for the JSON envelope owned by RESULTS_FORMAT §12. All
 * dependencies are injectable so success, refusal and failure paths are testable.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_PLUGIN_COMMANDS_HPP
#define ZTICK_PLUGIN_COMMANDS_HPP

#include "ztick/cli.hpp"

#include <ztick/plugin/compatibility.hpp>
#include <ztick/plugin/discovery.hpp>
#include <ztick/plugin/dynamic_loader.hpp>
#include <ztick/plugin/trust.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** @brief Command-line front end for ztick-bench. */
namespace ztick::cli {

/**
 * @brief Non-owning services used by the plugin commands.
 * @note Pointers must outlive the command call. A null loader, enumerator or trust
 *       store is reported as an explicit failure instead of being silently replaced.
 */
struct PluginCommandServices {
  /** @brief Directories searched by `plugin list`. */
  ztick::plugin::PluginDiscoverySources sources;
  /** @brief Loader used for metadata-only inspection. */
  const ztick::plugin::SharedLibraryLoader *loader{nullptr};
  /** @brief Directory reader used by `plugin list`. */
  const ztick::plugin::PluginDirectoryEnumerator *enumerator{nullptr};
  /** @brief Read-only trusted-plugin list. */
  const ztick::plugin::TrustStore *trust_store{nullptr};
};

/**
 * @brief Parsed `plugin list` invocation.
 */
struct PluginListInvocation {
  /** @brief Explicit candidate paths in command-line order. */
  std::vector<std::string> explicit_paths;
};

/**
 * @brief Parse `plugin list` options.
 * @param args Arguments after `plugin list`.
 * @param error Optional receiver for a human-readable usage error.
 * @return Explicit paths in order, or `std::nullopt` for an invalid invocation.
 * @note `--path <path>` and `--path=<path>` are both accepted and may be repeated.
 *       Any positional argument or unknown option is a usage error.
 */
[[nodiscard]] std::optional<PluginListInvocation>
parse_plugin_list_arguments(const std::vector<std::string_view> &args,
                            std::string *error = nullptr);

/**
 * @brief Build production plugin-command services.
 * @return Services using the platform loader, directory paths and trusted list.
 * @note An unresolvable trusted-list path is represented by a store that reports
 *       an explicit storage failure when read.
 */
PluginCommandServices default_plugin_command_services();

/**
 * @brief Render discovered plugins and their explicit states as text.
 * @param discovery Discovery result to render.
 * @param trust Trust-list result used for per-plugin trust states.
 * @return Deterministic text ending with a newline.
 */
std::string render_plugin_list(const ztick::plugin::DiscoveryResult &discovery,
                               const ztick::plugin::TrustListResult &trust);

/**
 * @brief Render one metadata-only inspection as text.
 * @param path Path that was inspected.
 * @param inspection Inspection outcome.
 * @param compatibility Metadata compatibility outcome.
 * @param trust Trust-list result used for the trust state.
 * @return Deterministic text ending with a newline.
 */
std::string render_plugin_inspection(std::string_view path,
                                     const ztick::plugin::InspectionResult &inspection,
                                     const ztick::plugin::NegotiationResult &compatibility,
                                     const ztick::plugin::TrustListResult &trust);

/**
 * @brief Build structured data for a plugin-list result.
 * @param discovery Discovery result to encode.
 * @param trust Trust-list result to encode.
 * @return Object matching the `plugin list` data schema in RESULTS_FORMAT §12.
 */
ztick::ConfigValue plugin_list_data(const ztick::plugin::DiscoveryResult &discovery,
                                    const ztick::plugin::TrustListResult &trust);

/**
 * @brief Build structured data for one plugin-inspection result.
 * @param path Path that was inspected.
 * @param inspection Inspection outcome.
 * @param compatibility Metadata compatibility outcome.
 * @param trust Trust-list result to encode.
 * @return Object matching the `plugin inspect` data schema in RESULTS_FORMAT §12.
 */
ztick::ConfigValue plugin_inspection_data(std::string_view path,
                                          const ztick::plugin::InspectionResult &inspection,
                                          const ztick::plugin::NegotiationResult &compatibility,
                                          const ztick::plugin::TrustListResult &trust);

/**
 * @brief Execute `plugin list` with injected services.
 * @param args Arguments after `plugin list`; accepts repeated `--path` options.
 * @param services Discovery, loader, enumerator and trust services.
 * @return Success with every discovered candidate and explicit directory states,
 *         usage error for malformed arguments, or failure when a dependency is missing.
 */
CommandResult execute_plugin_list(const std::vector<std::string_view> &args,
                                  const PluginCommandServices &services);

/**
 * @brief Execute `plugin list` with production services.
 * @param args Arguments after `plugin list`; accepts repeated `--path` options.
 * @return Outcome of the plugin-list command.
 */
CommandResult execute_plugin_list(const std::vector<std::string_view> &args);

/**
 * @brief Execute `plugin inspect` with injected services.
 * @param args Arguments after `plugin inspect`; exactly one path is required.
 * @param loader Loader used for metadata-only inspection.
 * @param trust_store Read-only trusted-plugin list.
 * @return Success with the inspected metadata, usage error for wrong arguments,
 *         or failure when inspection or trust-list access fails.
 * @note No lifecycle callback or entry point is called on the inspected module.
 */
CommandResult execute_plugin_inspect(const std::vector<std::string_view> &args,
                                     const ztick::plugin::SharedLibraryLoader &loader,
                                     const ztick::plugin::TrustStore &trust_store);

/**
 * @brief Execute `plugin inspect` with production services.
 * @param args Arguments after `plugin inspect`; exactly one path is required.
 * @return Outcome of the plugin-inspection command.
 */
CommandResult execute_plugin_inspect(const std::vector<std::string_view> &args);

} // namespace ztick::cli

#endif // ZTICK_PLUGIN_COMMANDS_HPP
