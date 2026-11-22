// SPDX-License-Identifier: MIT
/**
 * @file workload.hpp
 * @brief Workload models for processor, chain and engine levels (REQ-BENCH-01).
 *
 * A workload is converted to and from the `workload` section of the portable
 * config document described in RESULTS_FORMAT §6. Generator parameters are kept
 * as @ref ztick::ConfigValue data because their fixed definitions are introduced
 * by a later phase; this keeps the model independent of generator implementations.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_WORKLOAD_HPP
#define ZTICK_WORKLOAD_HPP

#include "ztick/config_value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztick {

/** @brief Supported workload levels, matching BENCHMARK_SPECIFICATION §2. */
enum class WorkloadLevel : std::uint8_t {
  /** @brief One DSP processor benchmarked alone. */
  processor = 0,
  /** @brief An ordered sequence of processors benchmarked as one workload. */
  chain = 1,
  /** @brief An integrated DSP engine benchmarked as a whole. */
  engine = 2,
};

/**
 * @brief Parse a config token into a @ref WorkloadLevel.
 * @param token Token from the config document, such as `processor`.
 * @return The matching level, or `std::nullopt` for an unknown token.
 */
std::optional<WorkloadLevel> parse_workload_level(std::string_view token) noexcept;

/**
 * @brief Render a @ref WorkloadLevel as its config token.
 * @param level Level to render.
 * @return Stable token used by the config document.
 */
std::string_view workload_level_name(WorkloadLevel level) noexcept;

/**
 * @brief Config-shaped representation of one benchmark workload.
 * @note All fields are round-tripped; no field is omitted when empty.
 */
struct Workload {
  /** @brief Level that determines how @ref processors are interpreted. */
  WorkloadLevel level{WorkloadLevel::processor};
  /** @brief Ordered processor or engine identifiers for this workload. */
  std::vector<std::string> processors;
  /** @brief Generator settings object; semantics are defined by a later phase. */
  ConfigValue generator{ConfigValue::object()};
};

/**
 * @brief Render a workload as the `workload` section of a config document.
 * @param workload Workload to render.
 * @return An object with `level`, `processors` and `generator` members.
 */
ConfigValue workload_to_config(const Workload &workload);

/**
 * @brief Build a workload from the `workload` section of a config document.
 * @param data Config-shaped object holding workload settings.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return The parsed workload, or `std::nullopt` when the data is malformed.
 * @note A malformed section never yields a partially populated workload.
 */
std::optional<Workload> workload_from_config(const ConfigValue &data, std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_WORKLOAD_HPP
