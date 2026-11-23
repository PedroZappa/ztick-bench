// SPDX-License-Identifier: MIT
/**
 * @file json.hpp
 * @brief Dependency-free JSON reader and writer for the config-shaped value tree.
 *
 * The portable config and result documents use JSON as their canonical format
 * (REQ-RES-01, BR §12.3). No runtime third-party JSON library is mandated for
 * v1.0, so this module reads and writes the subset of JSON used by those
 * documents directly into @ref ztick::ConfigValue. Objects keep member order,
 * which makes serialization deterministic and supports equality-based
 * round-trip checks. This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_JSON_HPP
#define ZTICK_JSON_HPP

#include "ztick/config_value.hpp"

#include <cstddef>
#include <string>
#include <string_view>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/** @brief Outcome of reading one JSON document. */
struct JsonReadResult {
  /** @brief Parsed value; meaningful only when @ref ok is true. */
  ConfigValue value{ConfigValue::null()};
  /** @brief True when the entire input was consumed without an error. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief Zero-based offset where parsing stopped or failed. */
  std::size_t offset{0};

  /**
   * @brief Report whether the document was read successfully.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Read one JSON document into a config-shaped value.
 * @param text Complete UTF-8 JSON text; leading and trailing whitespace is allowed.
 * @return Parsed value on success, otherwise an error with its character offset.
 * @note Duplicate object keys keep the last value but retain the first key's
 *       position. Numbers too large for a signed 64-bit integer are kept as reals.
 */
JsonReadResult json_read(std::string_view text);

/** @brief Outcome of writing one config-shaped value as JSON. */
struct JsonWriteResult {
  /** @brief Compact JSON text; empty when @ref ok is false. */
  std::string text;
  /** @brief True when the value was encoded successfully. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief Length of @ref text on success, otherwise the failure offset. */
  std::size_t offset{0};

  /**
   * @brief Report whether the value was written successfully.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Write a config-shaped value as compact, deterministic JSON.
 * @param value Value to encode.
 * @return JSON text on success, otherwise an error explaining the invalid value.
 * @note Object member order and array element order are preserved. Non-finite
 *       reals are rejected because JSON has no representation for them.
 */
JsonWriteResult json_write(const ConfigValue &value);

} // namespace ztick

#endif // ZTICK_JSON_HPP
