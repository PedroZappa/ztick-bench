// SPDX-License-Identifier: MIT
/**
 * @file out_of_process_runner_test.cpp
 * @brief Deterministic and POSIX-backed tests for out-of-process execution.
 *
 * Injected launchers cover every status mapping without creating processes. On POSIX
 * systems a test helper is launched for real so a signal-terminated child proves the
 * host survives and can run another workload (BR §14, BR §6.3). Expected IPC bytes,
 * statuses and reasons are independent literals.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/child_process.hpp>
#include <ztick/plugin/ipc_protocol.hpp>
#include <ztick/plugin/runner.hpp>

#include <ztick/sdk.h>
#include <ztick/types.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

/**
 * @brief Build an authorized input for one isolation request.
 * @param requested Isolation request to use.
 * @return Input with trust and the out-of-process backend available.
 */
auto authorized_input(ztick::plugin::IsolationRequest requested)
    -> ztick::plugin::RunnerSelectionInput {
  ztick::plugin::RunnerSelectionInput input;
  input.requested = requested;
  input.trust_authorized = true;
  input.out_of_process_available = true;
  return input;
}

/**
 * @brief Build a valid out-of-process selection.
 * @return Selection with the out-of-process mode chosen.
 */
auto out_of_process_selection() -> ztick::plugin::RunnerSelection {
  return ztick::plugin::select_runner(
      authorized_input(ztick::plugin::IsolationRequest::out_of_process));
}

/**
 * @brief Build a request carrying one child command.
 * @param selection Selection that authorizes the request.
 * @param executable Child executable path.
 * @param mode Helper scenario passed as the first child argument.
 * @return Request with a valid single-mode child command.
 */
auto child_request(const ztick::plugin::RunnerSelection &selection, std::string executable,
                   std::string mode = "success") -> ztick::plugin::RunRequest {
  ztick::plugin::RunRequest request;
  request.selection = selection;
  request.child_process = ztick::plugin::ChildProcessSpec{std::move(executable), {std::move(mode)}};
  return request;
}

/**
 * @brief Build a completed launcher result for one workload status.
 * @param status `ztick_status` code reported by the child.
 * @return Completed result carrying exactly one IPC message worth of bytes.
 */
auto completed_result(std::int32_t status) -> ztick::plugin::ChildProcessResult {
  return ztick::plugin::ChildProcessResult{ztick::plugin::ChildProcessOutcome::completed, status,
                                           nullptr, ztick::plugin::ipc::kStatusMessageSize};
}

/**
 * @brief Launcher returning a configurable result without creating a process.
 */
class FakeLauncher final : public ztick::plugin::ChildProcessLauncher {
public:
  /** @brief Result returned from the next launch. */
  ztick::plugin::ChildProcessResult result{completed_result(ztick_status_success)};
  /** @brief Availability reported by @ref available. */
  bool available_result{true};
  /** @brief When true, @ref launch throws to test infrastructure containment. */
  bool throw_on_launch{false};
  /** @brief Number of launch calls observed; mutable for the const launcher API. */
  mutable int calls{0};
  /** @brief Executable path from the most recent launch; mutable for the const API. */
  mutable std::string last_executable;

  /** @brief Report the configured availability. */
  auto available() const noexcept -> bool override { return available_result; }

  /**
   * @brief Record the request and return the configured result.
   * @param spec Command that would have been launched.
   * @return Configured result, or nothing when @ref throw_on_launch is set.
   */
  auto launch(const ztick::plugin::ChildProcessSpec &spec) const
      -> ztick::plugin::ChildProcessResult override {
    ++calls;
    last_executable = spec.executable;
    if (throw_on_launch) {
      throw 7;
    }
    return result;
  }
};

} // namespace

/**
 * @brief Verify the IPC status layout is explicit and rejects malformed data.
 */
