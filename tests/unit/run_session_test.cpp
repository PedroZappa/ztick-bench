// SPDX-License-Identifier: MIT
/**
 * @file run_session_test.cpp
 * @brief Catch2 tests for core config-driven session orchestration.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/config_document.hpp>
#include <ztick/config_value.hpp>
#include <ztick/metadata.hpp>
#include <ztick/result_document.hpp>
#include <ztick/run_session.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ztick::ConfigValue;
using ztick::SessionContext;

/** @brief Fixed clock increment in seconds used by every deterministic test. */
constexpr double kClockStepSeconds = 0.001;

/**
 * @brief Build a complete v1 config with a deterministic built-in generator.
 * @param iterations Number of processing blocks in one repetition.
 * @param repetitions Number of repetitions in the session.
 * @return Compact JSON config text accepted by the schema and semantic planner.
 */
std::string make_config(std::int64_t iterations, std::int64_t repetitions) {
  std::string text = R"json({"config_version":"1.0.0","sdk_version":"1.0.0",)json"
                     R"json("integration":{"id":"app-a","version":"1.2.3"},)json"
                     R"json("workload":{"level":"processor","processors":["eq"],)json"
                     R"json("generator":{"kind":"sine","amplitude":0.5,"frequency_hz":1000.0,)json"
                     R"json("phase_rad":0.0}},)json"
                     R"json("dimensions":{"sample_rate_hz":48000.0,"block_size":128,)json"
                     R"json("channels":2,"oversampling":1,"instances":1},)json"
                     R"json("execution":{"duration_mode":"iterations","iterations":)json";
  text += std::to_string(iterations);
  text += R"json(,"duration_seconds":0.0,"repetitions":)json";
  text += std::to_string(repetitions);
  text += R"json(,"warmup":{"iterations":0},"isolation":"auto"},)json"
          R"json("profile":{"name":"Quick"},)json"
          R"json("measurement":{"detail":"aggregate","end_to_end":false,)json"
          R"json("timing_levels":["per_block","whole_run"]},)json"
          R"json("validation":{"level":"Basic","tolerances":{},"references":[]},)json"
          R"json("parallelism":{"workers":"auto","affinity":"auto"}})json";
  return text;
}

/** @brief Shared state behind a deterministic injected clock. */
struct ClockState {
  /** @brief Current synthetic time in seconds. */
  double seconds{0.0};
};

/**
 * @brief Build a fully injected session context with a deterministic clock.
 * @param state Optional shared clock state; a fresh state is created when null.
 * @return Context whose clock advances by @ref kClockStepSeconds on every call.
 */
SessionContext make_context(const std::shared_ptr<ClockState> &state = nullptr) {
  const std::shared_ptr<ClockState> clock_state =
      state == nullptr ? std::make_shared<ClockState>() : state;
  SessionContext context;
  context.clock = [clock_state]() {
    clock_state->seconds += kClockStepSeconds;
    return clock_state->seconds;
  };
  context.machine_metadata = []() {
    return ztick::make_metadata_object({ztick::metadata_value("hostname", "test-host"),
                                        ztick::metadata_value("kernel", "test-kernel")});
  };
  context.session_id = []() { return std::string("session-test"); };
  context.timestamp = []() { return std::string("2026-01-01T00:00:00Z"); };
  return context;
}

/**
 * @brief Read a required string member from an object.
 * @param object Object to search.
 * @param key Member name.
 * @return Stored string, or an empty string for a missing or mistyped member.
 */
std::string string_member(const ConfigValue &object, std::string_view key) {
  const ConfigValue *member = object.find(key);
  if (member == nullptr || !member->as_string().has_value()) {
    return {};
  }
  return *member->as_string();
}

/**
 * @brief Find one canonical metric record by its stable identifier.
 * @param run Run record containing a `metrics` array.
 * @param id Metric identifier, such as `MET-01`.
 * @return Pointer to the matching record, or null when absent.
 */
const ConfigValue *find_metric(const ConfigValue &run, std::string_view id) {
  const ConfigValue *metrics = run.find("metrics");
  if (metrics == nullptr || metrics->as_array() == nullptr) {
    return nullptr;
  }
  for (const ConfigValue &metric : *metrics->as_array()) {
    if (string_member(metric, "id") == id) {
      return &metric;
    }
  }
  return nullptr;
}

/**
 * @brief Read the numeric value of one canonical metric record.
 * @param run Run record containing the metric.
 * @param id Metric identifier to read.
 * @return Recorded real value; a Catch2 assertion fails for a missing metric.
 */
