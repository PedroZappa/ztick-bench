// SPDX-License-Identifier: MIT
/**
 * @file ipc_protocol.hpp
 * @brief Fixed final-status message exchanged with an out-of-process workload.
 *
 * The protocol carries exactly one workload status after the child has finished its
 * timed work. It carries no per-block samples, so process management and IPC stay
 * outside the measured DSP interval (BR §6.3, ARCHITECTURE §4). The wire layout is
 * explicit and little-endian so the child and host do not depend on a shared C++ ABI.
 *
 * @note This header is private to ztick-bench and is not installed. It defines an
 *       internal transport format, not a public SDK interface.
 */

#ifndef ZTICK_PLUGIN_IPC_PROTOCOL_HPP
#define ZTICK_PLUGIN_IPC_PROTOCOL_HPP

#include <array>
#include <cstddef>
#include <cstdint>

/**
 * @brief Fixed-size status message used by the out-of-process runner.
 */
namespace ztick::plugin::ipc {

/** @brief Number of bytes in one final-status message. */
inline constexpr std::size_t kStatusMessageSize = 12U;

/**
 * @brief Owning byte buffer for one final-status message.
 * @note Layout: bytes 0–3 magic `ZTIC`, byte 4 version, byte 5 reserved,
 *       bytes 6–7 little-endian payload size (4), bytes 8–11 little-endian status.
 */
using StatusMessage = std::array<std::uint8_t, kStatusMessageSize>;

/**
 * @brief Encode one workload status into the fixed IPC layout.
 * @param status `ztick_status` code reported by the child workload.
 * @return Message ready to write to the launcher's status file descriptor.
 */
auto make_status_message(std::int32_t status) noexcept -> StatusMessage;

/**
 * @brief Decode one received final-status message.
 * @param data Start of the received bytes; may be NULL only when @p size is zero.
 * @param size Number of bytes received.
 * @param status Receives the decoded `ztick_status` code only on success.
 * @return True when the bytes are a complete, well-formed version-1 message.
 * @note Leaves @p status unchanged on failure. Exactly @ref kStatusMessageSize bytes
 *       are required; partial or extra data is rejected.
 */
auto parse_status_message(const std::uint8_t *data, std::size_t size, std::int32_t &status) noexcept
    -> bool;

} // namespace ztick::plugin::ipc

#endif // ZTICK_PLUGIN_IPC_PROTOCOL_HPP
