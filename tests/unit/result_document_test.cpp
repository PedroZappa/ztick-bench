// SPDX-License-Identifier: MIT
/**
 * @file result_document_test.cpp
 * @brief Catch2 tests for the result schema, status derivation and writer.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/config_document.hpp>
#include <ztick/result_document.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * @brief Build a complete v1 config document for the config snapshot field.
 * @return JSON text satisfying the config document schema.
 */
std::string make_config() {
  std::string text = R"json({"config_version":"1.0.0","sdk_version":"1.0.0",)json";
  text.append(R"json("integration":{"id":"app-a","version":"1.2.3"},)json");
  text.append(R"json("workload":{"level":"processor","processors":["eq"],)json");
  text.append(R"json("generator":{"type":"noise"}},)json");
  text.append(R"json("dimensions":{"sample_rate_hz":48000,"block_size":128,)json");
  text.append(R"json("channels":2,"oversampling":1,"instances":1},)json");
  text.append(R"json("execution":{"duration_mode":"iterations","iterations":1000,)json");
  text.append(R"json("repetitions":3,"warmup":{"iterations":100},"isolation":"auto"},)json");
  text.append(R"json("profile":{"name":"Quick"},)json");
  text.append(R"json("measurement":{"detail":"aggregate","end_to_end":false,)json");
  text.append(R"json("timing_levels":["per_block","whole_run"]},)json");
  text.append(R"json("validation":{"level":"Basic","tolerances":{},"references":[]},)json");
  text.append(R"json("parallelism":{"workers":"auto","affinity":"auto"}})json");
  return text;
}

/**
 * @brief Build a run record with an explicit status and optional failure context.
 * @param status Status token to store.
 * @param with_failure Whether to attach one failure entry.
 * @return Run-shaped ConfigValue.
 */
ztick::ConfigValue make_run(std::string_view status, bool with_failure = false) {
  ztick::ConfigValue run = ztick::ConfigValue::object();
  run.set("id", ztick::ConfigValue::string("run-1"));
  run.set("test_point", ztick::ConfigValue::object());
  run.set("status", ztick::ConfigValue::string(std::string(status)));
  ztick::ConfigValue failures = ztick::ConfigValue::array();
  if (with_failure) {
    failures.push_back(ztick::ConfigValue::string("adapter crash"));
  }
  run.set("failures", std::move(failures));
  return run;
}

/**
 * @brief Build a complete result document for the given runs.
 * @param session_status Initial session status token.
 * @param runs Run records to attach.
 * @param session_failure Whether to attach a session-level failure entry.
 * @return Result-shaped ConfigValue with every required top-level field.
 */
ztick::ConfigValue make_document(std::string_view session_status,
                                 const std::vector<ztick::ConfigValue> &runs,
                                 bool session_failure = false) {
  ztick::ConfigValue document = ztick::ConfigValue::object();
  document.set("schema_version",
               ztick::ConfigValue::string(std::string(ztick::kResultSchemaVersion)));
  document.set("sdk_version", ztick::ConfigValue::string("1.0.0"));

  ztick::ConfigValue session = ztick::ConfigValue::object();
  session.set("id", ztick::ConfigValue::string("session-1"));
  session.set("started_at", ztick::ConfigValue::string("2026-11-23T00:00:00Z"));
  document.set("session", std::move(session));

  const auto config = ztick::parse_config_document(make_config());
  document.set("config_snapshot", config.document);
  document.set("build", ztick::ConfigValue::object());
  document.set("machine", ztick::ConfigValue::object());
  document.set("environment", ztick::ConfigValue::object());

  ztick::ConfigValue run_array = ztick::ConfigValue::array();
  for (const ztick::ConfigValue &run : runs) {
    run_array.push_back(run);
  }
  document.set("runs", std::move(run_array));
  document.set("comparisons", ztick::ConfigValue::array());
  document.set("warnings", ztick::ConfigValue::array());

  ztick::ConfigValue failures = ztick::ConfigValue::array();
  if (session_failure) {
    failures.push_back(ztick::ConfigValue::string("monitor unavailable"));
  }
  document.set("failures", std::move(failures));
  document.set("status", ztick::ConfigValue::string(std::string(session_status)));
  return document;
}

