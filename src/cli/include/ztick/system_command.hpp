// SPDX-License-Identifier: MIT
/**
 * @file system_command.hpp
 * @brief Command-line surface of the `ztick-bench system` command.
 *
 * PROJECT_SPEC §3.3 owns the command set and BR §22 keeps this command in Phase 4.
 * PLATFORM_MONITORING §6 owns the four availability states. This module collects
 * the inventory through the platform abstraction, converts it with the existing
 * platform-to-metadata adapter and renders every field with its explicit state.
 * The collector is injected so all output and error paths are unit-testable.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_SYSTEM_COMMAND_HPP
#define ZTICK_SYSTEM_COMMAND_HPP

#include "ztick/cli.hpp"
#include "ztick/metadata.hpp"
#include "ztick/platform.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

/** @brief Command-line front end for ztick-bench. */
namespace ztick::cli {

/**
 * @brief Injectable collector for one system-inventory snapshot.
 * @note A non-value state is a valid result: it is reported in the rendered
 *       inventory instead of being treated as a missing field. Reason strings
 *       must outlive the returned result, so implementations use static text.
 */
using SystemInfoCollector = std::function<platform::Result<platform::SystemInfo>()>;

/**
 * @brief Build the production collector backed by the platform abstraction.
 * @return Callable that collects the live inventory; never null.
 * @note A missing provider is reported as an `unavailable` result rather than
 *       throwing or returning a partial inventory.
 */
SystemInfoCollector default_system_info_collector();

/**
 * @brief Render availability-aware metadata fields as deterministic text.
 * @param fields Fields to render in declaration order.
 * @return Header line followed by one `name: status (detail)` line per field.
 * @note A `value` field renders its value, while every non-value field renders
 *       its reason. Empty detail is shown explicitly so no field is omitted.
 */
std::string render_system_metadata(const std::vector<MetadataField> &fields);

/**
 * @brief Execute one `system` invocation with an injected collector.
 * @param args Arguments excluding `system`; must be empty.
 * @param collector Inventory collector to call.
 * @return Success with the rendered inventory, a usage error for extra arguments,
 *         or a failure when the collector itself reports a non-value state.
 * @note Per-field `unavailable`, `unsupported` and `not_applicable` states do not
 *       fail the command; they are printed with their reasons (BR §17).
 */
CommandResult execute_system(const std::vector<std::string_view> &args,
                             const SystemInfoCollector &collector);

/**
 * @brief Execute one `system` invocation and expose its structured inventory.
 * @param args Arguments excluding `system`; must be empty.
 * @param collector Inventory collector to call.
 * @param data Optional receiver for `{fields: <availability-aware object>}`.
 * @return Success with the rendered inventory or the appropriate error result.
 * @note The structured form uses @ref ztick::make_metadata_object, so every
 *       field carries its status, value and reason (RESULTS_FORMAT §12).
 */
CommandResult execute_system(const std::vector<std::string_view> &args,
                             const SystemInfoCollector &collector, ConfigValue *data);

/**
 * @brief Execute one `system` invocation with production platform services.
 * @param args Arguments excluding `system`; must be empty.
 * @return Success with the rendered inventory or the appropriate error result.
 */
CommandResult execute_system(const std::vector<std::string_view> &args);

} // namespace ztick::cli

#endif // ZTICK_SYSTEM_COMMAND_HPP
