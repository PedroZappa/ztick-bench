// SPDX-License-Identifier: MIT
/**
 * @file result_document.cpp
 * @brief Implementation of the result document schema and atomic writer declared
 *        in @ref result_document.hpp.
 */

#include "ztick/result_document.hpp"

#include "ztick/config_document.hpp"
#include "ztick/json.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ztick {

namespace {

/** @brief Required top-level result fields and their JSON value types. */
constexpr std::array<std::pair<std::string_view, ConfigValueType>, 12> kRequiredFields{{
    {"schema_version", ConfigValueType::string},
    {"sdk_version", ConfigValueType::string},
    {"session", ConfigValueType::object},
    {"config_snapshot", ConfigValueType::object},
    {"build", ConfigValueType::object},
    {"machine", ConfigValueType::object},
    {"environment", ConfigValueType::object},
    {"runs", ConfigValueType::array},
    {"comparisons", ConfigValueType::array},
    {"warnings", ConfigValueType::array},
    {"failures", ConfigValueType::array},
    {"status", ConfigValueType::string},
}};

/**
 * @brief Validate one run record and optionally demote a contradictory status.
 * @param run Run record to inspect.
 * @param error Optional receiver for a human-readable failure explanation.
 * @param mutable_run When non-null, a `complete` run with recorded failures is
 *        rewritten to `incomplete`.
 * @return True when the run record has a valid shape and status token.
 */
bool check_run(const ConfigValue &run, std::string *error, ConfigValue *mutable_run) {
  if (run.type() != ConfigValueType::object) {
    if (error != nullptr) {
      *error = "run record must be an object";
    }
    return false;
  }

  const ConfigValue *status = run.find("status");
  if (status == nullptr || status->type() != ConfigValueType::string) {
    if (error != nullptr) {
      *error = "run.status must be a string";
    }
    return false;
  }
  const std::optional<RunStatus> parsed = parse_run_status(*(status->as_string()));
  if (!parsed.has_value()) {
    if (error != nullptr) {
      *error = "invalid run status: " + *(status->as_string());
    }
    return false;
  }

  const ConfigValue *failures = run.find("failures");
  if (failures != nullptr && failures->type() != ConfigValueType::array) {
    if (error != nullptr) {
      *error = "run.failures must be an array";
    }
    return false;
  }

  if (mutable_run != nullptr && *parsed == RunStatus::complete && failures != nullptr &&
      !failures->as_array()->empty()) {
    mutable_run->set("status",
                     ConfigValue::string(std::string(run_status_token(RunStatus::incomplete))));
  }
  return true;
}

} // namespace

std::string_view run_status_token(RunStatus status) noexcept {
  switch (status) {
  case RunStatus::complete:
    return "complete";
  case RunStatus::incomplete:
    return "incomplete";
  case RunStatus::failed:
    return "failed";
  }
  return "unknown";
}

std::optional<RunStatus> parse_run_status(std::string_view token) noexcept {
  if (token == run_status_token(RunStatus::complete)) {
    return RunStatus::complete;
  }
  if (token == run_status_token(RunStatus::incomplete)) {
    return RunStatus::incomplete;
  }
  if (token == run_status_token(RunStatus::failed)) {
    return RunStatus::failed;
  }
  return std::nullopt;
}

std::string_view session_status_token(SessionStatus status) noexcept {
  switch (status) {
  case SessionStatus::complete:
    return "complete";
  case SessionStatus::incomplete:
    return "incomplete";
  }
  return "unknown";
}

std::optional<SessionStatus> parse_session_status(std::string_view token) noexcept {
  if (token == session_status_token(SessionStatus::complete)) {
    return SessionStatus::complete;
  }
  if (token == session_status_token(SessionStatus::incomplete)) {
    return SessionStatus::incomplete;
  }
  return std::nullopt;
}