/**
 * @brief Return a dynamically generated path for one test result file.
 * @param name Short discriminator for the calling test case.
 * @return Path inside the system temp directory.
 */
std::filesystem::path result_path(std::string_view name) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ztick_result_" + std::string(name) + "_" + std::to_string(ticks) + ".json");
}

/**
 * @brief Remove a generated result file without throwing.
 * @param path Path to remove.
 */
void remove_file(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

} // namespace

/**
 * @brief Verify status tokens map to and from their documented JSON strings.
 */
TEST_CASE("result status tokens round trip", "[results][result]") {
  using ztick::RunStatus;
  using ztick::SessionStatus;

  REQUIRE(ztick::run_status_token(RunStatus::complete) == "complete");
  REQUIRE(ztick::run_status_token(RunStatus::incomplete) == "incomplete");
  REQUIRE(ztick::run_status_token(RunStatus::failed) == "failed");
  REQUIRE(ztick::parse_run_status("complete") == RunStatus::complete);
  REQUIRE(ztick::parse_run_status("incomplete") == RunStatus::incomplete);
  REQUIRE(ztick::parse_run_status("failed") == RunStatus::failed);
  REQUIRE_FALSE(ztick::parse_run_status("complete ").has_value());

  REQUIRE(ztick::session_status_token(SessionStatus::complete) == "complete");
  REQUIRE(ztick::session_status_token(SessionStatus::incomplete) == "incomplete");
  REQUIRE(ztick::parse_session_status("complete") == SessionStatus::complete);
  REQUIRE(ztick::parse_session_status("incomplete") == SessionStatus::incomplete);
  REQUIRE_FALSE(ztick::parse_session_status("failed").has_value());
}

/**
 * @brief Verify a stale incomplete token is corrected when every run completed.
 */
TEST_CASE("complete runs derive a complete session", "[results][result]") {
  ztick::ConfigValue document = make_document("incomplete", {make_run("complete")});
  REQUIRE(ztick::validate_result_document(document));

  std::string error;
  REQUIRE(ztick::finalize_result_document(document, &error));
  REQUIRE(error.empty());
  REQUIRE(*(document.find("status")->as_string()) == "complete");
}

/**
 * @brief Verify incomplete, failed and session-level failures force incompleteness.
 */
TEST_CASE("incomplete evidence derives an incomplete session", "[results][result]") {
  ztick::ConfigValue interrupted = make_document("complete", {make_run("incomplete")});
  REQUIRE(ztick::finalize_result_document(interrupted));
  REQUIRE(*(interrupted.find("status")->as_string()) == "incomplete");

  ztick::ConfigValue failed = make_document("complete", {make_run("failed")});
  REQUIRE(ztick::finalize_result_document(failed));
  REQUIRE(*(failed.find("status")->as_string()) == "incomplete");

  ztick::ConfigValue session_failure = make_document("complete", {make_run("complete")}, true);
  REQUIRE(ztick::finalize_result_document(session_failure));
  REQUIRE(*(session_failure.find("status")->as_string()) == "incomplete");
}

/**
 * @brief Verify a run with recorded failures cannot stay marked complete.
 */
TEST_CASE("a run with failures is marked incomplete", "[results][result]") {
  ztick::ConfigValue document = make_document("complete", {make_run("complete", true)});

  REQUIRE(ztick::finalize_result_document(document));
  REQUIRE(*(document.find("runs")->as_array()->at(0).find("status")->as_string()) == "incomplete");
  REQUIRE(*(document.find("status")->as_string()) == "incomplete");
}

/**
 * @brief Verify a malformed run status is rejected without mutating the document.
 */
