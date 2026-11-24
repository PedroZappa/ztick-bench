// SPDX-License-Identifier: MIT
//! Minimal JSON reader for the bounded CLI envelopes returned by the front end.
//!
//! This module does not define a result or configuration schema. It preserves
//! object member order so availability-aware fields can be displayed exactly as
//! the core emitted them, and leaves schema interpretation to [`crate::data`].

use std::fmt;

/// Maximum nesting depth accepted from a front-end envelope.
const MAX_DEPTH: usize = 64;

/// One JSON value decoded from a front-end envelope.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Json {
    /// The JSON `null` value.
    Null,
    /// A JSON boolean.
    Bool(bool),
    /// A JSON number preserved as its original text.
    Number(String),
    /// A JSON string with all escapes decoded.
    String(String),
    /// A JSON array in source order.
    Array(Vec<Json>),
    /// A JSON object in source member order.
    Object(Vec<(String, Json)>),
}

/// Provide type-safe accessors for decoded JSON values.
impl Json {
    /// Return the object members when this value is an object.
    pub fn as_object(&self) -> Option<&[(String, Json)]> {
        match self {
            Self::Object(entries) => Some(entries),
            _ => None,
        }
    }

    /// Return the array items when this value is an array.
    pub fn as_array(&self) -> Option<&[Json]> {
        match self {
            Self::Array(items) => Some(items),
            _ => None,
        }
    }

    /// Return the decoded text when this value is a string.
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Self::String(value) => Some(value),
            _ => None,
        }
    }

    /// Return the signed integer value when this number has no fraction or exponent.
    pub fn as_i64(&self) -> Option<i64> {
        match self {
            Self::Number(text) => text.parse().ok(),
            _ => None,
        }
    }

    /// Return the first member with the requested name when this value is an object.
    pub fn get(&self, key: &str) -> Option<&Json> {
        self.as_object()?
            .iter()
            .find(|(name, _)| name == key)
            .map(|(_, value)| value)
    }
}

/// A syntax error in a front-end JSON envelope.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct JsonError {
    /// The byte offset where the syntax error was detected.
    offset: usize,
    /// The owned syntax error detail.
    message: String,
}

/// Construct and inspect JSON syntax errors.
impl JsonError {
    /// Construct an error at a byte offset.
    fn new(offset: usize, message: impl Into<String>) -> Self {
        Self {
            offset,
            message: message.into(),
        }
    }

    /// Return the byte offset where the error was detected.
    pub fn offset(&self) -> usize {
        self.offset
    }

    /// Return the human-readable error detail.
    pub fn message(&self) -> &str {
        &self.message
    }
}

/// Format a JSON syntax error for display.
impl fmt::Display for JsonError {
    /// Write the error or value into the supplied formatter.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "JSON error at byte {}: {}",
            self.offset, self.message
        )
    }
}

/// Mark JSON syntax errors as standard errors.
impl std::error::Error for JsonError {}

/// Decode one complete JSON document.
///
/// # Parameters
/// - `input`: Full text returned by a front-end command.
///
/// # Returns
/// The decoded value, or a syntax error with its byte offset.
pub fn parse(input: &str) -> Result<Json, JsonError> {
    let mut parser = Parser {
        bytes: input.as_bytes(),
        position: 0,
    };
    parser.skip_whitespace();
    let value = parser.parse_value(0)?;
    parser.skip_whitespace();
    if parser.position != parser.bytes.len() {
        return Err(parser.error("trailing content after JSON value"));
    }
    Ok(value)
}

/// Recursive-descent reader over UTF-8 JSON bytes.
struct Parser<'a> {
    /// The borrowed UTF-8 JSON input bytes.
    bytes: &'a [u8],
    /// The current byte offset in the input.
    position: usize,
}

