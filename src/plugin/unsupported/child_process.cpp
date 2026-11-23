// SPDX-License-Identifier: MIT
/**
 * @file child_process.cpp
 * @brief Non-POSIX fallback for the plugin host's child-process abstraction.
 *
 * Child processes are platform work deferred beyond v1.0 outside POSIX systems (BR §1,
 * §16). The fallback reports an explicit unsupported state through
 * @ref ztick::plugin::ChildProcessResult rather than pretending the mode is absent.
 */

#include <ztick/plugin/child_process.hpp>

namespace ztick::plugin::unsupported {
namespace {

/**
 * @brief Launcher that cannot create child processes on this platform.
 */
class UnsupportedChildProcessLauncher final : public ChildProcessLauncher {
public:
  /** @brief Report that no child-process backend is available. */
  auto available() const noexcept -> bool override { return false; }

  /**
   * @brief Refuse to launch a child process.
   * @param spec Command that would have been launched; unused.
   * @return Unsupported launch failure with a static explanation.
   */
  auto launch(const ChildProcessSpec &spec) const -> ChildProcessResult override {
    (void)spec;
    return ChildProcessResult{ChildProcessOutcome::launch_failed, ztick_status_unsupported,
                              "child processes are not supported on this platform", 0U};
  }
};

} // namespace

/**
 * @brief Return the unsupported fallback launcher.
 * @return Launcher with static storage duration.
 */
auto platform_child_process_launcher() noexcept -> const ChildProcessLauncher & {
  static const UnsupportedChildProcessLauncher launcher;
  return launcher;
}

} // namespace ztick::plugin::unsupported

namespace ztick::plugin {

/**
 * @brief Return the unsupported fallback launcher.
 * @return Fallback launcher with static storage duration.
 */
auto platform_child_process_launcher() noexcept -> const ChildProcessLauncher & {
  return unsupported::platform_child_process_launcher();
}

/**
 * @brief Report that no child-process backend exists in this build.
 * @return Always false.
 */
auto child_process_backend_available() noexcept -> bool { return false; }

auto write_child_status(std::int32_t status) noexcept -> bool {
  (void)status;
  return false;
}

} // namespace ztick::plugin
