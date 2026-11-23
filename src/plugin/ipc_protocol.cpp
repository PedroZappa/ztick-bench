// SPDX-License-Identifier: MIT
/**
 * @file ipc_protocol.cpp
 * @brief Encoding and validation for the fixed out-of-process status message.
 */

#include <ztick/plugin/ipc_protocol.hpp>

#include <bit>

namespace ztick::plugin::ipc {
namespace {

/** @brief First magic byte, `Z`, of the status message. */
constexpr std::uint8_t kMagic0 = 0x5aU;
/** @brief Second magic byte, `T`, of the status message. */
constexpr std::uint8_t kMagic1 = 0x54U;
/** @brief Third magic byte, `I`, of the status message. */
constexpr std::uint8_t kMagic2 = 0x49U;
/** @brief Fourth magic byte, `C`, of the status message. */
constexpr std::uint8_t kMagic3 = 0x43U;
/** @brief Protocol version carried in byte 4. */
constexpr std::uint8_t kVersion = 1U;
/** @brief Required reserved value in byte 5. */
constexpr std::uint8_t kReserved = 0U;
/** @brief Payload size in bytes, carried little-endian in bytes 6–7. */
constexpr std::uint16_t kPayloadSize = 4U;
/** @brief Offset of the little-endian status field. */
constexpr std::size_t kStatusOffset = 8U;

} // namespace

auto make_status_message(std::int32_t status) noexcept -> StatusMessage {
  StatusMessage message{};
  message[0U] = kMagic0;
  message[1U] = kMagic1;
  message[2U] = kMagic2;
  message[3U] = kMagic3;
  message[4U] = kVersion;
  message[5U] = kReserved;
  message[6U] = static_cast<std::uint8_t>(kPayloadSize & 0xffU);
  message[7U] = static_cast<std::uint8_t>((kPayloadSize >> 8U) & 0xffU);

  const std::uint32_t bits = std::bit_cast<std::uint32_t>(status);
  for (std::size_t index = 0U; index < sizeof(std::int32_t); ++index) {
    message[kStatusOffset + index] = static_cast<std::uint8_t>((bits >> (8U * index)) & 0xffU);
  }
  return message;
}

auto parse_status_message(const std::uint8_t *data, std::size_t size,
                          std::int32_t &status) noexcept -> bool {
  if (data == nullptr || size != kStatusMessageSize) {
    return false;
  }
  if (data[0U] != kMagic0 || data[1U] != kMagic1 || data[2U] != kMagic2 || data[3U] != kMagic3) {
    return false;
  }
  if (data[4U] != kVersion || data[5U] != kReserved) {
    return false;
  }
  if (data[6U] != static_cast<std::uint8_t>(kPayloadSize & 0xffU) ||
      data[7U] != static_cast<std::uint8_t>((kPayloadSize >> 8U) & 0xffU)) {
    return false;
  }

  std::uint32_t bits = 0U;
  for (std::size_t index = 0U; index < sizeof(std::int32_t); ++index) {
    bits |= static_cast<std::uint32_t>(data[kStatusOffset + index]) << (8U * index);
  }
  status = std::bit_cast<std::int32_t>(bits);
  return true;
}

} // namespace ztick::plugin::ipc
