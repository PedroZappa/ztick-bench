// SPDX-License-Identifier: MIT
/**
 * @file profile.hpp
 * @brief Named benchmark profile model and its config-shaped representation.
 *
 * Profiles define more than duration: warm-up, repetitions, coverage,
 * measurement detail, reliability checks and validation level are part of the
 * model (REQ-BENCH-05, BR §3.2). The serialized form spans the `profile`,
 * `execution`, `measurement` and `validation` sections of the config document
 * owned by RESULTS_FORMAT §6. This header is private to ztick-bench and is not
 * installed.
 */

#ifndef ZTICK_PROFILE_HPP
#define ZTICK_PROFILE_HPP

#include "ztick/config_value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztick {

/** @brief How the workload run length is selected. */
enum class DurationMode : std::uint8_t {
  /** @brief Fixed iteration count takes precedence when both modes are set. */
  iterations = 0,
  /** @brief Target duration converted to frames with `ceil(duration * fs)`. */
  duration = 1,
};

/**
 * @brief Parse a config token into a @ref DurationMode.
 * @param token Token from the config document, such as `iterations`.
 * @return The matching mode, or `std::nullopt` for an unknown token.
 */
std::optional<DurationMode> parse_duration_mode(std::string_view token) noexcept;

/**
 * @brief Render a @ref DurationMode as its config token.
 * @param mode Mode to render.
 * @return Stable token used by the config document.
 */
std::string_view duration_mode_name(DurationMode mode) noexcept;

/** @brief Granularity at which timing samples are retained. */
enum class MeasurementDetail : std::uint8_t {
  /** @brief One aggregate timing per measured block group. */
  aggregate = 0,
  /** @brief One timing sample per processed block. */
  per_block = 1,
};

/**
 * @brief Parse a config token into a @ref MeasurementDetail.
 * @param token Token from the config document, such as `aggregate`.
 * @return The matching detail, or `std::nullopt` for an unknown token.
 */
std::optional<MeasurementDetail> parse_measurement_detail(std::string_view token) noexcept;

/**
 * @brief Render a @ref MeasurementDetail as its config token.
 * @param detail Detail to render.
 * @return Stable token used by the config document.
 */
std::string_view measurement_detail_name(MeasurementDetail detail) noexcept;

/** @brief Independently selectable correctness validation levels. */
enum class ValidationLevel : std::uint8_t {
  /** @brief NaN, infinity, invalid output and obvious failures. */
  basic = 0,
  /** @brief Basic checks plus reference-output comparisons where available. */
  standard = 1,
  /** @brief Standard checks plus adapter-specific and extended input coverage. */
  strict = 2,
};

/**
 * @brief Parse a config token into a @ref ValidationLevel.
 * @param token Token from the config document, such as `Basic`.
 * @return The matching level, or `std::nullopt` for an unknown token.
 */
std::optional<ValidationLevel> parse_validation_level(std::string_view token) noexcept;

/**
 * @brief Render a @ref ValidationLevel as its config token.
 * @param level Level to render.
 * @return Stable token used by the config document.
 */
std::string_view validation_level_name(ValidationLevel level) noexcept;

/**
 * @brief Named, reusable set of benchmark settings.
 * @note Every field is round-tripped; optional lists default to empty rather
 *       than being omitted from the config-shaped representation.
 */
struct Profile {
  /** @brief User-facing profile name; `Custom` for user-defined profiles. */
  std::string name{"Custom"};
  /** @brief Fixed-iteration or target-duration execution. */
  DurationMode duration_mode{DurationMode::iterations};
  /** @brief Iteration count used in fixed-iteration mode. */
  std::uint64_t iterations{1000};
  /** @brief Target duration in seconds used in duration mode. */
  double duration_seconds{0.0};
  /** @brief Number of independent repeated measurements. */
  std::uint32_t repetitions{1};
  /** @brief Warm-up iterations excluded from primary timing. */
  std::uint32_t warmup_iterations{0};
  /** @brief Named dimension coverage entries for this profile. */
  std::vector<std::string> coverage;
  /** @brief Granularity of retained timing samples. */
  MeasurementDetail detail{MeasurementDetail::aggregate};
  /** @brief Whether input generation and setup are included in timing. */
  bool end_to_end{false};
  /** @brief Selected timing levels, such as `per_block` and `whole_run`. */
  std::vector<std::string> timing_levels;
  /** @brief Correctness validation level. */
  ValidationLevel validation_level{ValidationLevel::basic};
  /** @brief Named reliability checks enabled for this profile. */
  std::vector<std::string> reliability_checks;
};

/**
 * @brief Render a profile as the profile-owned sections of a config document.
 * @param profile Profile to render.
 * @return Object with `profile`, `execution`, `measurement` and `validation`
 *         sections matching RESULTS_FORMAT §6.
 */
ConfigValue profile_to_config(const Profile &profile);

/**
 * @brief Build a profile from the profile-owned sections of a config document.
 * @param data Config-shaped object holding profile settings.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return The parsed profile, or `std::nullopt` when the data is malformed.
 * @note A malformed section never yields a partially populated profile.
 */
std::optional<Profile> profile_from_config(const ConfigValue &data, std::string *error = nullptr);

} // namespace ztick

#endif // ZTICK_PROFILE_HPP
