// SPDX-License-Identifier: MIT
/**
 * @file json.cpp
 * @brief Implementation of the dependency-free JSON reader and writer declared
 *        in @ref json.hpp.
 */

#include "ztick/json.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace ztick {

namespace {

/**
 * @brief Recursive-descent reader for one JSON document.
 * @note Errors carry a zero-based character offset so callers can report the
 *       exact location of malformed config or result text.
 */
class JsonReader {
public:
  /**
   * @brief Construct a reader over complete JSON text.
   * @param text Text to parse; the view must outlive all parsing calls.
   */
  explicit JsonReader(std::string_view text) noexcept : text_(text) {}

  /**
   * @brief Parse the complete document.
   * @return Parsed value on success, otherwise an error with its offset.
   */
  JsonReadResult read() {
    skip_whitespace();
    std::optional<ConfigValue> value = parse_value();
    if (!value.has_value()) {
      return failure();
    }
    skip_whitespace();
    if (position_ != text_.size()) {
      return fail_document("unexpected trailing content");
    }
    return JsonReadResult{std::move(*value), true, {}, position_};
  }

private:
  /**
   * @brief Advance past JSON whitespace.
   * @note JSON whitespace is space, tab, line feed and carriage return.
   */
  void skip_whitespace() noexcept {
    while (position_ < text_.size()) {
      const char c = text_[position_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        break;
      }
      ++position_;
    }
  }

  /**
   * @brief Parse one JSON value at the current position.
   * @return Parsed value, or `std::nullopt` after recording an error.
   */
  std::optional<ConfigValue> parse_value() {
    if (position_ >= text_.size()) {
      return fail_value("expected a JSON value");
    }
    switch (text_[position_]) {
    case '"':
      return parse_string();
    case '{':
      return parse_object();
    case '[':
      return parse_array();
    case 't':
      return parse_keyword("true", ConfigValue::boolean(true));
    case 'f':
      return parse_keyword("false", ConfigValue::boolean(false));
    case 'n':
      return parse_keyword("null", ConfigValue::null());
    default:
      if (text_[position_] == '-' || is_digit(text_[position_])) {
        return parse_number();
      }
      return fail_value("unexpected character");
    }
  }

  /**
   * @brief Parse a JSON object.
   * @return Object value containing every parsed member in source order.
   * @note Duplicate keys keep the last value but retain the first key's position.
   */
  std::optional<ConfigValue> parse_object() {
    ++position_; // opening brace
    ConfigValue object = ConfigValue::object();
    skip_whitespace();
    if (consume('}')) {
      return object;
    }
    while (true) {
      skip_whitespace();
      if (position_ >= text_.size() || text_[position_] != '"') {
        return fail_value("expected an object key");
      }
      std::optional<ConfigValue> key = parse_string();
      if (!key.has_value()) {
        return std::nullopt;
      }
      skip_whitespace();
      if (!consume(':')) {
        return fail_value("expected ':' after object key");
      }
      skip_whitespace();
      std::optional<ConfigValue> value = parse_value();
      if (!value.has_value()) {
        return std::nullopt;
      }
      if (!object.set(std::move(*(key->as_string())), std::move(*value))) {
        return fail_value("failed to build object member");
      }
      skip_whitespace();
      if (consume('}')) {
        return object;
      }
      if (!consume(',')) {
        return fail_value("expected ',' or '}' in object");
      }
    }
  }

  /**
   * @brief Parse a JSON array.
   * @return Array value containing every parsed element in source order.
   */
  std::optional<ConfigValue> parse_array() {
    ++position_; // opening bracket
    ConfigValue array = ConfigValue::array();
    skip_whitespace();
    if (consume(']')) {
      return array;
    }
    while (true) {
      skip_whitespace();
      std::optional<ConfigValue> element = parse_value();
      if (!element.has_value()) {
        return std::nullopt;
      }
      if (!array.push_back(std::move(*element))) {
        return fail_value("failed to append array element");
      }
      skip_whitespace();
      if (consume(']')) {
        return array;
      }
      if (!consume(',')) {
        return fail_value("expected ',' or ']' in array");
      }
    }
  }

