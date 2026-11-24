// SPDX-License-Identifier: MIT
/**
 * @file config_document.hpp
 * @brief Portable JSON config document reader, writer and version checks.
 *
 * RESULTS_FORMAT §6 owns the config document schema; BR §13 requires the JSON
 * document to be portable and versioned. This module reads the document into a
 * @ref ztick::ConfigValue tree, validates its top-level shape and version
 * tokens, and writes it back without changing field order. Semantic validation
 * of the remaining sections belongs to the component that consumes each
 * section. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_CONFIG_DOCUMENT_HPP
#define ZTICK_CONFIG_DOCUMENT_HPP

#include "ztick/config_value.hpp"
#include "ztick/json.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/** @brief Only config schema version supported by this build. */
inline constexpr std::string_view kConfigSchemaVersion = "1.0.0";

/**
 * @brief A three-part numeric version token.
 * @note Leading zeroes are accepted while parsing; comparison is numeric.
 */
struct SemanticVersion {
  /** @brief Incompatible-change component. */
  int major{0};
  /** @brief Backward-compatible feature component. */
  int minor{0};
  /** @brief Patch component. */
  int patch{0};

  /** @brief Compare all three components. */
  friend bool operator==(const SemanticVersion &lhs, const SemanticVersion &rhs) noexcept = default;
};

/**
 * @brief Parse a `major.minor.patch` version token.
 * @param text Text to parse; no surrounding whitespace is allowed.
 * @return Parsed components, or `std::nullopt` when the token is malformed.
 */
std::optional<SemanticVersion> parse_semantic_version(std::string_view text) noexcept;

/**
 * @brief Check an SDK version token against this host's C ABI version.
 * @param version Parsed version declared by a config document.
 * @return True when this host can execute an integration declaring @p version.
 * @note The major component must match exactly; a newer host accepts older minor
 *       versions and any patch version (SDK_SPECIFICATION §2).
 */
bool sdk_version_supported(const SemanticVersion &version) noexcept;

/**
 * @brief Outcome of reading and validating one config document.
 * @note @ref document is null whenever @ref ok is false; no partially valid
 *       config is ever returned.
 */
struct ConfigParseResult {
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
 * @brief Read a portable config document from JSON text.
 * @param text Complete config JSON text.
 * @return Parsed document on success, otherwise an error with its offset.
 */
ConfigParseResult parse_config_document(std::string_view text);

/**
 * @brief Validate the top-level shape and version tokens of a config document.
 * @param document Config-shaped document to check.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return True when the document satisfies the v1.0 config schema.
 * @note Version tokens, section types and integration identity are checked
 *       here. Field-level semantics belong to the consuming component.
 */
bool validate_config_document(const ConfigValue &document, std::string *error = nullptr);

/**
 * @brief Outcome of validating and writing one config document.
 */
struct ConfigWriteResult {
  /** @brief Compact JSON text; empty when @ref ok is false. */
  std::string text;
  /** @brief True when validation and encoding both succeeded. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief Length of @ref text on success, otherwise the failure offset. */
  std::size_t offset{0};

  /**
   * @brief Report whether the document was validated and written.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Validate and write a portable config document as compact JSON.
 * @param document Config-shaped document to encode.
 * @return JSON text on success, otherwise an error explaining the failure.
 */
ConfigWriteResult write_config_document(const ConfigValue &document);

} // namespace ztick

#endif // ZTICK_CONFIG_DOCUMENT_HPP
