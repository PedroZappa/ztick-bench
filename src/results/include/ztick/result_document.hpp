// SPDX-License-Identifier: MIT
/**
 * @file result_document.hpp
 * @brief Versioned result document schema, status derivation and atomic writer.
 *
 * RESULTS_FORMAT §3, §5 and §9 own the result document shape. BR §14 requires
 * incomplete runs to be marked explicitly and result files to survive component
 * failures uncorrupted. This module validates that shape, derives the session
 * status from the recorded run states, and writes the encoded document through
 * @ref ztick::write_file_atomic so a partial write never reaches the target path.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_RESULT_DOCUMENT_HPP
#define ZTICK_RESULT_DOCUMENT_HPP

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

/** @brief Only result schema version supported by this build. */
inline constexpr std::string_view kResultSchemaVersion = "1.0.0";

/**
 * @brief Lifecycle state of one recorded run.
 * @note `failed` is retained for diagnostics; it also makes the session
 *       incomplete (RESULTS_FORMAT §9).
 */
enum class RunStatus : std::uint8_t {
  /** @brief The run completed all configured test points. */
  complete = 0,
  /** @brief The run stopped before all configured test points finished. */
  incomplete = 1,
  /** @brief The run failed while a workload was attempted. */
  failed = 2,
};

/** @brief Lifecycle state of a whole benchmark session. */
enum class SessionStatus : std::uint8_t {
  /** @brief Every recorded run completed successfully. */
  complete = 0,
  /** @brief At least one run was interrupted or failed. */
  incomplete = 1,
};

/**
 * @brief Convert a run status to its JSON token.
 * @param status Status to encode.
 * @return Stable lowercase token used in result documents.
 */
std::string_view run_status_token(RunStatus status) noexcept;

/**
 * @brief Parse a run status JSON token.
 * @param token Token to parse; no surrounding whitespace is allowed.
 * @return Parsed status, or `std::nullopt` for an unknown token.
 */
std::optional<RunStatus> parse_run_status(std::string_view token) noexcept;

/**
 * @brief Convert a session status to its JSON token.
 * @param status Status to encode.
 * @return `complete` or `incomplete`.
 */
std::string_view session_status_token(SessionStatus status) noexcept;

/**
 * @brief Parse a session status JSON token.
 * @param token Token to parse; no surrounding whitespace is allowed.
 * @return Parsed status, or `std::nullopt` for an unknown token.
 */
std::optional<SessionStatus> parse_session_status(std::string_view token) noexcept;

/**
 * @brief Validate the top-level shape and version tokens of a result document.
 * @param document Result-shaped document to check.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the document satisfies the v1.0 result schema.
 * @note This function checks shape, versions, session identity and the config
 *       snapshot. It does not recompute @ref SessionStatus; use
 *       @ref finalize_result_document for that.
 */
bool validate_result_document(const ConfigValue &document, std::string *error = nullptr);

/**
 * @brief Mark incomplete runs and derive the session status.
 * @param document Mutable result document.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the document was well formed and finalized.
 * @note A run that records failures while claiming `complete` is demoted to
 *       `incomplete`. The session becomes `incomplete` when any run is
 *       incomplete or failed, or when session-level failures are present.
 *       On failure @p document is left unchanged.
 */
bool finalize_result_document(ConfigValue &document, std::string *error = nullptr);

/** @brief Outcome of reading and validating one result document. */
struct ResultParseResult {
  /** @brief Parsed document; meaningful only when @ref ok is true. */
  ConfigValue document{ConfigValue::null()};
  /** @brief True when JSON parsing and schema validation both succeeded. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief JSON character offset, or zero when schema validation failed. */
  std::size_t offset{0};

  /**
   * @brief Report whether the document was read and validated.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Read a result document from JSON text.
 * @param text Complete result JSON text.
 * @return Parsed document on success, otherwise an error with its offset.
 * @note A stored status is validated but not recomputed here; use
 *       @ref finalize_result_document when editing a document.
 */
ResultParseResult parse_result_document(std::string_view text);

/**
 * @brief Read a result document from a file.
 * @param path Result file to read.
 * @return Parsed document on success, otherwise a file or schema error.
 */
ResultParseResult read_result_document(const std::filesystem::path &path);

/** @brief Outcome of finalizing and atomically writing one result document. */
struct ResultWriteResult {
  /** @brief True when validation, encoding and replacement all succeeded. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief Length of the encoded JSON on success, otherwise the failure offset. */
  std::size_t offset{0};

  /**
   * @brief Report whether the document was written successfully.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Validate, finalize and atomically write a result document.
 * @param target Destination path; its parent directory must already exist.
 * @param document Result-shaped document to write.
 * @return Success, or an error identifying validation, encoding or file failure.
 * @note The session status is derived before encoding, so a stale `complete`
 *       token cannot survive alongside an incomplete or failed run. An existing
 *       target is left untouched when any step fails.
 */
ResultWriteResult write_result_document(const std::filesystem::path &target,
                                        const ConfigValue &document);

} // namespace ztick

#endif // ZTICK_RESULT_DOCUMENT_HPP