TEST_CASE("IPC status messages encode and decode independently", "[plugin][runner][ipc]") {
  using ztick::plugin::ipc::kStatusMessageSize;
  using ztick::plugin::ipc::make_status_message;
  using ztick::plugin::ipc::parse_status_message;

  const auto success = make_status_message(ztick_status_success);
  const std::array<std::uint8_t, kStatusMessageSize> expected_success{
      0x5aU, 0x54U, 0x49U, 0x43U, 1U, 0U, 4U, 0U, 0U, 0U, 0U, 0U};
  CHECK(success == expected_success);

  const auto negative = make_status_message(-7);
  const std::array<std::uint8_t, kStatusMessageSize> expected_negative{
      0x5aU, 0x54U, 0x49U, 0x43U, 1U, 0U, 4U, 0U, 0xf9U, 0xffU, 0xffU, 0xffU};
  CHECK(negative == expected_negative);

  std::int32_t decoded = 42;
  REQUIRE(parse_status_message(success.data(), success.size(), decoded));
  CHECK(decoded == ztick_status_success);
  REQUIRE(parse_status_message(negative.data(), negative.size(), decoded));
  CHECK(decoded == -7);

  auto bad_magic = success;
  bad_magic[0U] = 0U;
  CHECK_FALSE(parse_status_message(bad_magic.data(), bad_magic.size(), decoded));
  auto bad_version = success;
  bad_version[4U] = 2U;
  CHECK_FALSE(parse_status_message(bad_version.data(), bad_version.size(), decoded));
  auto bad_reserved = success;
  bad_reserved[5U] = 1U;
  CHECK_FALSE(parse_status_message(bad_reserved.data(), bad_reserved.size(), decoded));
  auto bad_size = success;
  bad_size[6U] = 3U;
  CHECK_FALSE(parse_status_message(bad_size.data(), bad_size.size(), decoded));

  decoded = 42;
  CHECK_FALSE(parse_status_message(success.data(), success.size() - 1U, decoded));
  CHECK_FALSE(parse_status_message(nullptr, success.size(), decoded));
  CHECK(decoded == 42);
}

/**
 * @brief Verify child commands are rejected before a process is created.
 */
TEST_CASE("child process specifications are validated", "[plugin][runner][process]") {
  ztick::plugin::ChildProcessSpec spec{"/usr/bin/true", {"--flag"}};
  CHECK(ztick::plugin::child_process_spec_valid(spec));

  spec.executable.clear();
  CHECK_FALSE(ztick::plugin::child_process_spec_valid(spec));

  spec.executable = std::string("bad\0path", 8U);
  CHECK_FALSE(ztick::plugin::child_process_spec_valid(spec));

  spec.executable = "/usr/bin/true";
  spec.arguments.push_back(std::string("bad\0arg", 7U));
  CHECK_FALSE(ztick::plugin::child_process_spec_valid(spec));
}

/**
 * @brief Verify valid child statuses are mapped to run records.
 */
TEST_CASE("out-of-process runner records child statuses", "[plugin][runner][process]") {
  const ztick::plugin::RunnerSelection selection = out_of_process_selection();
  REQUIRE(selection.selectable());

  SECTION("successful child") {
    FakeLauncher launcher;
    launcher.result = completed_result(ztick_status_success);
    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child-success"), launcher);

    CHECK(launcher.calls == 1);
    CHECK(launcher.last_executable == "child-success");
    CHECK(record.succeeded());
    CHECK(record.status == ztick_status_success);
    CHECK(record.reason == nullptr);
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_out_of_process);
    CHECK(record.requested == ztick::plugin::IsolationRequest::out_of_process);
  }

  SECTION("failed child") {
    FakeLauncher launcher;
    launcher.result = completed_result(ztick_status_processing_failure);
    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child-failure"), launcher);

    CHECK(launcher.calls == 1);
    CHECK_FALSE(record.succeeded());
    CHECK(record.status == ztick_status_processing_failure);
    CHECK(std::string(record.reason) == "out-of-process workload failed");
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_out_of_process);
  }
}

/**
 * @brief Verify child termination is contained and reported as a workload failure.
 */