double metric_value(const ConfigValue &run, std::string_view id) {
  const ConfigValue *metric = find_metric(run, id);
  REQUIRE(metric != nullptr);
  const ConfigValue *value = metric->find("value");
  REQUIRE(value != nullptr);
  REQUIRE(value->as_real().has_value());
  return *value->as_real();
}

} // namespace

/**
 * @brief Verify a valid config produces a complete result with the frozen snapshot.
 */
TEST_CASE("run session executes a valid config", "[core][run]") {
  const auto parsed = ztick::parse_config_document(make_config(4, 1));
  REQUIRE(parsed.valid());

  std::string error;
  const auto result = ztick::run_configured_session(parsed.document, make_context(), &error);
  REQUIRE(result.has_value());
  REQUIRE(error.empty());
  REQUIRE(ztick::validate_result_document(*result));
  REQUIRE(string_member(*result, "status") == "complete");
  REQUIRE(string_member(*result->find("session"), "id") == "session-test");
  REQUIRE(*(result->find("config_snapshot")) == parsed.document);

  const ConfigValue *runs = result->find("runs");
  REQUIRE(runs != nullptr);
  REQUIRE(runs->as_array() != nullptr);
  REQUIRE(runs->as_array()->size() == 1U);
  const ConfigValue &run = runs->as_array()->front();
  REQUIRE(string_member(run, "status") == "complete");
  const ConfigValue *validation = run.find("validation");
  REQUIRE(validation != nullptr);
  REQUIRE(validation->find("checks") != nullptr);
}

/**
 * @brief Verify canonical timing and throughput values against independent constants.
 */
TEST_CASE("run session records independently computed metrics", "[core][run][metrics]") {
  const auto parsed = ztick::parse_config_document(make_config(4, 1));
  REQUIRE(parsed.valid());

  const auto result = ztick::run_configured_session(parsed.document, make_context());
  REQUIRE(result.has_value());
  const ConfigValue &run = result->find("runs")->as_array()->front();

  const ConfigValue *timing = run.find("timing");
  REQUIRE(timing != nullptr);
  const ConfigValue *block_samples = timing->find("per_block");
  REQUIRE(block_samples != nullptr);
  REQUIRE(block_samples->as_array() != nullptr);
  REQUIRE(block_samples->as_array()->size() == 4U);
  for (const ConfigValue &sample : *block_samples->as_array()) {
    REQUIRE(sample.as_real().has_value());
    REQUIRE(*sample.as_real() == Catch::Approx(kClockStepSeconds));
  }

  // Independent expectations: four blocks of 128 frames at a synthetic 1 ms/block.
  constexpr double kExpectedProcessingSeconds = 4.0 * kClockStepSeconds;
  constexpr double kExpectedFrames = 4.0 * 128.0;
  constexpr double kExpectedAudioSeconds = kExpectedFrames / 48000.0;
  REQUIRE(metric_value(run, "MET-01") == Catch::Approx(kExpectedProcessingSeconds));
  REQUIRE(metric_value(run, "MET-02") == Catch::Approx(kClockStepSeconds));
  REQUIRE(metric_value(run, "MET-11") ==
          Catch::Approx(kExpectedFrames / kExpectedProcessingSeconds));
  REQUIRE(metric_value(run, "MET-12.audio") == Catch::Approx(kExpectedAudioSeconds));
  REQUIRE(metric_value(run, "MET-12") ==
          Catch::Approx(kExpectedProcessingSeconds / kExpectedAudioSeconds));
}

/**
 * @brief Verify incomplete context services are rejected before execution.
 */
TEST_CASE("run session requires complete injected context", "[core][run]") {
  const auto parsed = ztick::parse_config_document(make_config(2, 1));
  REQUIRE(parsed.valid());

  SessionContext context = make_context();
  context.session_id = {};
  std::string error;
  REQUIRE_FALSE(ztick::run_configured_session(parsed.document, context, &error));
  REQUIRE(error.find("session context is incomplete") != std::string::npos);
}

/**
 * @brief Verify semantic planner failures are reported instead of producing a result.
 */
TEST_CASE("run session rejects semantically invalid configs", "[core][run]") {
  const auto parsed = ztick::parse_config_document(make_config(0, 1));
  REQUIRE(parsed.valid());

  std::string error;
  REQUIRE_FALSE(ztick::run_configured_session(parsed.document, make_context(), &error));
  REQUIRE_FALSE(error.empty());
  REQUIRE(error.find("execution") != std::string::npos);
}
