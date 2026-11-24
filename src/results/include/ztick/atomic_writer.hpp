// SPDX-License-Identifier: MIT
/**
 * @file atomic_writer.hpp
 * @brief Temporary-file-plus-rename writer for result documents.
 *
 * RESULTS_FORMAT §9 requires that a failed component never corrupt an existing
 * result file. This module serializes nothing itself: it writes complete bytes to
 * a unique temporary file in the target directory, flushes and closes that file,
 * then atomically renames it over the target. A failure leaves the previous target
 * untouched and removes the temporary file. This header is private to ztick-bench
 * and is not installed.
 */

#ifndef ZTICK_ATOMIC_WRITER_HPP
#define ZTICK_ATOMIC_WRITER_HPP

#include <filesystem>
#include <string>
#include <string_view>

/** @brief Portable benchmarking core: workload models and execution models. */
namespace ztick {

/** @brief Outcome of one atomic file write. */
struct FileWriteResult {
  /** @brief True when the target contains the complete new contents. */
  bool ok{false};
  /** @brief Human-readable failure reason; empty on success. */
  std::string error;
  /** @brief Temporary path used for the failed attempt, otherwise empty. */
  std::string temporary_path;

  /**
   * @brief Report whether the target was replaced successfully.
   * @return True only when @ref ok is set.
   */
  [[nodiscard]] bool valid() const noexcept { return ok; }
};

/**
 * @brief Atomically replace a file with complete byte contents.
 * @param target Destination path; its parent directory must already exist.
 * @param contents Complete byte contents to write.
 * @return Success, or an error that identifies the failed step.
 * @note The temporary file is created in @p target's directory so the final
 *       rename stays on the same filesystem and is atomic. An existing target is
 *       never truncated or removed before the rename succeeds.
 */
FileWriteResult write_file_atomic(const std::filesystem::path &target, std::string_view contents);

} // namespace ztick

#endif // ZTICK_ATOMIC_WRITER_HPP
