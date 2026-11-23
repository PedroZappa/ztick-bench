// SPDX-License-Identifier: MIT
/**
 * @file system_info_metadata.hpp
 * @brief Convert the platform system-inventory into results metadata fields.
 *
 * PLATFORM_MONITORING §2 owns the inventory fields and §6 defines the four
 * availability states. RESULTS_FORMAT §4.2 stores those fields in a result's
 * `machine` section. This adapter is the only bridge between the two modules:
 * it keeps OS-specific collection in @ref ztick::platform and result encoding in
 * the results module. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_SYSTEM_INFO_METADATA_HPP
#define ZTICK_SYSTEM_INFO_METADATA_HPP

#include <ztick/metadata.hpp>
#include <ztick/platform.hpp>

#include <vector>

/** @brief Portable platform abstraction types and interfaces. */
namespace ztick::platform {

/**
 * @brief Convert a collected inventory into availability-aware metadata fields.
 * @param info System inventory collected by a @ref SystemInfoProvider.
 * @return One field per inventory item in stable declaration order.
 * @note Each field keeps its platform availability state and reason, so a
 *       missing capability survives the conversion instead of being omitted
 *       (PLATFORM_MONITORING §6, RESULTS_FORMAT §4.2).
 */
std::vector<MetadataField> system_info_metadata(const SystemInfo &info);

} // namespace ztick::platform

#endif // ZTICK_SYSTEM_INFO_METADATA_HPP
