// SPDX-License-Identifier: MIT
/**
 * @file runner_test.cpp
 * @brief Deterministic tests for runner selection, run records and in-process execution.
 *
 * Expected modes, statuses and tokens are independent literals or public SDK
 * constants. The tests also prove that selection and refusal paths never invoke the
 * supplied workload, so mode selection cannot execute plugin code (BR §6.3).
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/runner.hpp>

#include <ztick/sdk.h>
#include <ztick/types.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

/**
 * @brief Build an authorized selection request for one isolation mode.
 * @param requested Isolation mode to request.
 * @return Input with trust already authorized.
 */
ztick::plugin::RunnerSelectionInput
authorized_input(ztick::plugin::IsolationRequest requested) noexcept {
  ztick::plugin::RunnerSelectionInput input;
  input.requested = requested;
  input.trust_authorized = true;
  return input;
}

} // namespace

/**
 * @brief Verify portable isolation tokens parse and render without implicit values.
 */
TEST_CASE("isolation tokens round-trip", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  REQUIRE(ztick::plugin::parse_isolation_request("auto") == IsolationRequest::auto_select);
  REQUIRE(ztick::plugin::parse_isolation_request("in_process") == IsolationRequest::in_process);
  REQUIRE(ztick::plugin::parse_isolation_request("out_of_process") ==
          IsolationRequest::out_of_process);

  CHECK(ztick::plugin::parse_isolation_request("") == std::nullopt);
  CHECK(ztick::plugin::parse_isolation_request("in-process") == std::nullopt);
  CHECK(ztick::plugin::parse_isolation_request("IN_PROCESS") == std::nullopt);

  CHECK(ztick::plugin::isolation_request_name(IsolationRequest::auto_select) == "auto");
  CHECK(ztick::plugin::isolation_request_name(IsolationRequest::in_process) == "in_process");
  CHECK(ztick::plugin::isolation_request_name(IsolationRequest::out_of_process) ==
        "out_of_process");
  CHECK(ztick::plugin::isolation_request_name(static_cast<IsolationRequest>(99)) == "unknown");

  CHECK(ztick::plugin::execution_mode_name(ztick_execution_mode_in_process) == "in_process");
  CHECK(ztick::plugin::execution_mode_name(ztick_execution_mode_out_of_process) ==
        "out_of_process");
  CHECK(ztick::plugin::execution_mode_name(99) == "unknown");
}

/**
 * @brief Verify no mode is selectable before trust authorization.
 */
TEST_CASE("runner selection requires trust", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  ztick::plugin::RunnerSelectionInput input;
  input.requested = IsolationRequest::in_process;
  input.trust_authorized = false;

  const ztick::plugin::RunnerSelection result = ztick::plugin::select_runner(input);
  CHECK_FALSE(result.selectable());
  CHECK(result.status == ztick_status_invalid_argument);
  CHECK(std::string(result.reason) == "plugin is not authorized for execution");
  CHECK(result.requested == IsolationRequest::in_process);
  CHECK_FALSE(result.mode.has_value());
}

/**
 * @brief Verify auto and explicit in-process requests select the low-overhead mode.
 */
TEST_CASE("runner selection prefers in-process for trusted integrations", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  const ztick::plugin::RunnerSelection automatic =
      ztick::plugin::select_runner(authorized_input(IsolationRequest::auto_select));
  REQUIRE(automatic.selectable());
  CHECK(automatic.status == ztick_status_success);
  CHECK(automatic.reason == nullptr);
  REQUIRE(automatic.mode.has_value());
  CHECK(*automatic.mode == ztick_execution_mode_in_process);
  CHECK(automatic.requested == IsolationRequest::auto_select);

  const ztick::plugin::RunnerSelection explicit_mode =
      ztick::plugin::select_runner(authorized_input(IsolationRequest::in_process));
  REQUIRE(explicit_mode.selectable());
  REQUIRE(explicit_mode.mode.has_value());
  CHECK(*explicit_mode.mode == ztick_execution_mode_in_process);
  CHECK(explicit_mode.requested == IsolationRequest::in_process);
}

/**
 * @brief Verify out-of-process selection depends on an available backend.
 */
TEST_CASE("runner selection checks out-of-process availability", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  const ztick::plugin::RunnerSelection missing =
      ztick::plugin::select_runner(authorized_input(IsolationRequest::out_of_process));
  CHECK_FALSE(missing.selectable());
  CHECK(missing.status == ztick_status_unsupported);
  CHECK(std::string(missing.reason) == "out-of-process runner is not available");

  ztick::plugin::RunnerSelectionInput available =
      authorized_input(IsolationRequest::out_of_process);
  available.out_of_process_available = true;
  const ztick::plugin::RunnerSelection result = ztick::plugin::select_runner(available);
  REQUIRE(result.selectable());
  REQUIRE(result.mode.has_value());
  CHECK(*result.mode == ztick_execution_mode_out_of_process);
  CHECK(result.requested == IsolationRequest::out_of_process);
  CHECK(result.reason == nullptr);
}

/**
 * @brief Verify an invalid request enum is refused rather than silently mapped.
 */
TEST_CASE("runner selection refuses unknown requests", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  const ztick::plugin::RunnerSelection result =
      ztick::plugin::select_runner(authorized_input(static_cast<IsolationRequest>(99)));
  CHECK_FALSE(result.selectable());
  CHECK(result.status == ztick_status_invalid_argument);
  CHECK(std::string(result.reason) == "unknown isolation request");
}

/**
 * @brief Verify execution records retain both requested and selected modes.
 */
