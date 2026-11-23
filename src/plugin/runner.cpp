// SPDX-License-Identifier: MIT
/**
 * @file runner.cpp
 * @brief Implementation of runner selection and both execution modes.
 *
 * Selection is a policy decision over the requested isolation mode, prior trust
 * authorization and backend availability. It never loads a module, creates an
 * adapter instance, launches a child or calls adapter code. The runners record the
 * attempted mode on every exit path; out-of-process launch, wait and IPC surround
 * the child workload and stay outside its DSP timings (BR §6.3, ARCHITECTURE §4).
 */

#include <ztick/plugin/runner.hpp>

#include <optional>
#include <string_view>

namespace ztick::plugin {
namespace {

/**
 * @brief Build a failed selection with a stable reason.
 * @param request Original isolation request.
 * @param status `ztick_status` code describing the refusal.
 * @param reason Static, NUL-terminated explanation; must not be NULL.
 * @return Selection that cannot execute a workload.
 */
RunnerSelection refused(IsolationRequest request, std::int32_t status,
                        const char *reason) noexcept {
  return RunnerSelection{request, std::nullopt, status, reason};
}

/**
 * @brief Build a successful selection for one execution mode.
 * @param request Original isolation request.
 * @param mode Mode selected for the run.
 * @return Selection that permits a workload to be executed.
 */
RunnerSelection selected(IsolationRequest request, ztick_execution_mode mode) noexcept {
  return RunnerSelection{request, mode, ztick_status_success, nullptr};
}

} // namespace

// Consumed by in-tree tests and the future CLI config layer; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
auto parse_isolation_request(std::string_view token) noexcept -> std::optional<IsolationRequest> {
  if (token == "auto") {
    return IsolationRequest::auto_select;
  }
  if (token == "in_process") {
    return IsolationRequest::in_process;
  }
  if (token == "out_of_process") {
    return IsolationRequest::out_of_process;
  }
  return std::nullopt;
}

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
std::string_view isolation_request_name(IsolationRequest request) noexcept {
  switch (request) {
  case IsolationRequest::auto_select:
    return "auto";
  case IsolationRequest::in_process:
    return "in_process";
  case IsolationRequest::out_of_process:
    return "out_of_process";
  default:
    return "unknown";
  }
}

// Consumed by in-tree tests and the future results writer; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
std::string_view execution_mode_name(std::int32_t mode) noexcept {
  switch (mode) {
  case ztick_execution_mode_in_process:
    return "in_process";
  case ztick_execution_mode_out_of_process:
    return "out_of_process";
  default:
    return "unknown";
  }
}

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
RunnerSelection select_runner(const RunnerSelectionInput &input) noexcept {
  if (!input.trust_authorized) {
    return refused(input.requested, ztick_status_invalid_argument,
                   "plugin is not authorized for execution");
  }

  switch (input.requested) {
  case IsolationRequest::auto_select:
    return selected(input.requested, ztick_execution_mode_in_process);
  case IsolationRequest::in_process:
    return selected(input.requested, ztick_execution_mode_in_process);
  case IsolationRequest::out_of_process:
    if (!input.out_of_process_available) {
      return refused(input.requested, ztick_status_unsupported,
                     "out-of-process runner is not available");
    }
    return selected(input.requested, ztick_execution_mode_out_of_process);
  default:
    return refused(input.requested, ztick_status_invalid_argument, "unknown isolation request");
  }
}

RunExecutionRecord make_execution_record(const RunnerSelection &selection, std::int32_t status,
                                         const char *reason) noexcept {
  return RunExecutionRecord{selection.mode, selection.requested, status, reason};
}

RunExecutionRecord InProcessRunner::run(const RunRequest &request) noexcept {
  if (!request.selection.selectable()) {
    return make_execution_record(request.selection, request.selection.status,
                                 request.selection.reason);
  }
  if (*request.selection.mode != ztick_execution_mode_in_process) {
    return make_execution_record(request.selection, ztick_status_unsupported,
                                 "in-process runner cannot execute out-of-process mode");
  }
  if (!request.workload) {
    return make_execution_record(request.selection, ztick_status_invalid_argument,
                                 "no in-process workload supplied");
  }

  try {
    const std::int32_t status = request.workload();
    const char *reason = status == ztick_status_success ? nullptr : "in-process workload failed";
    return make_execution_record(request.selection, status, reason);
  } catch (...) {
    return make_execution_record(request.selection, ztick_status_internal_error,
                                 "in-process workload threw an exception");
  }
}

OutOfProcessRunner::OutOfProcessRunner(const ChildProcessLauncher &launcher) noexcept
    : launcher_(&launcher) {}

RunExecutionRecord OutOfProcessRunner::run(const RunRequest &request) noexcept {
  if (!request.selection.selectable()) {
    return make_execution_record(request.selection, request.selection.status,
                                 request.selection.reason);
  }
  if (*request.selection.mode != ztick_execution_mode_out_of_process) {
    return make_execution_record(request.selection, ztick_status_unsupported,
                                 "out-of-process runner requires an out-of-process selection");
  }
  if (launcher_ == nullptr || !launcher_->available()) {
    return make_execution_record(request.selection, ztick_status_unsupported,
                                 "out-of-process runner is not available");
  }
  if (!request.child_process.has_value()) {
    return make_execution_record(request.selection, ztick_status_invalid_argument,
                                 "no out-of-process workload supplied");
  }
  if (!child_process_spec_valid(*request.child_process)) {
    return make_execution_record(request.selection, ztick_status_invalid_argument,
                                 "invalid out-of-process workload");
  }

  ChildProcessResult result;
  try {
    result = launcher_->launch(*request.child_process);
  } catch (...) {
    return make_execution_record(request.selection, ztick_status_internal_error,
                                 "out-of-process launcher threw an exception");
  }

  if (result.completed()) {
    const char *reason =
        result.status == ztick_status_success ? nullptr : "out-of-process workload failed";
    return make_execution_record(request.selection, result.status, reason);
  }

  switch (result.outcome) {
  case ChildProcessOutcome::terminated:
    return make_execution_record(request.selection, ztick_status_processing_failure,
                                 "out-of-process workload terminated by signal");
  case ChildProcessOutcome::launch_failed:
    return make_execution_record(
        request.selection, ztick_status_internal_error,
        result.reason != nullptr ? result.reason : "out-of-process workload could not be launched");
  case ChildProcessOutcome::protocol_error:
    return make_execution_record(request.selection, ztick_status_internal_error,
                                 "out-of-process IPC protocol failure");
  default:
    return make_execution_record(request.selection, ztick_status_internal_error,
                                 "unknown out-of-process result");
  }
}

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
RunExecutionRecord run_in_process(const RunRequest &request) noexcept {
  InProcessRunner runner;
  return runner.run(request);
}

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
RunExecutionRecord run_out_of_process(const RunRequest &request,
                                      const ChildProcessLauncher &launcher) noexcept {
  OutOfProcessRunner runner(launcher);
  return runner.run(request);
}

} // namespace ztick::plugin