  /**
   * @brief Parse a JSON string, including escapes and UTF-16 surrogate pairs.
   * @return String value, or `std::nullopt` after recording an error.
   */
  std::optional<ConfigValue> parse_string() {
    ++position_; // opening quote
    std::string result;
    while (position_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[position_++]);
      if (c == '"') {
        return ConfigValue::string(std::move(result));
      }
      if (c < 0x20U) {
        return fail_value("unescaped control character in string");
      }
      if (c != '\\') {
        result.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ >= text_.size()) {
        return fail_value("dangling escape in string");
      }
      const char escaped = text_[position_++];
      switch (escaped) {
      case '"':
        result.push_back('"');
        break;
      case '\\':
        result.push_back('\\');
        break;
      case '/':
        result.push_back('/');
        break;
      case 'b':
        result.push_back('\b');
        break;
      case 'f':
        result.push_back('\f');
        break;
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 't':
        result.push_back('\t');
        break;
      case 'u': {
        std::uint32_t code = 0;
        if (!read_hex_quad(code)) {
          return fail_value("invalid Unicode escape");
        }
        if (code >= 0xD800U && code <= 0xDBFFU) {
          if (position_ + 1 >= text_.size() || text_[position_] != '\\' ||
              text_[position_ + 1] != 'u') {
            return fail_value("missing low surrogate");
          }
          position_ += 2;
          std::uint32_t low = 0;
          if (!read_hex_quad(low)) {
            return fail_value("invalid low surrogate");
          }
          if (low < 0xDC00U || low > 0xDFFFU) {
            return fail_value("invalid low surrogate");
          }
          code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
        } else if (code >= 0xDC00U && code <= 0xDFFFU) {
          return fail_value("unexpected low surrogate");
        }
        append_utf8(code, result);
        break;
      }
      default:
        return fail_value("unknown string escape");
      }
    }
    return fail_value("unterminated string");
  }

  /**
   * @brief Read four hexadecimal digits for a `\\uXXXX` escape.
   * @param[out] code Receives the decoded code unit.
   * @return True when four digits were consumed, false otherwise.
   */
  bool read_hex_quad(std::uint32_t &code) noexcept {
    code = 0;
    for (int i = 0; i < 4; ++i) {
      if (position_ >= text_.size()) {
        return false;
      }
      const char c = text_[position_++];
      code <<= 4U;
      if (c >= '0' && c <= '9') {
        code |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        code |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        code |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Append one Unicode code point as UTF-8.
   * @param code Code point to encode.
   * @param[out] out String receiving the encoded bytes.
   */
  static void append_utf8(std::uint32_t code, std::string &out) {
    if (code <= 0x7FU) {
      out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FFU) {
      out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code <= 0xFFFFU) {
      out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
  }

  /**
   * @brief Parse a JSON number as an integer or real.
   * @return Integer for exact signed 64-bit values, otherwise a real value.
   * @note Integer literals outside the signed 64-bit range are preserved as reals.
   */
  std::optional<ConfigValue> parse_number() {
    const std::size_t start = position_;
    if (consume('-') && position_ >= text_.size()) {
      return fail_value("incomplete number");
    }
    if (consume('0')) {
      if (position_ < text_.size() && is_digit(text_[position_])) {
        return fail_value("leading zero in number");
      }
    } else if (position_ < text_.size() && text_[position_] >= '1' && text_[position_] <= '9') {
      while (position_ < text_.size() && is_digit(text_[position_])) {
        ++position_;
      }
    } else {
      return fail_value("invalid number");
    }

    bool integral = true;
    if (consume('.')) {
      integral = false;
      if (position_ >= text_.size() || !is_digit(text_[position_])) {
        return fail_value("expected digits after decimal point");
      }
      while (position_ < text_.size() && is_digit(text_[position_])) {
        ++position_;
      }
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      integral = false;
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      if (position_ >= text_.size() || !is_digit(text_[position_])) {
        return fail_value("expected digits in exponent");
      }
      while (position_ < text_.size() && is_digit(text_[position_])) {
        ++position_;
      }
    }

    const std::string_view token = text_.substr(start, position_ - start);
    if (integral) {
      std::int64_t integer = 0;
      const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), integer);
      if (error == std::errc{} && end == token.data() + token.size()) {
        return ConfigValue::integer(integer);
      }
    }
    double real = 0.0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), real,
                                              std::chars_format::general);
    if (error == std::errc{} && end == token.data() + token.size()) {
      return ConfigValue::real(real);
    }
    return fail_value("invalid number");
  }

