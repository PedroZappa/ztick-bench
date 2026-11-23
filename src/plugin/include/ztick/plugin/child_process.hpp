// SPDX-License-Identifier: MIT
/**
 * @file child_process.hpp
 * @brief Portable child-process launcher and final-status transport boundary.
 *
 * Out-of-process execution launches a prepared child command, waits for it, and
 * reads one fixed IPC status message after the child workload has finished. Launch,
 * wait and transport therefore surround the workload and are never mixed into
 * per-block DSP timing (BR §6.3, ARCHITECTURE §4). A signal-terminated child becomes
 * a workload-failure result so the host can continue with independent work (BR §14).
 *
 * @note Process isolation reduces crash impact but is not a complete security
 *       sandbox (BR §6.3). This header is private to ztick-bench and is not
 *       installed. Platform calls stay behind @ref ztick::plugin::ChildProcessLauncher
 *       so runner policy remains portable and deterministic in unit tests
 *       (REQ-ARCH-02).
 */

#ifndef ZTICK_PLUGIN_CHILD_PROCESS_HPP
#define ZTICK_PLUGIN_CHILD_PROCESS_HPP

#include <ztick/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Host-side discovery, trust and execution support.
 * @note Child-process transport is implemented in P3.6; selection and the in-process
 *       runner are implemented in P3.5 (ARCHITECTURE §5).
 */
namespace ztick::plugin {

/**
 * @brief Outcome category of one child-process launch attempt.
 * @note Only @ref ChildProcessOutcome::completed carries a workload status; all other
 *       outcomes describe infrastructure or containment failures.
 */
enum class ChildProcessOutcome : std::uint8_t {
  /** @brief The child reported a valid final status through IPC. */
  completed = 0,
  /** @brief The child ended because of a signal; the host survived. */
  terminated = 1,
  /** @brief The child could not be started or reaped. */
  launch_failed = 2,
  /** @brief The child ran but did not report a valid IPC status message. */
  protocol_error = 3,
};

/**
 * @brief Command used to create one out-of-process workload.
 * @note The platform launcher appends its private status-file-descriptor argument;
 *       callers supply only the executable and application arguments. All strings
 *       must be NUL-free because they cross an `exec` boundary.
 */
struct ChildProcessSpec {
  /** @brief Path to the child executable; must be non-empty. */
  std::string executable;
  /** @brief Arguments passed to the child in order, excluding `argv[0]`. */
  std::vector<std::string> arguments;
};

/**
 * @brief Result of one child-process launch and final-status exchange.
 * @note @ref reason is a static, NUL-terminated string that outlives the result.
 *       @ref transport_bytes is diagnostic only and is never subtracted from timings.
 */
struct ChildProcessResult {
  /** @brief Category describing how the child attempt ended. */
  ChildProcessOutcome outcome{ChildProcessOutcome::launch_failed};
  /** @brief Final `ztick_status` code when @ref outcome is completed. */
  std::int32_t status{ztick_status_internal_error};
  /** @brief Static explanation on failure, or NULL on success. */
  const char *reason{"not launched"};
  /** @brief Number of IPC bytes received from the child; zero on no exchange. */
  std::size_t transport_bytes{0U};

  /**
   * @brief Report whether the child reported a valid final workload status.
   * @return True only when @ref outcome is `completed`.
   */
  [[nodiscard]] bool completed() const noexcept {
    return outcome == ChildProcessOutcome::completed;
  }
};

/**
 * @brief Validate one child command before any process is created.
 * @param spec Command to validate.
 * @return True when the executable is non-empty and no string contains an embedded NUL.
 * @note This check performs no filesystem access and launches nothing.
 */
auto child_process_spec_valid(const ChildProcessSpec &spec) noexcept -> bool;

/**
 * @brief Platform-specific child-process launcher.
 * @note Implementations create processes, reap them and read the IPC status message.
 *       They never load a plugin in the host process.
 */
class ChildProcessLauncher {
public:
  /** @brief Allow callers to hold launchers through this interface. */
  virtual ~ChildProcessLauncher() = default;

  /**
   * @brief Report whether this platform can launch child processes.
   * @return True when @ref launch can create and reap a child process.
   */
  virtual auto available() const noexcept -> bool = 0;

  /**
   * @brief Launch a child, wait for it, and read its final status message.
   * @param spec Command to execute.
   * @return Outcome, workload status when available, and transport diagnostics.
   * @note Implementations should avoid throwing, but callers must contain exceptions
   *       from injected launchers as infrastructure failures (BR §14).
   */
  virtual auto launch(const ChildProcessSpec &spec) const -> ChildProcessResult = 0;
};

/**
 * @brief Return the process launcher selected for this platform.
 * @return Process launcher with static storage duration.
 * @note POSIX systems provide a fork/exec implementation; other platforms provide an
 *       explicit unsupported fallback (BR §17).
 */
auto platform_child_process_launcher() noexcept -> const ChildProcessLauncher &;

/**
 * @brief Report whether the selected platform provides out-of-process execution.
 * @return True when @ref platform_child_process_launcher is usable.
 * @note Selection records this capability rather than silently omitting the mode.
 */
auto child_process_backend_available() noexcept -> bool;

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_CHILD_PROCESS_HPP