TEST_CASE("finalization rejects malformed runs without changes", "[results][result]") {
  ztick::ConfigValue document = make_document("complete", {make_run("bogus")});
  const ztick::ConfigValue original = document;

  std::string error;
  REQUIRE_FALSE(ztick::finalize_result_document(document, &error));
  REQUIRE_FALSE(error.empty());
  REQUIRE(document == original);
}

/**
 * @brief Verify the writer finalizes, encodes and atomically stores a document.
 */
TEST_CASE("result document round trips through an atomic write", "[results][result]") {
  const std::filesystem::path path = result_path("roundtrip");
  const ztick::ConfigValue document = make_document("complete", {make_run("failed")});

  const auto written = ztick::write_result_document(path, document);
  REQUIRE(written.valid());
  REQUIRE(written.offset > 0U);
  REQUIRE(std::filesystem::exists(path));

  const auto read = ztick::read_result_document(path);
  REQUIRE(read.valid());
  REQUIRE(*(read.document.find("status")->as_string()) == "incomplete");
  REQUIRE(*(read.document.find("runs")->as_array()->at(0).find("status")->as_string()) == "failed");
  REQUIRE(ztick::validate_result_document(read.document));

  remove_file(path);
}

/**
 * @brief Verify invalid documents are rejected before a file is created.
 */
TEST_CASE("result writer validates before writing", "[results][result]") {
  const std::filesystem::path path = result_path("invalid");
  const ztick::ConfigValue partial = ztick::ConfigValue::object();

  const auto written = ztick::write_result_document(path, partial);
  REQUIRE_FALSE(written.valid());
  REQUIRE_FALSE(written.error.empty());
  REQUIRE_FALSE(std::filesystem::exists(path));
}

/**
 * @brief Verify schema, SDK, section and snapshot errors are reported explicitly.
 */
TEST_CASE("result document rejects malformed schemas", "[results][result]") {
  std::string error;

  ztick::ConfigValue future = make_document("complete", {});
  future.set("schema_version", ztick::ConfigValue::string("2.0.0"));
  REQUIRE_FALSE(ztick::validate_result_document(future, &error));
  REQUIRE(error.find("schema_version") != std::string::npos);

  ztick::ConfigValue bad_sdk = make_document("complete", {});
  bad_sdk.set("sdk_version", ztick::ConfigValue::string("1.1.0"));
  REQUIRE_FALSE(ztick::validate_result_document(bad_sdk, &error));
  REQUIRE(error.find("sdk_version") != std::string::npos);

  ztick::ConfigValue bad_runs = make_document("complete", {});
  bad_runs.set("runs", ztick::ConfigValue::object());
  REQUIRE_FALSE(ztick::validate_result_document(bad_runs, &error));
  REQUIRE(error.find("runs must be array") != std::string::npos);

  ztick::ConfigValue bad_session = make_document("complete", {});
  bad_session.find("session")->set("id", ztick::ConfigValue::string(""));
  REQUIRE_FALSE(ztick::validate_result_document(bad_session, &error));
  REQUIRE(error.find("session.id") != std::string::npos);

  ztick::ConfigValue bad_snapshot = make_document("complete", {});
  bad_snapshot.set("config_snapshot", ztick::ConfigValue::object());
  REQUIRE_FALSE(ztick::validate_result_document(bad_snapshot, &error));
  REQUIRE(error.find("config snapshot") != std::string::npos);
}

/**
 * @brief Verify parse and file-read entry points report their failures.
 */
TEST_CASE("result document readers report parse and file errors", "[results][result]") {
  std::string error;
  REQUIRE_FALSE(ztick::validate_result_document(ztick::ConfigValue::null(), &error));
  REQUIRE(error.find("must be an object") != std::string::npos);

  const auto bad_json = ztick::parse_result_document("{not json");
  REQUIRE_FALSE(bad_json.valid());
  REQUIRE_FALSE(bad_json.error.empty());

  const auto missing = ztick::read_result_document(std::filesystem::temp_directory_path() /
                                                   "ztick_result_does_not_exist.json");
  REQUIRE_FALSE(missing.valid());
  REQUIRE(missing.error.find("could not open") != std::string::npos);
}