/// Decode JSON values from borrowed UTF-8 bytes.
impl Parser<'_> {
    /// Build an error at the current byte position.
    fn error(&self, message: impl Into<String>) -> JsonError {
        JsonError::new(self.position, message)
    }

    /// Return the next byte without consuming it.
    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.position).copied()
    }

    /// Consume whitespace permitted by RFC 8259.
    fn skip_whitespace(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.position += 1;
        }
    }

    /// Consume one required byte or report an error.
    fn expect_byte(&mut self, expected: u8) -> Result<(), JsonError> {
        if self.peek() == Some(expected) {
            self.position += 1;
            Ok(())
        } else {
            Err(self.error(format!("expected '{}'", expected as char)))
        }
    }

    /// Consume a fixed ASCII literal or report an error.
    fn expect_literal(&mut self, literal: &[u8]) -> Result<(), JsonError> {
        if self.bytes[self.position..].starts_with(literal) {
            self.position += literal.len();
            Ok(())
        } else {
            Err(self.error(format!(
                "expected '{}'",
                std::str::from_utf8(literal).unwrap_or("literal")
            )))
        }
    }

    /// Decode any JSON value at the current position.
    fn parse_value(&mut self, depth: usize) -> Result<Json, JsonError> {
        if depth > MAX_DEPTH {
            return Err(self.error("JSON nesting is too deep"));
        }
        match self.peek() {
            Some(b'n') => {
                self.expect_literal(b"null")?;
                Ok(Json::Null)
            }
            Some(b't') => {
                self.expect_literal(b"true")?;
                Ok(Json::Bool(true))
            }
            Some(b'f') => {
                self.expect_literal(b"false")?;
                Ok(Json::Bool(false))
            }
            Some(b'"') => Ok(Json::String(self.parse_string()?)),
            Some(b'[') => self.parse_array(depth),
            Some(b'{') => self.parse_object(depth),
            Some(b'-' | b'0'..=b'9') => self.parse_number(),
            Some(_) => Err(self.error("unexpected byte in JSON value")),
            None => Err(self.error("unexpected end of JSON input")),
        }
    }

    /// Decode a quoted string, including Unicode escapes and surrogate pairs.
    fn parse_string(&mut self) -> Result<String, JsonError> {
        self.expect_byte(b'"')?;
        let mut output = String::new();
        loop {
            let byte = match self.peek() {
                Some(value) => value,
                None => return Err(self.error("unterminated JSON string")),
            };
            match byte {
                b'"' => {
                    self.position += 1;
                    return Ok(output);
                }
                b'\\' => {
                    self.position += 1;
                    let escape = match self.peek() {
                        Some(value) => value,
                        None => return Err(self.error("unterminated JSON escape")),
                    };
                    self.position += 1;
                    match escape {
                        b'"' => output.push('"'),
                        b'\\' => output.push('\\'),
                        b'/' => output.push('/'),
                        b'b' => output.push('\u{0008}'),
                        b'f' => output.push('\u{000c}'),
                        b'n' => output.push('\n'),
                        b'r' => output.push('\r'),
                        b't' => output.push('\t'),
                        b'u' => {
                            let scalar = self.parse_unicode_scalar()?;
                            let character = char::from_u32(scalar)
                                .ok_or_else(|| self.error("invalid Unicode scalar value"))?;
                            output.push(character);
                        }
                        _ => return Err(self.error("unsupported JSON escape")),
                    }
                }
                0x00..=0x1f => return Err(self.error("unescaped control byte in JSON string")),
                0x20..=0x7f => {
                    output.push(byte as char);
                    self.position += 1;
                }
                0x80..=0xf7 => {
                    let width = utf8_width(byte);
                    let end = self.position + width;
                    if end > self.bytes.len() {
                        return Err(self.error("truncated UTF-8 sequence"));
                    }
                    let text = std::str::from_utf8(&self.bytes[self.position..end])
                        .map_err(|_| self.error("invalid UTF-8 in JSON string"))?;
                    output.push_str(text);
                    self.position = end;
                }
                _ => return Err(self.error("invalid UTF-8 lead byte")),
            }
        }
    }

    /// Decode one `\uXXXX` escape, combining surrogate pairs when present.
    fn parse_unicode_scalar(&mut self) -> Result<u32, JsonError> {
        let first = self.parse_hex4()?;
        if !(0xd800..=0xdbff).contains(&first) {
            if (0xdc00..=0xdfff).contains(&first) {
                return Err(self.error("low surrogate without high surrogate"));
            }
            return Ok(first as u32);
        }
        if self.peek() != Some(b'\\') || self.bytes.get(self.position + 1) != Some(&b'u') {
            return Err(self.error("high surrogate without low surrogate"));
        }
        self.position += 2;
        let second = self.parse_hex4()?;
        if !(0xdc00..=0xdfff).contains(&second) {
            return Err(self.error("invalid low surrogate"));
        }
        Ok(0x10000 + (((first - 0xd800) as u32) << 10) + (second - 0xdc00) as u32)
    }

    /// Decode exactly four hexadecimal digits.
    fn parse_hex4(&mut self) -> Result<u16, JsonError> {
        if self.position + 4 > self.bytes.len() {
            return Err(self.error("truncated Unicode escape"));
        }
        let mut value = 0_u16;
        for _ in 0..4 {
            let digit = match self.peek() {
                Some(b'0'..=b'9') => self.bytes[self.position] - b'0',
                Some(b'a'..=b'f') => self.bytes[self.position] - b'a' + 10,
                Some(b'A'..=b'F') => self.bytes[self.position] - b'A' + 10,
                _ => return Err(self.error("invalid hexadecimal digit in Unicode escape")),
            };
            self.position += 1;
            value = value * 16 + digit as u16;
        }
        Ok(value)
    }

    /// Decode an array with ordered items.
    fn parse_array(&mut self, depth: usize) -> Result<Json, JsonError> {
        self.expect_byte(b'[')?;
        let mut items = Vec::new();
        self.skip_whitespace();
        if self.peek() == Some(b']') {
            self.position += 1;
            return Ok(Json::Array(items));
        }
        loop {
            self.skip_whitespace();
            items.push(self.parse_value(depth + 1)?);
            self.skip_whitespace();
            match self.peek() {
                Some(b',') => self.position += 1,
                Some(b']') => {
                    self.position += 1;
                    return Ok(Json::Array(items));
                }
                Some(_) => return Err(self.error("expected ',' or ']' in JSON array")),
                None => return Err(self.error("unterminated JSON array")),
            }
        }
    }

    /// Decode an object with ordered, unique member names.
    fn parse_object(&mut self, depth: usize) -> Result<Json, JsonError> {
        self.expect_byte(b'{')?;
        let mut entries: Vec<(String, Json)> = Vec::new();
        self.skip_whitespace();
        if self.peek() == Some(b'}') {
            self.position += 1;
            return Ok(Json::Object(entries));
        }
        loop {
            self.skip_whitespace();
            let key = self.parse_string()?;
            if entries.iter().any(|(name, _)| name == &key) {
                return Err(self.error(format!("duplicate object member '{key}'")));
            }
            self.skip_whitespace();
            self.expect_byte(b':')?;
            self.skip_whitespace();
            let value = self.parse_value(depth + 1)?;
            entries.push((key, value));
            self.skip_whitespace();
            match self.peek() {
                Some(b',') => self.position += 1,
                Some(b'}') => {
                    self.position += 1;
                    return Ok(Json::Object(entries));
                }
                Some(_) => return Err(self.error("expected ',' or '}' in JSON object")),
                None => return Err(self.error("unterminated JSON object")),
            }
        }
    }

    /// Decode a JSON number as its original text.
    fn parse_number(&mut self) -> Result<Json, JsonError> {
        let start = self.position;
        if self.peek() == Some(b'-') {
            self.position += 1;
        }
        match self.peek() {
            Some(b'0') => self.position += 1,
            Some(b'1'..=b'9') => {
                while matches!(self.peek(), Some(b'0'..=b'9')) {
                    self.position += 1;
                }
            }
            _ => return Err(self.error("invalid JSON number")),
        }
        if self.peek() == Some(b'.') {
            self.position += 1;
            if !matches!(self.peek(), Some(b'0'..=b'9')) {
                return Err(self.error("fraction has no digits"));
            }
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.position += 1;
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            self.position += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.position += 1;
            }
            if !matches!(self.peek(), Some(b'0'..=b'9')) {
                return Err(self.error("exponent has no digits"));
            }
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.position += 1;
            }
        }
        let text = std::str::from_utf8(&self.bytes[start..self.position])
            .map_err(|_| self.error("invalid number text"))?;
        Ok(Json::Number(text.to_string()))
    }
}

