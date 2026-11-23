// SPDX-License-Identifier: MIT
/**
 * @file config_value.hpp
 * @brief Minimal in-memory representation of config-shaped data for core models.
 *
 * The portable config document is owned by RESULTS_FORMAT §6. This value tree
 * mirrors that document's object/array/scalar shape so workload and profile
 * models, the JSON reader and the config schema checker can exchange data
 * without a runtime JSON dependency.
 *
 * Objects preserve insertion order, which makes rendered data deterministic and
 * allows equality-based round-trip tests. This header is private to ztick-bench
 * and is not installed.
 */

#ifndef ZTICK_CONFIG_VALUE_HPP
#define ZTICK_CONFIG_VALUE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/** @brief Discriminant for the scalar and container forms of @ref ztick::ConfigValue. */
enum class ConfigValueType : std::uint8_t {
  /** @brief Absence of a value; never used to silently omit a config field. */
  null = 0,
  /** @brief Boolean scalar. */
  boolean = 1,
  /** @brief Signed integer scalar. */
  integer = 2,
  /** @brief Floating-point scalar. */
  real = 3,
  /** @brief UTF-8 text scalar. */
  string = 4,
  /** @brief Ordered collection of named members. */
  object = 5,
  /** @brief Ordered sequence of values. */
  array = 6,
};

/**
 * @brief Stable token for a @ref ConfigValueType.
 * @param type Discriminant to name.
 * @return Lowercase token such as `object` or `integer`.
 */
std::string_view config_value_type_name(ConfigValueType type) noexcept;

/**
 * @brief A JSON-shaped value used to exchange config data with core models.
 * @note Use the static factories to construct values; the private storage
 *       constructor avoids ambiguous conversions between integer, real and
 *       boolean scalars. All operations are non-throwing except string copies.
 */
class ConfigValue {
public:
  /** @brief One named member of an object value. */
  using Member = std::pair<std::string, ConfigValue>;
  /** @brief Ordered object members; keys are unique after @ref set. */
  using Object = std::vector<Member>;
  /** @brief Ordered array elements. */
  using Array = std::vector<ConfigValue>;

  /**
   * @brief Create a null value.
   * @return A value whose @ref type is `null`.
   */
  static ConfigValue null() noexcept;

  /**
   * @brief Create a boolean value.
   * @param value Boolean to store.
   * @return A value whose @ref type is `boolean`.
   */
  static ConfigValue boolean(bool value) noexcept;

  /**
   * @brief Create an integer value.
   * @param value Integer to store.
   * @return A value whose @ref type is `integer`.
   */
  static ConfigValue integer(std::int64_t value) noexcept;

  /**
   * @brief Create a real value.
   * @param value Floating-point number to store.
   * @return A value whose @ref type is `real`.
   */
  static ConfigValue real(double value) noexcept;

  /**
   * @brief Create a string value.
   * @param value Text to store; moved into the value.
   * @return A value whose @ref type is `string`.
   */
  static ConfigValue string(std::string value) noexcept;

  /**
   * @brief Create an empty object value.
   * @return A value whose @ref type is `object`.
   */
  static ConfigValue object() noexcept;

  /**
   * @brief Create an empty array value.
   * @return A value whose @ref type is `array`.
   */
  static ConfigValue array() noexcept;

  /**
   * @brief Create an array value from existing elements.
   * @param elements Elements to move into the value.
   * @return A value whose @ref type is `array` containing @p elements.
   */
  static ConfigValue array(Array elements) noexcept;

  /**
   * @brief Read the stored discriminant.
   * @return The active @ref ConfigValueType.
   */
  [[nodiscard]] ConfigValueType type() const noexcept;

  /**
   * @brief Read a boolean scalar.
   * @return The stored boolean, or `std::nullopt` for any other type.
   */
  [[nodiscard]] std::optional<bool> as_boolean() const noexcept;

  /**
   * @brief Read an integer scalar.
   * @return The stored integer, or `std::nullopt` for any other type.
   */
  [[nodiscard]] std::optional<std::int64_t> as_integer() const noexcept;

  /**
   * @brief Read a real scalar.
   * @return The stored real, or `std::nullopt` for any other type.
   */
  [[nodiscard]] std::optional<double> as_real() const noexcept;

  /**
   * @brief Read a string scalar.
   * @return A copy of the stored text, or `std::nullopt` for any other type.
   */
  [[nodiscard]] std::optional<std::string> as_string() const;

  /**
   * @brief Read the object member list.
   * @return Pointer to the members, or `nullptr` when this is not an object.
   */
  [[nodiscard]] const Object *as_object() const noexcept;

  /**
   * @brief Read the array elements.
   * @return Pointer to the elements, or `nullptr` when this is not an array.
   */
  [[nodiscard]] const Array *as_array() const noexcept;

  /**
   * @brief Find an object member by key.
   * @param key Member name to look up.
   * @return Pointer to the member value, or `nullptr` when absent or not an object.
   */
  [[nodiscard]] const ConfigValue *find(std::string_view key) const noexcept;

  /**
   * @brief Insert or replace an object member.
   * @param key Member name; an existing member with this key is replaced in place.
   * @param value Value to store.
   * @return True when the member was written, false when this is not an object.
   */
  bool set(std::string key, ConfigValue value);

  /**
   * @brief Append an element to an array value.
   * @param value Element to store.
   * @return True when the element was appended, false when this is not an array.
   */
  bool push_back(ConfigValue value);

  /**
   * @brief Compare two values, including nested objects and arrays.
   * @param lhs Left-hand value.
   * @param rhs Right-hand value.
   * @return True when both values have the same type and equal contents.
   */
  friend bool operator==(const ConfigValue &lhs, const ConfigValue &rhs);

  /**
   * @brief Compare two values for inequality.
   * @param lhs Left-hand value.
   * @param rhs Right-hand value.
   * @return True when @ref operator== would return false.
   */
  friend bool operator!=(const ConfigValue &lhs, const ConfigValue &rhs);

private:
  /** @brief Storage backing every supported config value form. */
  using Storage =
      std::variant<std::monostate, bool, std::int64_t, double, std::string, Object, Array>;

  /**
   * @brief Construct from the internal storage variant.
   * @param storage Contents to move into the new value.
   */
  explicit ConfigValue(Storage storage) noexcept;

  /** @brief Active scalar or container contents. */
  Storage storage_;
};

} // namespace ztick

#endif // ZTICK_CONFIG_VALUE_HPP
