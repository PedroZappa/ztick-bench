// SPDX-License-Identifier: MIT
/**
 * @file child_process.cpp
 * @brief POSIX implementation of child-process launching and final-status IPC.
 *
 * The launcher creates a pipe, forks, and executes the supplied command with the
 * pipe write end installed as descriptor 3. After the child workload finishes it
 * writes one fixed status message; the parent reads it and reaps the child. Signal
 * termination is reported as @ref ztick::plugin::ChildProcessOutcome::terminated so
 * a crashed workload never terminates the host (BR §14).
 *
 * @note All platform-specific calls stay in this file behind
 *       @ref ztick::plugin::ChildProcessLauncher (REQ-ARCH-02).
 */

#include <ztick/plugin/child_process.hpp>
#include <ztick/plugin/ipc_protocol.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ztick::plugin::posix {
namespace {

/** @brief Descriptor used by the child for its final-status message. */
constexpr int kStatusFd = 3;
/** @brief Argument appended by the launcher to publish the status descriptor. */
constexpr const char *kStatusFdArgument = "--ztick-status-fd=3";
/** @brief Child exit code reserved for failures before or during `exec`. */
constexpr int kExecFailureExitCode = 127;

/**
 * @brief Mark one descriptor close-on-exec so only the intended child fd survives.
 * @param fd Descriptor to modify.
 * @return True when the descriptor flags were read and updated successfully.
 */
auto set_close_on_exec(int fd) noexcept -> bool {
  const int flags = ::fcntl(fd, F_GETFD);
  if (flags < 0) {
    return false;
  }
  return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

/**
 * @brief POSIX child-process launcher using `fork`, `execv` and `waitpid`.
 */
class PosixChildProcessLauncher final : public ChildProcessLauncher {
public:
  /** @brief Report that POSIX process launching is available. */
  auto available() const noexcept -> bool override { return true; }

  /**
   * @brief Launch one child command and collect its final status message.
   * @param spec Command to execute.
   * @return Outcome, workload status when available, and received byte count.
   * @note The launcher appends @ref kStatusFdArgument before `exec`. A child that
   *       terminates by signal is reported without trusting any partial IPC bytes.
   */
  auto launch(const ChildProcessSpec &spec) const -> ChildProcessResult override {
    if (!child_process_spec_valid(spec)) {
      return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_invalid_argument,
                                "invalid child process specification", 0U};
    }

    std::vector<std::string> argv_storage;
    argv_storage.reserve(spec.arguments.size() + 2U);
    argv_storage.push_back(spec.executable);
    std::copy(spec.arguments.begin(), spec.arguments.end(), std::back_inserter(argv_storage));
    argv_storage.emplace_back(kStatusFdArgument);

    std::vector<char *> argv;
    argv.reserve(argv_storage.size() + 1U);
    std::transform(argv_storage.begin(), argv_storage.end(), std::back_inserter(argv),
                   [](std::string &entry) noexcept { return entry.data(); });
    argv.push_back(nullptr);

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
      return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_internal_error,
                                "could not create out-of-process IPC pipe", 0U};
    }
    if (!set_close_on_exec(pipe_fds[0]) || !set_close_on_exec(pipe_fds[1])) {
      (void)::close(pipe_fds[0]);
      (void)::close(pipe_fds[1]);
      return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_internal_error,
                                "could not configure out-of-process IPC pipe", 0U};
    }

    const pid_t child = ::fork();
    if (child < 0) {
      (void)::close(pipe_fds[0]);
      (void)::close(pipe_fds[1]);
      return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_internal_error,
                                "could not fork child process", 0U};
    }

    if (child == 0) {
      (void)::close(pipe_fds[0]);
      if (::dup2(pipe_fds[1], kStatusFd) < 0) {
        ::_exit(kExecFailureExitCode);
      }
      if (pipe_fds[1] != kStatusFd) {
        (void)::close(pipe_fds[1]);
      }
      const int status_fd_flags = ::fcntl(kStatusFd, F_GETFD);
      if (status_fd_flags >= 0) {
        (void)::fcntl(kStatusFd, F_SETFD, status_fd_flags & ~FD_CLOEXEC);
      }
      (void)::execv(argv_storage.front().c_str(), argv.data());
      ::_exit(kExecFailureExitCode);
    }

    (void)::close(pipe_fds[1]);

    std::array<std::uint8_t, ipc::kStatusMessageSize> message{};
    std::size_t received = 0U;
    while (received < message.size()) {
      const ssize_t count =
          ::read(pipe_fds[0], message.data() + received, message.size() - received);
      if (count > 0) {
        received += static_cast<std::size_t>(count);
        continue;
      }
      if (count == 0) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    (void)::close(pipe_fds[0]);

    int wait_status = 0;
    pid_t waited = -1;
    do {
      waited = ::waitpid(child, &wait_status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
      return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_internal_error,
                                "could not reap child process", received};
    }
    if (WIFSIGNALED(wait_status)) {
      return ChildProcessResult{ChildProcessOutcome::terminated, ztick_status_processing_failure,
                                "child terminated by signal", received};
    }
    if (!WIFEXITED(wait_status)) {
      return ChildProcessResult{ChildProcessOutcome::protocol_error, ztick_status_internal_error,
                                "child did not exit normally", received};
    }

    std::int32_t workload_status = ztick_status_internal_error;
    if (ipc::parse_status_message(message.data(), received, workload_status)) {
      return ChildProcessResult{ChildProcessOutcome::completed, workload_status, nullptr, received};
    }
    if (WEXITSTATUS(wait_status) == kExecFailureExitCode) {
      return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_internal_error,
                                "child executable could not be started", received};
    }
    return ChildProcessResult{ChildProcessOutcome::protocol_error, ztick_status_internal_error,
                              "child did not report a valid status", received};
  }
};

} // namespace

/**
 * @brief Return the POSIX child-process launcher.
 * @return Process launcher with static storage duration.
 */
auto platform_child_process_launcher() noexcept -> const ChildProcessLauncher & {
  static const PosixChildProcessLauncher launcher;
  return launcher;
}

/**
 * @brief Report that POSIX process launching is available.
 * @return Always true.
 */
auto child_process_backend_available() noexcept -> bool {
  return platform_child_process_launcher().available();
}

} // namespace ztick::plugin::posix

namespace ztick::plugin {

/**
 * @brief Return the process launcher selected for this platform.
 * @return POSIX launcher with static storage duration.
 */
auto platform_child_process_launcher() noexcept -> const ChildProcessLauncher & {
  return posix::platform_child_process_launcher();
}

/**
 * @brief Report that the POSIX child-process backend is available.
 * @return Always true in this implementation.
 */
auto child_process_backend_available() noexcept -> bool {
  return posix::child_process_backend_available();
}

} // namespace ztick::plugin
