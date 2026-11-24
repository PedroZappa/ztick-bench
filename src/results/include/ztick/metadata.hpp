// SPDX-License-Identifier: MIT
/**
 * @file metadata.hpp
 * @brief Availability-aware build, machine and workload metadata for results.
 *
 * RESULTS_FORMAT §4 owns the attached metadata sections and §7 owns portability
 * checks. BR §11.1 requires build facts (commit, compiler, flags, configuration)
 * to be recorded where discoverable with missing items reported as unavailable.
 * BR §11.2/§11.3 require comparability warnings when runs from different builds
 * or machines are compared. This module builds the `build`, `machine` and
 * `environment` sections of a result document, attaches them and compares two
 * documents so mismatches become explicit warning records rather than silent
 * differences. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_METADATA_HPP
#define ZTICK_METADATA_HPP

#include "ztick/config_value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/**
 * @brief Availability of one attached metadata field.
 * @note Mirrors the platform states defined by PLATFORM_MONITORING §6 without
 *       making the results layer depend on the platform abstraction.
 */
enum class MetadataStatus : std::uint8_t {
  /** @brief Supported and successfully read. */
  value = 0,
  /** @brief Supported but the value could not be obtained. */
  unavailable = 1,
  /** @brief Not implemented for this OS, backend or mode. */
  unsupported = 2,
  /** @brief Meaningless for the selected benchmark mode. */
  not_applicable = 3,
};

/**
 * @brief Convert a metadata status to its stable JSON token.
 * @param status Status to encode.
 * @return One of `value`, `unavailable`, `unsupported`, `not_applicable`.
 */
std::string_view metadata_status_token(MetadataStatus status) noexcept;

/**
 * @brief Parse a metadata status JSON token.
 * @param token Token to parse; no surrounding whitespace is allowed.
 * @return Parsed status, or `std::nullopt` for an unknown token.
 */
std::optional<MetadataStatus> parse_metadata_status(std::string_view token) noexcept;

/**
 * @brief One availability-aware metadata field.
 * @note A field is always present in the serialized object. A non-value status
 *       carries its explanation in @ref reason instead of being omitted.
 */
struct MetadataField {
  /** @brief Stable field name used as the object key. */
  std::string name;
  /** @brief Availability of @ref value. */
  MetadataStatus status{MetadataStatus::unavailable};
  /** @brief Recorded value; empty unless @ref status is `value`. */
  std::string value;
  /** @brief Explanation; empty unless @ref status is a non-value state. */
  std::string reason;
};

/**
 * @brief Build a successfully read metadata field.
 * @param name Stable field name.
 * @param value Recorded value text.
 * @return Field with `value` status and an empty reason.
 */
MetadataField metadata_value(std::string name, std::string value);

/**
 * @brief Build a metadata field that could not be read.
 * @param name Stable field name.
 * @param reason Human-readable explanation.
 * @return Field with `unavailable` status and an empty value.
 */
MetadataField metadata_unavailable(std::string name, std::string reason);

/**
 * @brief Build a metadata field for a capability not implemented here.
 * @param name Stable field name.
 * @param reason Human-readable explanation.
 * @return Field with `unsupported` status and an empty value.
 */
MetadataField metadata_unsupported(std::string name, std::string reason);

/**
 * @brief Build a metadata field that does not apply to the selected mode.
 * @param name Stable field name.
 * @param reason Human-readable explanation.
 * @return Field with `not_applicable` status and an empty value.
 */
MetadataField metadata_not_applicable(std::string name, std::string reason);

/**
 * @brief Build one metadata object from availability-aware fields.
 * @param fields Fields to serialize; names must be unique within @p fields.
 * @return Object mapping each field name to `{status, value, reason}`.
 * @note All three record members are always present, so a non-value field is
 *       reported rather than omitted (RESULTS_FORMAT §3.1, §4).
 */