/// Return the UTF-8 sequence width indicated by a lead byte.
fn utf8_width(byte: u8) -> usize {
    match byte {
        0x00..=0x7f => 1,
        0xc0..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf7 => 4,
        _ => 1,
    }
}

/// Unit tests for the bounded JSON reader.
#[cfg(test)]
mod tests {
    use super::{Json, parse};

    /// Verify parse preserves object member order.
    #[test]
    fn parse_preserves_object_member_order() {
        let value = parse(r#"{"b":1,"a":2,"b_note":3}"#).expect("object parses");
        let entries = value.as_object().expect("value is an object");
        let names: Vec<&str> = entries.iter().map(|(name, _)| name.as_str()).collect();
        assert_eq!(names, ["b", "a", "b_note"]);
    }

    /// Verify parse decodes strings numbers and unicode.
    #[test]
    fn parse_decodes_strings_numbers_and_unicode() {
        let value = parse(r#"{"text":"a\n\u00e9\ud83d\ude00","n":-1.25e+2,"ok":true,"nil":null}"#)
            .expect("value parses");
        assert_eq!(value.get("text").and_then(Json::as_str), Some("a\né😀"));
        assert_eq!(value.get("n").and_then(Json::as_i64), None);
        assert_eq!(value.get("ok"), Some(&Json::Bool(true)));
        assert_eq!(value.get("nil"), Some(&Json::Null));
    }

    /// Verify parse accepts integer and array values.
    #[test]
    fn parse_accepts_integer_and_array_values() {
        let value = parse("[1,2,3]").expect("array parses");
        let items = value.as_array().expect("value is an array");
        assert_eq!(items.len(), 3);
        assert_eq!(items[0].as_i64(), Some(1));
    }

    /// Verify parse rejects trailing content.
    #[test]
    fn parse_rejects_trailing_content() {
        let error = parse("true false").expect_err("trailing content is invalid");
        assert!(error.message().contains("trailing content"));
    }

    /// Verify parse rejects duplicate members.
    #[test]
    fn parse_rejects_duplicate_members() {
        let error = parse(r#"{"a":1,"a":2}"#).expect_err("duplicate keys are invalid");
        assert!(error.message().contains("duplicate"));
    }

    /// Verify parse rejects malformed numbers.
    #[test]
    fn parse_rejects_malformed_numbers() {
        assert!(parse("01").is_err());
        assert!(parse("1.").is_err());
        assert!(parse("1e").is_err());
    }

    /// Verify parse rejects unterminated structures.
    #[test]
    fn parse_rejects_unterminated_structures() {
        assert!(parse(r#""unterminated"#).is_err());
        assert!(parse("[1,2").is_err());
        assert!(parse(r#"{"a":1"#).is_err());
    }

    /// Verify parse rejects invalid escapes and controls.
    #[test]
    fn parse_rejects_invalid_escapes_and_controls() {
        assert!(parse(r#""\q""#).is_err());
        assert!(parse("\"line\nfeed\"").is_err());
        assert!(parse(r#""\ud800""#).is_err());
    }

    /// Verify json accessors reject wrong types.
    #[test]
    fn json_accessors_reject_wrong_types() {
        let value = parse(r#"{"text":"x","count":4,"items":[]}"#).expect("object parses");
        assert_eq!(value.get("missing"), None);
        assert!(value.as_array().is_none());
        assert!(value.get("text").and_then(Json::as_i64).is_none());
        assert!(value.get("count").and_then(Json::as_str).is_none());
        assert!(value.get("items").and_then(Json::as_array).is_some());
    }
}
