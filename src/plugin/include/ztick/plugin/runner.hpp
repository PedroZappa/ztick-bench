// SPDX-License-Identifier: MIT
/**
 * @file runner.hpp
 * @brief Execution-mode selection and the in-process runner.
 *
 * Runner selection happens after discovery, negotiation and trust authorization. It
 * consumes metadata and decisions only: no module is loaded and no adapter callback
 * is called while choosing a mode. The selected mode is copied into a run-record
 * fragment when a workload is attempted, so each attempt states how it was executed
 * (BR §6.3, RESULTS_FORMAT §5). The in-process runner executes a supplied workload on the
 * caller's thread; out-of-process execution remains later Phase 3 work.
 *
 * @note This header is private to ztick-bench and is not installed. The public
 *       <ztick/types.h> execution-mode enum is reused so host records and the SDK
 *       C ABI agree on numeric values (SDK_SPECIFICATION §1.2).
 */

#ifndef ZTICK_PLUGIN_RUNNER_HPP
#define ZTICK_PLUGIN_RUNNER_HPP

#include <ztick/sdk.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

/**
 * @brief Host-side discovery, trust and execution support.
 * @note The in-process runner is implemented in P3.5; the out-of-process transport
 *       belongs to P3.6 (ARCHITECTURE §5).
 */
namespace ztick::plugin {

/**
 * @brief Isolation choice requested before runner selection.
 * @note `auto_select` lets the host choose from trust and platform availability;
 *       the explicit values override that preference but cannot bypass trust.
 */
enum class IsolationRequest : std::uint8_t {
  /** @brief Let the host select the mode from trust and availability. */
  auto_select = 0,
  /** @brief Require in-process execution. */
  in_process = 1,
  /** @brief Require out-of-process execution; availability is checked at selection. */
  out_of_process = 2,
};

/**
 * @brief Parse one portable `execution.isolation` configuration token.
 * @param token Token to parse; must be `auto`, `in_process` or `out_of_process`.
 * @return Matching request, or `std::nullopt` for an unknown token.
 * @note Parsing is case-sensitive and performs no plugin call (RESULTS_FORMAT §6).
 */
auto parse_isolation_request(std::string_view token) noexcept -> std::optional<IsolationRequest>;

/**
 * @brief Render one isolation request as its stable configuration token.
 * @param request Request to render.
 * @return `auto`, `in_process`, `out_of_process`, or `unknown` for an invalid value.
 */
std::string_view isolation_request_name(IsolationRequest request) noexcept;

/**
 * @brief Render one public C ABI execution mode as a stable token.
 * @param mode `ztick_execution_mode` value to render, passed as an integer so
 *        malformed C ABI values can be reported safely.
 * @return `in_process`, `out_of_process`, or `unknown` for an invalid value.
 */
std::string_view execution_mode_name(std::int32_t mode) noexcept;

/**
 * @brief Inputs to the mode-selection decision for one run.
 * @note Trust must already be authorized by @ref ztick::plugin::TrustManager; this
 *       structure only records that decision and does not repeat consent or storage
 *       work.
 */
struct RunnerSelectionInput {
  /** @brief Requested isolation mode from configuration or the front end. */
  IsolationRequest requested{IsolationRequest::auto_select};
  /** @brief True when trust authorization has already succeeded. */
  bool trust_authorized{false};
  /** @brief True when an out-of-process backend is available on this platform. */
  bool out_of_process_available{false};
};

/**
 * @brief Result of one runner-selection attempt.
 * @note Only @ref RunnerSelection::selectable permits a workload to be executed.
 *       Every refusal carries a stable reason and leaves no module loaded or mode
 *       recorded.
 */
struct RunnerSelection {
  /** @brief Original isolation request. */
  IsolationRequest requested{IsolationRequest::auto_select};
  /** @brief Mode selected for the run, or no value when selection was refused. */
  std::optional<ztick_execution_mode> mode{};
  /** @brief `ztick_status` code describing the selection. */
  std::int32_t status{ztick_status_internal_error};
  /** @brief Static explanation on refusal, or NULL on success. */
  const char *reason{"not selected"};

