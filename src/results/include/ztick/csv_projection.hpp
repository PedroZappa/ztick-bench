// SPDX-License-Identifier: MIT
/**
 * @file csv_projection.hpp
 * @brief Convenience CSV projection of a stored result document.
 *
 * RESULTS_FORMAT §8 defines CSV as a non-canonical convenience view of the JSON
 * result document. Every projected row repeats the JSON identifiers, metric
 * value, unit, status and source so those fields are directly comparable with
 * the canonical document. Raw per-block series and complex metadata stay in
 * JSON and are named in the `omitted_fields` column rather than dropped
 * silently. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_CSV_PROJECTION_HPP
#define ZTICK_CSV_PROJECTION_HPP

#include "ztick/atomic_writer.hpp"
#include "ztick/config_value.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/** @brief Stable CSV header; column order is part of the v1.0 projection. */
inline constexpr std::string_view kCsvHeader =
    "session_id,run_id,test_point,metric_id,metric_name,value,unit,status,source,method,reason,"
    "warning_count,failure_count,run_status,omitted_fields";

/** @brief Data retained in JSON and named by the `omitted_fields` column. */
inline constexpr std::string_view kCsvOmittedFields = "raw_per_block_series;complex_metadata";

/** @brief Row granularity of one CSV export. */
enum class CsvRowMode : std::uint8_t {
  /** @brief One row per metric of a test point, using its aggregate value. */
  per_test_point = 0,
  /** @brief One row per metric sample, falling back to the aggregate value. */
  per_metric_sample = 1,
};

/**
 * @brief Convert a row mode to its stable CLI token.
 * @param mode Row mode to encode.
 * @return Lowercase token suitable for diagnostics and command-line options.
 */
std::string_view csv_row_mode_token(CsvRowMode mode) noexcept;

/**
 * @brief Parse a row mode token.
 * @param token Token to parse; no surrounding whitespace is allowed.
 * @return Parsed mode, or `std::nullopt` for an unknown token.
 */
std::optional<CsvRowMode> parse_csv_row_mode(std::string_view token) noexcept;

/** @brief Options controlling one CSV projection. */
struct CsvExportOptions {
  /** @brief Selected row granularity. */
  CsvRowMode row_mode{CsvRowMode::per_test_point};
};

/** @brief Outcome of projecting one result document to CSV. */
struct CsvProjectionResult {
  /** @brief Complete CSV text with header and trailing newline; empty on failure. */
  std::string text;
  /** @brief Number of data rows, excluding the header. */
  std::size_t row_count{0};
  /** @brief True when validation and projection both succeeded. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;

  /**
   * @brief Report whether the projection succeeded.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Project a validated result document to CSV text.
 * @param document Result document to read; it is never modified.
 * @param options Row granularity selection.
 * @return CSV text on success, otherwise an error explaining the failure.
 * @note The document is validated with `validate_result_document` first. A run
 *       without metrics still produces one row so its test point is not lost.
 */
CsvProjectionResult project_result_document(const ConfigValue &document,
                                            const CsvExportOptions &options = {});

/** @brief Outcome of atomically writing one CSV projection. */
struct CsvWriteResult {
  /** @brief True when projection and file replacement both succeeded. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief Number of data rows written, excluding the header. */
  std::size_t row_count{0};

  /**
   * @brief Report whether the CSV file was written successfully.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Project a result document and atomically write the CSV file.
 * @param target Destination path; its parent directory must already exist.
 * @param document Result document to read; it is never modified.
 * @param options Row granularity selection.
 * @return Success, or an error identifying projection or file failure.
 * @note An existing target is left untouched when projection fails.
 */
CsvWriteResult write_result_csv(const std::filesystem::path &target, const ConfigValue &document,
                                const CsvExportOptions &options = {});

} // namespace ztick

#endif // ZTICK_CSV_PROJECTION_HPP