TEST_CASE("out-of-process runner contains a terminated child", "[plugin][runner][process]") {
  FakeLauncher launcher;
  launcher.result = ztick::plugin::ChildProcessResult{
      ztick::plugin::ChildProcessOutcome::terminated, ztick_status_processing_failure,
      "child terminated by signal", 0U};

  const ztick::plugin::RunExecutionRecord record = ztick::plugin::run_out_of_process(
      child_request(out_of_process_selection(), "child-crash"), launcher);

  CHECK(launcher.calls == 1);
  CHECK_FALSE(record.succeeded());
  CHECK(record.status == ztick_status_processing_failure);
  CHECK(std::string(record.reason) == "out-of-process workload terminated by signal");
  REQUIRE(record.mode.has_value());
  CHECK(*record.mode == ztick_execution_mode_out_of_process);
}

/**
 * @brief Verify launcher and IPC failures are classified as infrastructure failures.
 */
TEST_CASE("out-of-process runner records infrastructure failures", "[plugin][runner][process]") {
  const ztick::plugin::RunnerSelection selection = out_of_process_selection();
  REQUIRE(selection.selectable());

  SECTION("launch failure") {
    FakeLauncher launcher;
    launcher.result = ztick::plugin::ChildProcessResult{
        ztick::plugin::ChildProcessOutcome::launch_failed, ztick_status_internal_error,
        "injected launch failure", 0U};

    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child"), launcher);

    CHECK(record.status == ztick_status_internal_error);
    CHECK(std::string(record.reason) == "injected launch failure");
  }

  SECTION("protocol failure") {
    FakeLauncher launcher;
    launcher.result = ztick::plugin::ChildProcessResult{
        ztick::plugin::ChildProcessOutcome::protocol_error, ztick_status_internal_error,
        "child did not report a valid status", 4U};

    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child"), launcher);

    CHECK(record.status == ztick_status_internal_error);
    CHECK(std::string(record.reason) == "out-of-process IPC protocol failure");
  }

  SECTION("launcher exception") {
    FakeLauncher launcher;
    launcher.throw_on_launch = true;

    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child"), launcher);

    CHECK(record.status == ztick_status_internal_error);
    CHECK(std::string(record.reason) == "out-of-process launcher threw an exception");
  }

  SECTION("unavailable backend") {
    FakeLauncher launcher;
    launcher.available_result = false;

    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child"), launcher);

    CHECK(launcher.calls == 0);
    CHECK(record.status == ztick_status_unsupported);
    CHECK(std::string(record.reason) == "out-of-process runner is not available");
  }
}

/**
 * @brief Verify invalid requests never reach a child process.
 */
TEST_CASE("out-of-process runner refuses invalid requests", "[plugin][runner][process]") {
  FakeLauncher launcher;

  SECTION("untrusted selection") {
    ztick::plugin::RunnerSelectionInput input;
    input.requested = ztick::plugin::IsolationRequest::out_of_process;
    input.out_of_process_available = true;
    const ztick::plugin::RunExecutionRecord record = ztick::plugin::run_out_of_process(
        child_request(ztick::plugin::select_runner(input), "child"), launcher);

    CHECK(launcher.calls == 0);
    CHECK(record.status == ztick_status_invalid_argument);
    CHECK(std::string(record.reason) == "plugin is not authorized for execution");
    CHECK_FALSE(record.mode.has_value());
  }

  SECTION("in-process selection") {
    const ztick::plugin::RunnerSelection selection =
        ztick::plugin::select_runner(authorized_input(ztick::plugin::IsolationRequest::in_process));
    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(child_request(selection, "child"), launcher);

    CHECK(launcher.calls == 0);
    CHECK(record.status == ztick_status_unsupported);
    CHECK(std::string(record.reason) ==
          "out-of-process runner requires an out-of-process selection");
    REQUIRE(record.mode.has_value());
    CHECK(*record.mode == ztick_execution_mode_in_process);
  }

  SECTION("missing child command") {
    ztick::plugin::RunRequest request;
    request.selection = out_of_process_selection();
    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(request, launcher);

    CHECK(launcher.calls == 0);
    CHECK(record.status == ztick_status_invalid_argument);
    CHECK(std::string(record.reason) == "no out-of-process workload supplied");
  }

  SECTION("invalid child command") {
    ztick::plugin::RunRequest request = child_request(out_of_process_selection(), "child");
    request.child_process->executable.clear();
    const ztick::plugin::RunExecutionRecord record =
        ztick::plugin::run_out_of_process(request, launcher);

    CHECK(launcher.calls == 0);
    CHECK(record.status == ztick_status_invalid_argument);
    CHECK(std::string(record.reason) == "invalid out-of-process workload");
  }
}

