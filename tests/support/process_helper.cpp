// SPDX-License-Identifier: MIT
/**
 * @file process_helper.cpp
 * @brief Test child executable for out-of-process runner and IPC verification.
 *
 * The helper reports success or failure through the fixed IPC message, or terminates
 * by signal to prove that the host survives a crashed child. A malformed-payload mode
 * exercises the host's protocol validation. The binary is built only for tests and is
 * never installed.
 */

#include <ztick/plugin/ipc_protocol.hpp>

#include <ztick/types.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

/** @brief Descriptor selected by the POSIX launcher for status output. */
constexpr int kStatusFd = 3;
/** @brief Argument used by the POSIX launcher to publish the status descriptor. */
constexpr const char *kStatusFdArgument = "--ztick-status-fd=3";

/**
 * @brief Write every byte of one buffer to a descriptor.
 * @param fd Descriptor to write.
 * @param data Start of the byte buffer.
 * @param size Number of bytes to write.
 * @return True when all bytes were written.
 */
auto write_all(int fd, const std::uint8_t *data, std::size_t size) noexcept -> bool {
  std::size_t written = 0U;
  while (written < size) {
    const ssize_t count = ::write(fd, data + written, size - written);
    if (count > 0) {
      written += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

/**
 * @brief Encode and write one final workload status.
 * @param fd Status descriptor supplied by the launcher.
 * @param status `ztick_status` code to report.
 * @return True when the complete message was written.
 */
auto write_status(int fd, std::int32_t status) noexcept -> bool {
  const ztick::plugin::ipc::StatusMessage message = ztick::plugin::ipc::make_status_message(status);
  return write_all(fd, message.data(), message.size());
}

} // namespace

/**
 * @brief Run one helper scenario selected by the first command-line argument.
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments; `argv[1]` selects the scenario.
 * @return Zero for an expected scenario outcome, or a distinct nonzero setup error.
 */
int main(int argc, char **argv) {
  if (argc < 2) {
    return 2;
  }

  const std::string mode(argv[1]);
  if (mode == "self-check") {
    const ztick::plugin::ipc::StatusMessage message =
        ztick::plugin::ipc::make_status_message(ztick_status_success);
    std::int32_t decoded = ztick_status_internal_error;
    const bool valid =
        ztick::plugin::ipc::parse_status_message(message.data(), message.size(), decoded);
    return valid && decoded == ztick_status_success ? 0 : 1;
  }

  int status_fd = -1;
  for (int index = 2; index < argc; ++index) {
    if (std::strcmp(argv[index], kStatusFdArgument) == 0) {
      status_fd = kStatusFd;
    }
  }
  if (status_fd < 0) {
    return 2;
  }

  if (mode == "success") {
    return write_status(status_fd, ztick_status_success) ? 0 : 4;
  }
  if (mode == "failure") {
    return write_status(status_fd, ztick_status_processing_failure) ? 0 : 4;
  }
  if (mode == "bad-payload") {
    const std::uint8_t partial[4U] = {0x5aU, 0x54U, 0x49U, 0x43U};
    return write_all(status_fd, partial, sizeof(partial)) ? 0 : 4;
  }
  if (mode == "silent") {
    return 0;
  }
  if (mode == "crash") {
    (void)std::raise(SIGTERM);
    std::_Exit(99);
  }
  return 3;
}