bool validate_result_document(const ConfigValue &document, std::string *error) {
  if (document.type() != ConfigValueType::object) {
    if (error != nullptr) {
      *error = "result document must be an object";
    }
    return false;
  }
  for (const auto &[name, expected_type] : kRequiredFields) {
    const ConfigValue *field = document.find(name);
    if (field == nullptr) {
      if (error != nullptr) {
        *error = "missing result field: " + std::string(name);
      }
      return false;
    }
    if (field->type() != expected_type) {
      if (error != nullptr) {
        *error =
            std::string(name) + " must be " + std::string(config_value_type_name(expected_type));
      }
      return false;
    }
  }

  const std::string schema_version = *(document.find("schema_version")->as_string());
  if (schema_version != kResultSchemaVersion) {
    if (error != nullptr) {
      *error = "unsupported schema_version: " + schema_version;
    }
    return false;
  }

  const std::string sdk_version_text = *(document.find("sdk_version")->as_string());
  const std::optional<SemanticVersion> sdk_version = parse_semantic_version(sdk_version_text);
  if (!sdk_version.has_value()) {
    if (error != nullptr) {
      *error = "malformed sdk_version: " + sdk_version_text;
    }
    return false;
  }
  if (!sdk_version_supported(*sdk_version)) {
    if (error != nullptr) {
      *error = "unsupported sdk_version: " + sdk_version_text;
    }
    return false;
  }

  const ConfigValue *session = document.find("session");
  const ConfigValue *session_id = session->find("id");
  if (session_id == nullptr || session_id->type() != ConfigValueType::string ||
      session_id->as_string()->empty()) {
    if (error != nullptr) {
      *error = "session.id must be a non-empty string";
    }
    return false;
  }

  std::string snapshot_error;
  if (!validate_config_document(*(document.find("config_snapshot")), &snapshot_error)) {
    if (error != nullptr) {
      *error = "config snapshot: " + snapshot_error;
    }
    return false;
  }

  const std::string status = *(document.find("status")->as_string());
  if (!parse_session_status(status).has_value()) {
    if (error != nullptr) {
      *error = "invalid session status: " + status;
    }
    return false;
  }

  std::size_t index = 0;
  for (const ConfigValue &run : *(document.find("runs")->as_array())) {
    if (!check_run(run, error, nullptr)) {
      if (error != nullptr) {
        *error = "runs[" + std::to_string(index) + "]: " + *error;
      }
      return false;
    }
    ++index;
  }
  return true;
}

bool finalize_result_document(ConfigValue &document, std::string *error) {
  ConfigValue normalized = document;
  if (!validate_result_document(normalized, error)) {
    return false;
  }

  bool incomplete = false;
  for (ConfigValue &run : *(normalized.find("runs")->as_array())) {
    if (!check_run(run, error, &run)) {
      return false;
    }
    const RunStatus status = *(parse_run_status(*(run.find("status")->as_string())));
    if (status != RunStatus::complete) {
      incomplete = true;
    }
  }
  if (!normalized.find("failures")->as_array()->empty()) {
    incomplete = true;
  }

  const SessionStatus derived = incomplete ? SessionStatus::incomplete : SessionStatus::complete;
  normalized.set("status", ConfigValue::string(std::string(session_status_token(derived))));
  document = std::move(normalized);
  return true;
}

ResultParseResult parse_result_document(std::string_view text) {
  ResultParseResult result;
  const JsonReadResult json = json_read(text);
  if (!json.ok) {
    result.error = json.error;
    result.offset = json.offset;
    return result;
  }
  if (!validate_result_document(json.value, &result.error)) {
    return result;
  }
  result.document = json.value;
  result.offset = json.offset;
  result.ok = true;
  return result;
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ResultParseResult read_result_document(const std::filesystem::path &path) {
  ResultParseResult result;
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    result.error = "could not open result file: " + path.string();
    return result;
  }
  const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  if (stream.bad()) {
    result.error = "could not read result file: " + path.string();
    return result;
  }
  return parse_result_document(text);
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
ResultWriteResult write_result_document(const std::filesystem::path &target,
                                        const ConfigValue &document) {
  ResultWriteResult result;
  ConfigValue normalized = document;
  if (!finalize_result_document(normalized, &result.error)) {
    return result;
  }
  const JsonWriteResult json = json_write(normalized);
  if (!json.ok) {
    result.error = json.error;
    result.offset = json.offset;
    return result;
  }
  const FileWriteResult file = write_file_atomic(target, json.text);
  if (!file.ok) {
    result.error = file.error;
    result.offset = json.text.size();
    return result;
  }
  result.offset = json.text.size();
  result.ok = true;
  return result;
}

} // namespace ztick