TEST_CASE("execution records retain the selected mode", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  const ztick::plugin::RunnerSelection selection =
      ztick::plugin::select_runner(authorized_input(IsolationRequest::auto_select));
  const ztick::plugin::RunExecutionRecord record =
      ztick::plugin::make_execution_record(selection, ztick_status_success, nullptr);

  REQUIRE(record.mode.has_value());
  CHECK(*record.mode == ztick_execution_mode_in_process);
  CHECK(record.requested == IsolationRequest::auto_select);
  CHECK(record.status == ztick_status_success);
  CHECK(record.reason == nullptr);
  CHECK(record.succeeded());

  const ztick::plugin::RunExecutionRecord failed = ztick::plugin::make_execution_record(
      selection, ztick_status_processing_failure, "workload failed");
  REQUIRE(failed.mode.has_value());
  CHECK(*failed.mode == ztick_execution_mode_in_process);
  CHECK_FALSE(failed.succeeded());
  CHECK(std::string(failed.reason) == "workload failed");
}

/**
 * @brief Verify the in-process runner executes an authorized workload once.
 */
TEST_CASE("in-process runner executes the supplied workload", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  ztick::plugin::InProcessRunner runner;
  ztick::plugin::Runner &base = runner;
  ztick::plugin::RunRequest request;
  request.selection = ztick::plugin::select_runner(authorized_input(IsolationRequest::in_process));

  int calls = 0;
  request.workload = [&calls]() -> std::int32_t {
    ++calls;
    return ztick_status_success;
  };

  const ztick::plugin::RunExecutionRecord record = base.run(request);
  CHECK(calls == 1);
  CHECK(record.succeeded());
  CHECK(record.status == ztick_status_success);
  CHECK(record.reason == nullptr);
  REQUIRE(record.mode.has_value());
  CHECK(*record.mode == ztick_execution_mode_in_process);
  CHECK(record.requested == IsolationRequest::in_process);
}

/**
 * @brief Verify the convenience wrapper uses the same in-process policy.
 */
TEST_CASE("in-process convenience runner delegates correctly", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  ztick::plugin::RunRequest request;
  request.selection = ztick::plugin::select_runner(authorized_input(IsolationRequest::in_process));
  int calls = 0;
  request.workload = [&calls]() -> std::int32_t {
    ++calls;
    return ztick_status_success;
  };

  const ztick::plugin::RunExecutionRecord record = ztick::plugin::run_in_process(request);
  CHECK(calls == 1);
  CHECK(record.succeeded());
  REQUIRE(record.mode.has_value());
  CHECK(*record.mode == ztick_execution_mode_in_process);
  CHECK(record.requested == IsolationRequest::in_process);
}

/**
 * @brief Verify invalid runner requests are refused without invoking any workload.
 */
TEST_CASE("in-process runner refuses invalid requests", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  ztick::plugin::InProcessRunner runner;
  ztick::plugin::RunRequest request;
  int calls = 0;
  request.workload = [&calls]() -> std::int32_t {
    ++calls;
    return ztick_status_success;
  };

  SECTION("untrusted selection") {
    ztick::plugin::RunnerSelectionInput untrusted;
    untrusted.requested = IsolationRequest::in_process;
    request.selection = ztick::plugin::select_runner(untrusted);

    const ztick::plugin::RunExecutionRecord record = runner.run(request);
    CHECK(calls == 0);
    CHECK_FALSE(record.succeeded());
    CHECK(record.status == ztick_status_invalid_argument);
    CHECK(std::string(record.reason) == "plugin is not authorized for execution");
    CHECK_FALSE(record.mode.has_value());
  }

  SECTION("out-of-process selection") {
    ztick::plugin::RunnerSelectionInput remote = authorized_input(IsolationRequest::out_of_process);
    remote.out_of_process_available = true;
    request.selection = ztick::plugin::select_runner(remote);

    const ztick::plugin::RunExecutionRecord record = runner.run(request);
    CHECK(calls == 0);
    CHECK_FALSE(record.succeeded());
    CHECK(record.status == ztick_status_unsupported);
    CHECK(std::string(record.reason) == "in-process runner cannot execute out-of-process mode");
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_out_of_process);
  }

  SECTION("missing workload") {
    request.selection =
        ztick::plugin::select_runner(authorized_input(IsolationRequest::in_process));
    request.workload = nullptr;

    const ztick::plugin::RunExecutionRecord record = runner.run(request);
    CHECK(calls == 0);
    CHECK(record.status == ztick_status_invalid_argument);
    CHECK(std::string(record.reason) == "no in-process workload supplied");
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_in_process);
  }
}

/**
 * @brief Verify workload failures and exceptions are recorded with their mode.
 */
TEST_CASE("in-process runner records workload failures", "[plugin][runner]") {
  using ztick::plugin::IsolationRequest;

  ztick::plugin::InProcessRunner runner;
  ztick::plugin::RunRequest request;
  request.selection = ztick::plugin::select_runner(authorized_input(IsolationRequest::auto_select));
  int calls = 0;

  SECTION("status failure") {
    request.workload = [&calls]() -> std::int32_t {
      ++calls;
      return ztick_status_processing_failure;
    };

    const ztick::plugin::RunExecutionRecord record = runner.run(request);
    CHECK(calls == 1);
    CHECK_FALSE(record.succeeded());
    CHECK(record.status == ztick_status_processing_failure);
    CHECK(std::string(record.reason) == "in-process workload failed");
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_in_process);
  }

  SECTION("exception") {
    request.workload = [&calls]() -> std::int32_t {
      ++calls;
      throw 7;
    };

    const ztick::plugin::RunExecutionRecord record = runner.run(request);
    CHECK(calls == 1);
    CHECK_FALSE(record.succeeded());
    CHECK(record.status == ztick_status_internal_error);
    CHECK(std::string(record.reason) == "in-process workload threw an exception");
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_in_process);
  }
}