  /**
   * @brief Parse a fixed JSON keyword.
   * @param token Keyword expected at the current position.
   * @param value Value to return when the keyword matches.
   * @return @p value on success, or `std::nullopt` after recording an error.
   */
  std::optional<ConfigValue> parse_keyword(std::string_view token, ConfigValue value) {
    if (text_.substr(position_, token.size()) != token) {
      return fail_value("invalid JSON keyword");
    }
    position_ += token.size();
    return value;
  }

  /**
   * @brief Test whether a character is an ASCII digit.
   * @param c Character to test.
   * @return True for `0` through `9`.
   */
  static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

  /**
   * @brief Consume one expected character.
   * @param expected Character to consume.
   * @return True when the character matched and was consumed.
   */
  bool consume(char expected) noexcept {
    if (position_ >= text_.size() || text_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  /**
   * @brief Record an error and return a failed parse result.
   * @param message Human-readable explanation.
   * @return Empty optional for value parsers.
   */
  std::optional<ConfigValue> fail_value(std::string_view message) {
    error_ = message;
    return std::nullopt;
  }

  /**
   * @brief Record an error and return a failed top-level result.
   * @param message Human-readable explanation.
   * @return Failed @ref JsonReadResult carrying @ref error_ and @ref position_.
   */
  JsonReadResult fail_document(std::string_view message) {
    error_ = message;
    return failure();
  }

  /**
   * @brief Build the current failed top-level result.
   * @return Failed @ref JsonReadResult carrying @ref error_ and @ref position_.
   */
  JsonReadResult failure() const {
    return JsonReadResult{ConfigValue::null(), false, error_, position_};
  }

  /** @brief Complete JSON text being read. */
  std::string_view text_;
  /** @brief Zero-based offset of the next character to read. */
  std::size_t position_{0};
  /** @brief Most recent failure explanation. */
  std::string error_;
};

/**
 * @brief Recursive JSON writer for the config-shaped value tree.
 * @note Output is compact and preserves object member and array element order.
 */
class JsonWriter {
public:
  /**
   * @brief Write one value as compact JSON.
   * @param value Value to encode.
   * @return JSON text on success, otherwise an error and failure offset.
   */
  JsonWriteResult write(const ConfigValue &value) {
    if (!append_value(value)) {
      return JsonWriteResult{{}, false, error_, failure_offset_};
    }
    return JsonWriteResult{std::move(output_), true, {}, output_.size()};
  }

private:
  /**
   * @brief Append one value to the output buffer.
   * @param value Value to encode.
   * @return True when the value was encoded, false after recording an error.
   */
  bool append_value(const ConfigValue &value) {
    switch (value.type()) {
    case ConfigValueType::null:
      output_ += "null";
      return true;
    case ConfigValueType::boolean:
      output_ += *(value.as_boolean()) ? "true" : "false";
      return true;
    case ConfigValueType::integer:
      output_ += std::to_string(*(value.as_integer()));
      return true;
    case ConfigValueType::real:
      return append_real(*(value.as_real()));
    case ConfigValueType::string:
      append_string(*(value.as_string()));
      return true;
    case ConfigValueType::object:
      return append_object(*(value.as_object()));
    case ConfigValueType::array:
      return append_array(*(value.as_array()));
    }
    return fail("unknown config value type");
  }

  /**
   * @brief Append an object with escaped keys and recursively encoded values.
   * @param members Ordered object members.
   * @return True when every member was encoded.
   */
  bool append_object(const ConfigValue::Object &members) {
    output_.push_back('{');
    for (std::size_t i = 0; i < members.size(); ++i) {
      if (i != 0) {
        output_.push_back(',');
      }
      append_string(members[i].first);
      output_.push_back(':');
      if (!append_value(members[i].second)) {
        return false;
      }
    }
    output_.push_back('}');
    return true;
  }

  /**
   * @brief Append an array with recursively encoded elements.
   * @param elements Ordered array elements.
   * @return True when every element was encoded.
   */
  bool append_array(const ConfigValue::Array &elements) {
    output_.push_back('[');
    for (std::size_t i = 0; i < elements.size(); ++i) {
      if (i != 0) {
        output_.push_back(',');
      }
      if (!append_value(elements[i])) {
        return false;
      }
    }
    output_.push_back(']');
    return true;
  }

  /**
   * @brief Append a finite real number with a JSON-compatible representation.
   * @param value Real value to encode.
   * @return True when the value was encoded, false for non-finite numbers.
   * @note Integral-looking doubles gain a `.0` suffix so they do not change type
   *       when read back into a @ref ConfigValue.
   */
  bool append_real(double value) {
    if (!std::isfinite(value)) {
      return fail("non-finite real cannot be encoded as JSON");
    }
    char buffer[64];
    const auto [end, error] =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    if (error != std::errc{}) {
      return fail("failed to encode real number");
    }
    const std::string_view token(buffer, static_cast<std::size_t>(end - buffer));
    output_.append(token);
    if (token.find_first_of(".eE") == std::string_view::npos) {
      output_ += ".0";
    }
    return true;
  }

  /**
   * @brief Append a string with all required JSON escapes.
   * @param text UTF-8 text to encode.
   * @note Bytes above ASCII are passed through so existing UTF-8 survives unchanged.
   */
  void append_string(std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    output_.push_back('"');
    for (const unsigned char c : text) {
      switch (c) {
      case '"':
        output_ += "\\\"";
        break;
      case '\\':
        output_ += "\\\\";
        break;
      case '\b':
        output_ += "\\b";
        break;
      case '\f':
        output_ += "\\f";
        break;
      case '\n':
        output_ += "\\n";
        break;
      case '\r':
        output_ += "\\r";
        break;
      case '\t':
        output_ += "\\t";
        break;
      default:
        if (c < 0x20U) {
          output_ += "\\u00";
          output_.push_back(kHex[(c >> 4U) & 0x0FU]);
          output_.push_back(kHex[c & 0x0FU]);
        } else {
          output_.push_back(static_cast<char>(c));
        }
        break;
      }
    }
    output_.push_back('"');
  }

  /**
   * @brief Record a writer failure at the current output length.
   * @param message Human-readable explanation.
   * @return False for all append helpers.
   */
  bool fail(std::string_view message) {
    error_ = message;
    failure_offset_ = output_.size();
    return false;
  }

  /** @brief Accumulated JSON text. */
  std::string output_;
  /** @brief Most recent failure explanation. */
  std::string error_;
  /** @brief Output length where the most recent failure occurred. */
  std::size_t failure_offset_{0};
};

} // namespace

JsonReadResult json_read(std::string_view text) { return JsonReader(text).read(); }

JsonWriteResult json_write(const ConfigValue &value) { return JsonWriter().write(value); }

} // namespace ztick