ConfigValue make_metadata_object(const std::vector<MetadataField> &fields);

/**
 * @brief Build facts captured when this binary was compiled.
 * @note Each member is a compile-time string supplied by CMake. An undiscoverable
 *       fact is the literal `unavailable`; no member is left empty by this build.
 */
struct BuildInfo {
  /** @brief Git commit hash, or `unavailable` when Git or the commit is absent. */
  std::string git_commit;
  /** @brief C++ compiler identifier, or `unavailable` when unknown. */
  std::string compiler;
  /** @brief C++ compiler version, or `unavailable` when unknown. */
  std::string compiler_version;
  /** @brief Optimization and warning flags used for the build. */
  std::string optimization_flags;
  /** @brief Build configuration such as `Release` or `Debug`. */
  std::string build_configuration;
  /** @brief Tool version and maturity suffix, such as `0.3.0-dev`. */
  std::string project_version;
};

/**
 * @brief Read the build facts captured for this binary.
 * @return Build metadata values from the compile-time definitions.
 */
BuildInfo current_build_info();

/**
 * @brief Build the `build` metadata section from explicit facts.
 * @param info Build facts to serialize.
 * @return Metadata object with one field per @ref BuildInfo member.
 * @note An empty string in @p info is rendered as `unavailable` with a reason,
 *       so a caller constructing facts by hand cannot silently omit a field.
 */
ConfigValue build_metadata(const BuildInfo &info);

/**
 * @brief Build the `build` metadata section for this binary.
 * @return Metadata object produced from @ref current_build_info.
 */
ConfigValue build_metadata();

/**
 * @brief Project workload identity and dimensions from a config snapshot.
 * @param config_snapshot Full portable config document from a result.
 * @return Metadata object describing integration, workload and dimensions.
 * @note Missing or mistyped fields become `unavailable` fields with reasons.
 */
ConfigValue workload_metadata(const ConfigValue &config_snapshot);

/**
 * @brief Project execution and parallelism settings from a config snapshot.
 * @param config_snapshot Full portable config document from a result.
 * @return Metadata object describing isolation, workers, affinity and detail.
 * @note Missing or mistyped fields become `unavailable` fields with reasons.
 */
ConfigValue environment_metadata(const ConfigValue &config_snapshot);

/**
 * @brief Attach the three metadata sections to a result document.
 * @param document Result document to update; it is unchanged on failure.
 * @param build Build metadata object from @ref build_metadata.
 * @param machine Machine metadata object from availability-aware fields.
 * @param environment Environment metadata object.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when all sections are objects and @p document was updated.
 */
bool attach_metadata(ConfigValue &document, const ConfigValue &build, const ConfigValue &machine,
                     const ConfigValue &environment, std::string *error = nullptr);

/**
 * @brief Result of comparing two documents' attached metadata.
 * @note @ref warnings is an array of warning records suitable for a comparison
 *       record's `comparability_warnings` field (RESULTS_FORMAT §10).
 */
struct MetadataComparison {
  /** @brief True when no comparability differences were found. */
  bool comparable{true};
  /** @brief Number of fields that produced a warning. */
  std::size_t difference_count{0};
  /** @brief Warning records in deterministic field order. */
  ConfigValue warnings{ConfigValue::array()};
};

/**
 * @brief Compare the metadata sections of two result documents.
 * @param left Baseline result document.
 * @param right Candidate result document.
 * @return Comparability status and warning records for every differing field.
 * @note Two equal value fields agree. Differing values, differing statuses, or a
 *       value on one side against a non-value status on the other produce a
 *       warning; identical non-value statuses do not. Fields absent from both
 *       sections are ignored because an attachment step owns their presence. A
 *       non-object document is reported as incomparable with one warning.
 */
MetadataComparison compare_metadata(const ConfigValue &left, const ConfigValue &right);

} // namespace ztick

#endif // ZTICK_METADATA_HPP
