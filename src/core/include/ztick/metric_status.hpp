// SPDX-License-Identifier: MIT
/**
 * @file metric_status.hpp
 * @brief Availability status shared by all reported metric values (REQ-MET-01).
 *
 * Every reported metric distinguishes measured, estimated, unavailable and
 * not-applicable values. An unavailable or not-applicable metric is still
 * reported with its status; it is never silently omitted. This header is private
 * to ztick-bench and is not installed.
 */

#ifndef ZTICK_METRIC_STATUS_HPP
#define ZTICK_METRIC_STATUS_HPP

#include <cstdint>

namespace ztick {

/**
 * @brief Availability status of a reported metric value (REQ-MET-01).
 * @note An unavailable or not-applicable value is still reported, never omitted.
 */
enum class ValueStatus : std::uint8_t {
  /** @brief Directly observed by instrumentation or monitoring. */
  measured = 0,
  /** @brief Derived or approximated. */
  estimated = 1,
  /** @brief The platform or integration could not provide it. */
  unavailable = 2,
  /** @brief Undefined for this benchmark type or mode. */
  not_applicable = 3,
};

} // namespace ztick

#endif // ZTICK_METRIC_STATUS_HPP