  /**
   * @brief Report whether a workload may be executed with this selection.
   * @return True only when @ref status is success and a mode was selected.
   */
  bool selectable() const noexcept { return status == ztick_status_success && mode.has_value(); }
};

/**
 * @brief Select the execution mode for one authorized integration.
 * @param input Trust, request and platform availability inputs.
 * @return Selection with the chosen public C ABI execution mode or an explicit reason.
 * @note Auto mode prefers in-process for trusted integrations. An explicit
 *       out-of-process request requires an available backend. Selection reads no
 *       adapter callback and grants no trust (BR §6.2, §6.3).
 */
RunnerSelection select_runner(const RunnerSelectionInput &input) noexcept;

/**
 * @brief Execution portion of a per-run record.
 * @note This is the host-side fragment later attached to the result document's
 *       `execution` section by the results layer (RESULTS_FORMAT §5). It preserves
 *       the selected mode when a workload was attempted, even if that workload fails.
 */
struct RunExecutionRecord {
  /** @brief Mode selected for this run, or no value when selection was refused. */
  std::optional<ztick_execution_mode> mode{};
  /** @brief Isolation value requested before selection. */
  IsolationRequest requested{IsolationRequest::auto_select};
  /** @brief `ztick_status` code describing the run attempt. */
  std::int32_t status{ztick_status_internal_error};
  /** @brief Static explanation on failure, or NULL on success. */
  const char *reason{"not recorded"};

  /**
   * @brief Report whether the recorded run attempt succeeded.
   * @return True only when @ref status is `ztick_status_success`.
   */
  // Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
  // cppcheck-suppress unusedFunction
  bool succeeded() const noexcept { return status == ztick_status_success; }
};

/**
 * @brief Create an execution record from a selection and a run outcome.
 * @param selection Selection whose requested and chosen modes are recorded.
 * @param status `ztick_status` code describing the run attempt.
 * @param reason Static explanation on failure, or NULL on success.
 * @return Record carrying the selected mode and the supplied outcome.
 */
RunExecutionRecord make_execution_record(const RunnerSelection &selection, std::int32_t status,
                                         const char *reason) noexcept;

/**
 * @brief One mode-independent workload invocation request.
 * @note The callable is supplied by the already authorized integration harness;
 *       runner selection never constructs or invokes it.
 */
struct RunRequest {
  /** @brief Selection that authorized this invocation. */
  RunnerSelection selection{};
  /** @brief Workload returning a `ztick_status` code; must not be empty. */
  std::function<std::int32_t()> workload{};
};

/**
 * @brief Interface implemented by each execution-mode runner.
 * @note Implementations must contain workload exceptions and return a record with a
 *       mode for attempts or no mode for refusals; they must not mark an incomplete
 *       run successful.
 */
class Runner {
public:
  /** @brief Allow callers to hold runners through this interface. */
  virtual ~Runner() = default;

  /**
   * @brief Execute one authorized workload.
   * @param request Selection and workload to run.
   * @return Execution record carrying the attempted mode and outcome.
   */
  virtual RunExecutionRecord run(const RunRequest &request) noexcept = 0;
};

/**
 * @brief Runner that executes a workload on the caller's thread.
 * @note This is the low-overhead mode for trusted integrations. It does not provide
 *       fault isolation and must not be described as a security sandbox (BR §6.3).
 */
class InProcessRunner final : public Runner {
public:
  /**
   * @brief Execute the supplied workload in process.
   * @param request Selection and workload to run.
   * @return Execution record with the in-process mode and the workload outcome.
   * @note No workload is called for an unselectable or out-of-process request, or
   *       when @ref RunRequest::workload is empty. A nonzero workload status is
   *       preserved; an exception becomes `ztick_status_internal_error`.
   */
  RunExecutionRecord run(const RunRequest &request) noexcept override;
};

/**
 * @brief Run one workload with a temporary in-process runner.
 * @param request Selection and workload to run.
 * @return Execution record with the in-process mode and the workload outcome.
 * @note Convenience wrapper for callers that do not retain runner state; it has the
 *       same refusal and exception behavior as @ref ztick::plugin::InProcessRunner.
 */
RunExecutionRecord run_in_process(const RunRequest &request) noexcept;

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_RUNNER_HPP