/**
 * @brief Verify the runner interface dispatches to the out-of-process implementation.
 */
TEST_CASE("out-of-process runner satisfies the runner interface", "[plugin][runner][process]") {
  FakeLauncher launcher;
  ztick::plugin::OutOfProcessRunner runner(launcher);
  ztick::plugin::Runner &base = runner;

  const ztick::plugin::RunExecutionRecord record =
      base.run(child_request(out_of_process_selection(), "child-interface"));

  CHECK(launcher.calls == 1);
  CHECK(launcher.last_executable == "child-interface");
  CHECK(record.succeeded());
  REQUIRE(record.mode.has_value());
  CHECK(*record.mode == ztick_execution_mode_out_of_process);
}

#if defined(ZTICK_TEST_POSIX_CHILD_PROCESS) && defined(ZTICK_TEST_CHILD_HELPER_PATH)
/**
 * @brief Verify a real signalling child is contained and the host keeps running.
 */
TEST_CASE("POSIX child crash is contained by the host", "[plugin][runner][process]") {
  using ztick::plugin::ChildProcessOutcome;
  using ztick::plugin::ChildProcessSpec;

  CHECK(ztick::plugin::child_process_backend_available());
  const ztick::plugin::RunnerSelection selection = out_of_process_selection();
  REQUIRE(selection.selectable());

  const ChildProcessSpec success_spec{ZTICK_TEST_CHILD_HELPER_PATH, {"success"}};
  const ztick::plugin::ChildProcessResult direct =
      ztick::plugin::platform_child_process_launcher().launch(success_spec);
  CHECK(direct.outcome == ChildProcessOutcome::completed);
  CHECK(direct.status == ztick_status_success);
  CHECK(direct.transport_bytes == ztick::plugin::ipc::kStatusMessageSize);

  const ztick::plugin::ChildProcessResult malformed =
      ztick::plugin::platform_child_process_launcher().launch(
          ChildProcessSpec{ZTICK_TEST_CHILD_HELPER_PATH, {"bad-payload"}});
  CHECK(malformed.outcome == ChildProcessOutcome::protocol_error);
  CHECK(malformed.transport_bytes == 4U);

  const ztick::plugin::RunExecutionRecord first =
      ztick::plugin::run_out_of_process(child_request(selection, ZTICK_TEST_CHILD_HELPER_PATH));
  REQUIRE(first.succeeded());

  const ztick::plugin::RunExecutionRecord failure = ztick::plugin::run_out_of_process(
      child_request(selection, ZTICK_TEST_CHILD_HELPER_PATH, "failure"));
  CHECK_FALSE(failure.succeeded());
  CHECK(failure.status == ztick_status_processing_failure);
  CHECK(std::string(failure.reason) == "out-of-process workload failed");

  const ztick::plugin::RunExecutionRecord crash = ztick::plugin::run_out_of_process(
      child_request(selection, ZTICK_TEST_CHILD_HELPER_PATH, "crash"));
  CHECK_FALSE(crash.succeeded());
  CHECK(crash.status == ztick_status_processing_failure);
  CHECK(std::string(crash.reason) == "out-of-process workload terminated by signal");

  const ztick::plugin::RunExecutionRecord after =
      ztick::plugin::run_out_of_process(child_request(selection, ZTICK_TEST_CHILD_HELPER_PATH));
  CHECK(after.succeeded());
}
#else
/**
 * @brief Verify the fallback reports its missing child-process capability explicitly.
 */
TEST_CASE("child process fallback is explicit", "[plugin][runner][process]") {
  CHECK_FALSE(ztick::plugin::child_process_backend_available());
  const ztick::plugin::ChildProcessResult result =
      ztick::plugin::platform_child_process_launcher().launch(
          ztick::plugin::ChildProcessSpec{"/bin/true", {}});

  CHECK(result.outcome == ztick::plugin::ChildProcessOutcome::launch_failed);
  CHECK(result.status == ztick_status_unsupported);
  CHECK(std::string(result.reason) == "child processes are not supported on this platform");
}
#endif
