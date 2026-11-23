// SPDX-License-Identifier: MIT
/**
 * @file atomic_writer_test.cpp
 * @brief Catch2 tests for the temporary-file-plus-replacement writer.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/atomic_writer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

/** @brief Scoped temporary directory that is removed when the test ends. */
class ScopedDirectory {
public:
  /** @brief Create a uniquely named directory under the system temp directory. */
  ScopedDirectory() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("ztick_atomic_test_" + std::to_string(ticks) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directory(path_);
  }

  /** @brief Remove the directory and everything created inside it. */
  ~ScopedDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  /**
   * @brief Access the created directory.
   * @return Directory path shared by the tests in this file.
   */
  [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

private:
  /** @brief Absolute path of the scoped directory. */
  std::filesystem::path path_;
};

/**
 * @brief Read a whole file as binary text.
 * @param path File to read.
 * @return Complete file contents.
 */
std::string read_all(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

/**
 * @brief Count the entries directly inside a directory.
 * @param path Directory to inspect.
 * @return Number of immediate entries, including temporary leftovers.
 */
std::size_t entry_count(const std::filesystem::path &path) {
  return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator{path},
                                                std::filesystem::directory_iterator{}));
}

} // namespace

/**
 * @brief Verify a new target receives exactly the supplied bytes.
 */
TEST_CASE("atomic writer creates a file with exact contents", "[results][atomic]") {
  ScopedDirectory directory;
  const std::filesystem::path target = directory.path() / "result.json";

  const auto written = ztick::write_file_atomic(target, "{\"status\":\"complete\"}");
  REQUIRE(written.valid());
  REQUIRE(written.error.empty());
  REQUIRE(written.temporary_path.empty());
  REQUIRE(read_all(target) == "{\"status\":\"complete\"}");
  REQUIRE(entry_count(directory.path()) == 1U);
}

/**
 * @brief Verify an existing target is fully replaced and no temporary remains.
 */
TEST_CASE("atomic writer replaces an existing file", "[results][atomic]") {
  ScopedDirectory directory;
  const std::filesystem::path target = directory.path() / "result.json";
  REQUIRE(ztick::write_file_atomic(target, "old contents").valid());

  const auto written = ztick::write_file_atomic(target, "new");
  REQUIRE(written.valid());
  REQUIRE(read_all(target) == "new");
  REQUIRE(entry_count(directory.path()) == 1U);
}

/**
 * @brief Verify empty contents produce an empty target file.
 */
TEST_CASE("atomic writer accepts empty contents", "[results][atomic]") {
  ScopedDirectory directory;
  const std::filesystem::path target = directory.path() / "empty.json";

  REQUIRE(ztick::write_file_atomic(target, "").valid());
  REQUIRE(std::filesystem::file_size(target) == 0U);
  REQUIRE(entry_count(directory.path()) == 1U);
}

/**
 * @brief Verify a failed replacement leaves the target and directory untouched.
 */
TEST_CASE("atomic writer preserves the target when replacement fails", "[results][atomic]") {
  ScopedDirectory directory;
  const std::filesystem::path target = directory.path() / "occupied";
  REQUIRE(std::filesystem::create_directory(target));

  const auto written = ztick::write_file_atomic(target, "payload");
  REQUIRE_FALSE(written.valid());
  REQUIRE_FALSE(written.error.empty());
  REQUIRE_FALSE(written.temporary_path.empty());
  REQUIRE(std::filesystem::is_directory(target));
  REQUIRE(entry_count(directory.path()) == 1U);
}

/**
 * @brief Verify a missing parent directory is reported before any file is created.
 */
TEST_CASE("atomic writer rejects a missing parent directory", "[results][atomic]") {
  ScopedDirectory directory;
  const std::filesystem::path target = directory.path() / "missing" / "result.json";

  const auto written = ztick::write_file_atomic(target, "payload");
  REQUIRE_FALSE(written.valid());
  REQUIRE(written.error.find("target directory") != std::string::npos);
  REQUIRE(entry_count(directory.path()) == 0U);
}
