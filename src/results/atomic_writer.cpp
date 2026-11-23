// SPDX-License-Identifier: MIT
/**
 * @file atomic_writer.cpp
 * @brief Implementation of the temporary-file-plus-rename writer declared in
 *        @ref atomic_writer.hpp.
 */

#include "ztick/atomic_writer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

namespace ztick {

namespace {

/** @brief Process-local counter that makes temporary names unique within a clock tick. */
std::atomic<std::uint64_t> g_temporary_counter{0};

/**
 * @brief Render a 64-bit value as fixed-width lowercase hexadecimal.
 * @param value Value to render.
 * @return Exactly sixteen hexadecimal digits.
 */
std::string to_hex(std::uint64_t value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text(16U, '0');
  for (std::size_t index = 0; index < text.size(); ++index) {
    text[text.size() - 1U - index] = kDigits[value & 0xFUL];
    value >>= 4U;
  }
  return text;
}

/**
 * @brief Build an unused temporary path in the target's directory.
 * @param target Destination path whose parent must already exist.
 * @return Candidate path that did not exist when checked.
 * @note The candidate shares the target directory so the final rename cannot
 *       cross a filesystem boundary. The existence check is a best-effort
 *       uniqueness guard for the single writing process.
 */
std::filesystem::path make_temporary_path(const std::filesystem::path &target) {
  const auto ticks =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t counter = g_temporary_counter.fetch_add(1U, std::memory_order_relaxed);

  std::string name = target.filename().string();
  if (name.empty()) {
    name = "result.json";
  }
  name += ".tmp.";
  name += to_hex(ticks);
  name += '.';
  name += to_hex(counter);

  const std::filesystem::path parent = target.parent_path();
  const std::filesystem::path directory = parent.empty() ? std::filesystem::path{"."} : parent;
  return directory / name;
}

/**
 * @brief Remove a temporary file without throwing.
 * @param path Temporary path to remove.
 */
void remove_temporary(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

} // namespace

FileWriteResult write_file_atomic(const std::filesystem::path &target, std::string_view contents) {
  FileWriteResult result;
  std::error_code error;

  const std::filesystem::path parent = target.parent_path();
  const std::filesystem::path directory = parent.empty() ? std::filesystem::path{"."} : parent;
  if (!std::filesystem::is_directory(directory, error) || error) {
    result.error = "target directory does not exist: " + directory.string();
    return result;
  }
  if (contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    result.error = "contents exceed the maximum stream size";
    return result;
  }

  std::filesystem::path temporary;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const std::filesystem::path candidate = make_temporary_path(target);
    error.clear();
    if (!std::filesystem::exists(candidate, error) && !error) {
      temporary = candidate;
      break;
    }
  }
  if (temporary.empty()) {
    result.error = "could not allocate an unused temporary file";
    return result;
  }
  result.temporary_path = temporary.string();

  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
      result.error = "could not create temporary file: " + temporary.string();
      remove_temporary(temporary);
      return result;
    }
    if (!contents.empty()) {
      stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    stream.flush();
    if (!stream.good()) {
      result.error = "could not write temporary file: " + temporary.string();
      stream.close();
      remove_temporary(temporary);
      return result;
    }
    stream.close();
    if (stream.fail()) {
      result.error = "could not close temporary file: " + temporary.string();
      remove_temporary(temporary);
      return result;
    }
  }

  std::filesystem::rename(temporary, target, error);
  if (error) {
    result.error = "could not replace target: " + error.message();
    remove_temporary(temporary);
    return result;
  }

  result.temporary_path.clear();
  result.ok = true;
  return result;
}

} // namespace ztick
