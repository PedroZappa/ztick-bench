// SPDX-License-Identifier: MIT
/**
 * @file trust_test.cpp
 * @brief Deterministic tests for the trusted-plugin list and first-run consent.
 *
 * Every expected value is an independent literal, and injected stores and prompters
 * drive success, denial, refusal, malformed-input and storage-failure paths. The
 * tests assert that an untrusted plugin is authorized only after an explicit
 * approval that was persisted, and that no prompt, denial or store failure ever
 * authorizes execution (BR §6.2, REQ-ARCH-08).
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/plugin/trust.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

/**
 * @brief Build a well-formed plugin identity with chosen fields.
 * @param identifier Stable identifier to use.
 * @param name Human-readable name to use.
 * @return Identity carrying literal versions and capability bits.
 */
ztick::plugin::PluginIdentity make_identity(std::string identifier,
                                            std::string name = "Test plugin") {
  ztick::plugin::PluginIdentity identity;
  identity.identifier = std::move(identifier);
  identity.name = std::move(name);
  identity.adapter_major = 2U;
  identity.adapter_minor = 3U;
  identity.adapter_patch = 4U;
  identity.sdk_major = 1U;
  identity.sdk_minor = 0U;
  identity.sdk_patch = 0U;
  identity.capabilities = UINT64_C(5);
  return identity;
}

/**
 * @brief Front end that records prompts and returns a fixed decision.
 * @note Records through `mutable` members so the const interface can count calls.
 */
class RecordingPrompter final : public ztick::plugin::ConsentPrompter {
public:
  /**
   * @brief Start a prompter that always returns one decision.
   * @param decision Decision to return from every prompt.
   */
  explicit RecordingPrompter(ztick::plugin::ConsentDecision decision) noexcept
      : decision_(decision) {}

  /**
   * @brief Record one prompt and return the configured decision.
   * @param identity Identity offered for confirmation.
   * @return The configured decision.
   */
  ztick::plugin::ConsentDecision
  confirm_first_run(const ztick::plugin::PluginIdentity &identity) const noexcept override {
    ++calls_;
    try {
      last_identity_ = identity;
    } catch (...) {
      last_identity_.reset();
    }
    return decision_;
  }

  /** @brief Number of prompts observed. */
  int calls() const noexcept { return calls_; }

  /** @brief Identity passed to the most recent prompt, or empty when none. */
  const std::optional<ztick::plugin::PluginIdentity> &last_identity() const noexcept {
    return last_identity_;
  }

private:
  /** @brief Decision returned from every prompt. */
  ztick::plugin::ConsentDecision decision_;
  /** @brief Prompt counter; mutable so the const interface can record. */
  mutable int calls_{0};
  /** @brief Identity from the most recent prompt. */
  mutable std::optional<ztick::plugin::PluginIdentity> last_identity_;
};

/**
 * @brief Trust store with switchable load and save failures.
 * @note The in-memory list behaves like @ref ztick::plugin::MemoryTrustStore.
 */
class ConfigurableStore final : public ztick::plugin::TrustStore {
public:
  /** @brief Entries returned by @ref load; mutable so the const store interface can
   *         update them. */
  mutable std::vector<ztick::plugin::PluginIdentity> entries;
  /** @brief When true, @ref load reports a failure. */
  bool fail_load{false};
  /** @brief When true, @ref save reports a failure. */
  bool fail_save{false};
  /** @brief Reason reported by injected failures; may be NULL to test fallbacks. */
  const char *failure_reason{"injected store failure"};

  /**
   * @brief Read entries or report the injected failure.
   * @param result Receives the entries on success.
   * @return Success or an injected failure.
   */
  ztick::plugin::StoreStatus
  load(std::vector<ztick::plugin::PluginIdentity> &result) const noexcept override {
    if (fail_load) {
      return ztick::plugin::StoreStatus{ztick_status_internal_error, failure_reason};
    }
    try {
      result = entries;
      return ztick::plugin::StoreStatus{ztick_status_success, nullptr};
    } catch (...) {
      return ztick::plugin::StoreStatus{ztick_status_internal_error, "cannot copy entries"};
    }
  }

  /**
   * @brief Replace entries or report the injected failure.
   * @param input Complete list to store.
   * @return Success or an injected failure.
   */
  ztick::plugin::StoreStatus
  save(const std::vector<ztick::plugin::PluginIdentity> &input) const noexcept override {
    if (fail_save) {
      return ztick::plugin::StoreStatus{ztick_status_internal_error, failure_reason};
    }
    try {
      entries = input;
      return ztick::plugin::StoreStatus{ztick_status_success, nullptr};
    } catch (...) {
      return ztick::plugin::StoreStatus{ztick_status_internal_error, "cannot copy entries"};
    }
  }
};

/**
 * @brief Unique temporary directory removed when the guard goes out of scope.
 */
struct TempDir {
  /** @brief Directory path owned by this guard. */
  std::filesystem::path path;

  /**
   * @brief Create a uniquely named directory under the system temporary directory.
   * @param tag Short label included in the name for diagnosis.
   */
  explicit TempDir(const char *tag) : path(create(tag)) {}

  /** @brief Remove the directory tree, ignoring cleanup errors. */
  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  /** @brief Copies would double-remove the same directory. */
  TempDir(const TempDir &) = delete;
  /** @brief Copies would double-remove the same directory. */
  TempDir &operator=(const TempDir &) = delete;

  /**
   * @brief Build a fresh unique directory path and create it.
   * @param tag Short label included in the name.
   * @return Created directory path.
   */
  static std::filesystem::path create(const char *tag) {
    static unsigned counter = 0U;
    const std::string suffix = std::to_string(++counter);
#if !defined(_WIN32)
    const std::string pid = std::to_string(static_cast<long>(::getpid()));
#else
    const std::string pid = "win";
#endif
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("ztick-trust-" + std::string(tag) + "-" + pid + "-" + suffix);
    std::filesystem::create_directories(directory);
    return directory;
  }
};

#if !defined(_WIN32)
/**
 * @brief Save one environment variable and restore it on destruction.
 */
class EnvironmentSnapshot {
public:
  /**
   * @brief Capture the current value of one variable.
   * @param name Variable name; must outlive the guard.
   */
  explicit EnvironmentSnapshot(const char *name) : name_(name) {
    const char *value = std::getenv(name);
    if (value != nullptr) {
      value_ = value;
    }
  }

  /** @brief Restore the captured value, unsetting the variable when it was absent. */
  ~EnvironmentSnapshot() {
    if (value_.has_value()) {
      ::setenv(name_.c_str(), value_->c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
  }

  /** @brief Copies would restore the same variable twice. */
  EnvironmentSnapshot(const EnvironmentSnapshot &) = delete;
  /** @brief Copies would restore the same variable twice. */
  EnvironmentSnapshot &operator=(const EnvironmentSnapshot &) = delete;

private:
  /** @brief Variable name captured by this guard. */
  std::string name_;
  /** @brief Previous value, or empty when the variable was unset. */
  std::optional<std::string> value_;
};

/**
 * @brief Overwrite one file with literal text.
 * @param path File to write.
 * @param text Complete contents.
 */
void write_text_file(const std::filesystem::path &path, const std::string &text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  REQUIRE(stream.is_open());
  stream << text;
  stream.close();
  REQUIRE(stream.good());
}
#endif

/**
 * @brief Serialize one identity as a trusted-list line for fixture files.
 * @param identity Entry to serialize.
 * @return Tab-separated line without a trailing newline.
 */
std::string trust_line(const ztick::plugin::PluginIdentity &identity) {
  return identity.identifier + "\t" + identity.name + "\t" +
         std::to_string(identity.adapter_major) + "\t" + std::to_string(identity.adapter_minor) +
         "\t" + std::to_string(identity.adapter_patch) + "\t" + std::to_string(identity.sdk_major) +
         "\t" + std::to_string(identity.sdk_minor) + "\t" + std::to_string(identity.sdk_patch) +
         "\t" + std::to_string(identity.capabilities);
}

} // namespace

/**
 * @brief Verify inspected metadata is copied field by field into the identity.
 */
TEST_CASE("identity copies metadata without extra state", "[plugin][trust]") {
  ztick::plugin::AdapterMetadata metadata;
  metadata.identifier = "trust.test.metadata";
  metadata.name = "Trust metadata adapter";
  metadata.adapter_major = 7U;
  metadata.adapter_minor = 8U;
  metadata.adapter_patch = 9U;
  metadata.sdk_major = 1U;
  metadata.sdk_minor = 0U;
  metadata.sdk_patch = 0U;
  metadata.capabilities = UINT64_C(9);
  metadata.metadata = "ignored";

  const ztick::plugin::PluginIdentity identity = ztick::plugin::identity_from_metadata(metadata);
  CHECK(identity.identifier == "trust.test.metadata");
  CHECK(identity.name == "Trust metadata adapter");
  CHECK(identity.adapter_major == 7U);
  CHECK(identity.adapter_minor == 8U);
  CHECK(identity.adapter_patch == 9U);
  CHECK(identity.sdk_major == 1U);
  CHECK(identity.sdk_minor == 0U);
  CHECK(identity.sdk_patch == 0U);
  CHECK(identity.capabilities == UINT64_C(9));
}

/**
 * @brief Verify the in-memory store round-trips entries and rejects bad input.
 */
TEST_CASE("memory store round-trips and validates entries", "[plugin][trust]") {
  ztick::plugin::MemoryTrustStore store;
  std::vector<ztick::plugin::PluginIdentity> entries;
  REQUIRE(store.load(entries).ok());
  CHECK(entries.empty());

  entries.push_back(make_identity("trust.test.memory.one"));
  entries.push_back(make_identity("trust.test.memory.two"));
  REQUIRE(store.save(entries).ok());

  std::vector<ztick::plugin::PluginIdentity> loaded;
  REQUIRE(store.load(loaded).ok());
  REQUIRE(loaded.size() == 2U);
  CHECK(loaded[0].identifier == "trust.test.memory.one");
  CHECK(loaded[1].identifier == "trust.test.memory.two");

  std::vector<ztick::plugin::PluginIdentity> duplicate{make_identity("trust.test.memory.one"),
                                                       make_identity("trust.test.memory.one")};
  const ztick::plugin::StoreStatus duplicate_status = store.save(duplicate);
  CHECK(duplicate_status.code == ztick_status_invalid_argument);
  CHECK(std::string(duplicate_status.reason) == "duplicate trusted-list identifier");

  std::vector<ztick::plugin::PluginIdentity> malformed{make_identity("")};
  const ztick::plugin::StoreStatus malformed_status = store.save(malformed);
  CHECK(malformed_status.code == ztick_status_invalid_argument);
  CHECK(std::string(malformed_status.reason) == "malformed trusted-list entry");
}

/**
 * @brief Verify the file store persists entries and reads them from a new instance.
 */
TEST_CASE("file store persists entries across instances", "[plugin][trust]") {
  const TempDir directory("persist");
  const std::filesystem::path list = directory.path / "trusted.txt";

  const std::vector<ztick::plugin::PluginIdentity> entries{make_identity("trust.test.file.one"),
                                                           make_identity("trust.test.file.two")};
  {
    ztick::plugin::FileTrustStore store(list.string());
    REQUIRE(store.save(entries).ok());
  }
  CHECK(std::filesystem::exists(list));

  std::ifstream stream(list);
  REQUIRE(stream.is_open());
  std::string header;
  REQUIRE(std::getline(stream, header));
  CHECK(header == "ztick-trust-list-v1");

  ztick::plugin::FileTrustStore reopened(list.string());
  std::vector<ztick::plugin::PluginIdentity> loaded;
  REQUIRE(reopened.load(loaded).ok());
  REQUIRE(loaded.size() == 2U);
  CHECK(loaded[0].identifier == "trust.test.file.one");
  CHECK(loaded[0].name == "Test plugin");
  CHECK(loaded[0].adapter_major == 2U);
  CHECK(loaded[0].adapter_minor == 3U);
  CHECK(loaded[0].adapter_patch == 4U);
  CHECK(loaded[0].sdk_major == 1U);
  CHECK(loaded[0].sdk_minor == 0U);
  CHECK(loaded[0].sdk_patch == 0U);
  CHECK(loaded[0].capabilities == UINT64_C(5));
  CHECK(loaded[1].identifier == "trust.test.file.two");
}

/**
 * @brief Verify a missing list is an empty list while malformed content is an error.
 */
TEST_CASE("file store distinguishes missing and malformed lists", "[plugin][trust]") {
  const TempDir directory("malformed");
  const std::filesystem::path list = directory.path / "trusted.txt";

  ztick::plugin::FileTrustStore store(list.string());
  std::vector<ztick::plugin::PluginIdentity> entries;
  REQUIRE(store.load(entries).ok());
  CHECK(entries.empty());

#if !defined(_WIN32)
  write_text_file(list, "wrong-header\n");
  const ztick::plugin::StoreStatus bad_header = store.load(entries);
  CHECK(bad_header.code == ztick_status_internal_error);
  CHECK(std::string(bad_header.reason) == "malformed trusted-plugin list header");

  write_text_file(list, "ztick-trust-list-v1\ntoo\tfew\tfields\n");
  const ztick::plugin::StoreStatus bad_entry = store.load(entries);
  CHECK(bad_entry.code == ztick_status_internal_error);
  CHECK(std::string(bad_entry.reason) == "malformed trusted-plugin list entry");

  const ztick::plugin::PluginIdentity identity = make_identity("trust.test.file.duplicate");
  write_text_file(list, "ztick-trust-list-v1\n" + trust_line(identity) + "\n" +
                            trust_line(identity) + "\n");
  REQUIRE(store.load(entries).ok());
  REQUIRE(entries.size() == 1U);
  CHECK(entries[0].identifier == "trust.test.file.duplicate");

  write_text_file(list, "ztick-trust-list-v1\n");
  std::vector<ztick::plugin::PluginIdentity> malformed{make_identity("trust.test.file.bad")};
  malformed[0].name = "bad\nname";
  const ztick::plugin::StoreStatus bad_save = store.save(malformed);
  CHECK(bad_save.code == ztick_status_invalid_argument);
  CHECK(std::string(bad_save.reason) == "malformed trusted-list entry");
#endif
}

/**
 * @brief Verify the default path follows XDG, falls back to HOME and reports absence.
 */
TEST_CASE("default trust path follows the user configuration directory", "[plugin][trust]") {
#if !defined(_WIN32)
  const EnvironmentSnapshot xdg_snapshot("XDG_CONFIG_HOME");
  const EnvironmentSnapshot home_snapshot("HOME");
  const TempDir directory("default");

  const std::string xdg = directory.path.string();
  REQUIRE(::setenv("XDG_CONFIG_HOME", xdg.c_str(), 1) == 0);
  const std::filesystem::path expected = directory.path / "ztick-bench" / "trusted-plugins.txt";
  const std::optional<std::string> resolved = ztick::plugin::default_trust_list_path();
  REQUIRE(resolved.has_value());
  CHECK(std::filesystem::path(*resolved) == expected);

  REQUIRE(::setenv("XDG_CONFIG_HOME", (xdg + "/").c_str(), 1) == 0);
  const std::optional<std::string> trailing = ztick::plugin::default_trust_list_path();
  REQUIRE(trailing.has_value());
  CHECK(std::filesystem::path(*trailing) == expected);

  ztick::plugin::FileTrustStore store("");
  std::vector<ztick::plugin::PluginIdentity> entries{make_identity("trust.test.default")};
  REQUIRE(store.save(entries).ok());
  CHECK(std::filesystem::exists(expected));

  std::vector<ztick::plugin::PluginIdentity> loaded;
  REQUIRE(store.load(loaded).ok());
  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0].identifier == "trust.test.default");

  REQUIRE(::setenv("HOME", xdg.c_str(), 1) == 0);
  const std::filesystem::path home_expected =
      directory.path / ".config" / "ztick-bench" / "trusted-plugins.txt";

  REQUIRE(::setenv("XDG_CONFIG_HOME", "relative/config", 1) == 0);
  const std::optional<std::string> relative_ignored = ztick::plugin::default_trust_list_path();
  REQUIRE(relative_ignored.has_value());
  CHECK(std::filesystem::path(*relative_ignored) == home_expected);

  REQUIRE(::unsetenv("XDG_CONFIG_HOME") == 0);
  const std::optional<std::string> home_resolved = ztick::plugin::default_trust_list_path();
  REQUIRE(home_resolved.has_value());
  CHECK(std::filesystem::path(*home_resolved) == home_expected);

  REQUIRE(::unsetenv("XDG_CONFIG_HOME") == 0);
  REQUIRE(::unsetenv("HOME") == 0);
  CHECK_FALSE(ztick::plugin::default_trust_list_path().has_value());

  ztick::plugin::FileTrustStore unavailable("");
  std::vector<ztick::plugin::PluginIdentity> none;
  const ztick::plugin::StoreStatus status = unavailable.load(none);
  CHECK(status.code == ztick_status_invalid_argument);
  CHECK(std::string(status.reason) == "trusted-plugin list path unavailable");
#else
  SUCCEED("POSIX environment convention is not exercised on this platform");
#endif
}

/**
 * @brief Verify an already-trusted plugin is authorized without prompting.
 */
TEST_CASE("trusted plugin is authorized without a prompt", "[plugin][trust]") {
  const ztick::plugin::PluginIdentity identity = make_identity("trust.test.known");
  ztick::plugin::MemoryTrustStore store;
  std::vector<ztick::plugin::PluginIdentity> seed{identity};
  REQUIRE(store.save(seed).ok());

  RecordingPrompter prompter(ztick::plugin::ConsentDecision::deny);
  ztick::plugin::TrustManager manager(store, &prompter);
  const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
  CHECK(result.permits_execution());
  CHECK(result.authorized);
  CHECK_FALSE(result.newly_trusted);
  CHECK(result.previously_trusted);
  CHECK(prompter.calls() == 0);
}

/**
 * @brief Verify an untrusted plugin requires approval and the approval persists.
 */
TEST_CASE("untrusted plugin requires explicit approval", "[plugin][trust]") {
  const ztick::plugin::PluginIdentity identity = make_identity("trust.test.first-run");
  ztick::plugin::MemoryTrustStore store;
  RecordingPrompter prompter(ztick::plugin::ConsentDecision::approve);
  ztick::plugin::TrustManager manager(store, &prompter);

  const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
  CHECK(result.permits_execution());
  CHECK(result.authorized);
  CHECK(result.newly_trusted);
  CHECK_FALSE(result.previously_trusted);
  CHECK(prompter.calls() == 1);
  REQUIRE(prompter.last_identity().has_value());
  CHECK(prompter.last_identity()->identifier == "trust.test.first-run");
  CHECK(prompter.last_identity()->name == "Test plugin");
  CHECK(prompter.last_identity()->capabilities == UINT64_C(5));

  std::vector<ztick::plugin::PluginIdentity> stored;
  REQUIRE(store.load(stored).ok());
  REQUIRE(stored.size() == 1U);
  CHECK(stored[0].identifier == "trust.test.first-run");

  const ztick::plugin::TrustAuthorization again = manager.authorize(identity);
  CHECK(again.previously_trusted);
  CHECK_FALSE(again.newly_trusted);
  CHECK(prompter.calls() == 1);
}

/**
 * @brief Verify denial, missing consent and malformed input never authorize.
 */
TEST_CASE("untrusted plugin is refused without approval", "[plugin][trust]") {
  const ztick::plugin::PluginIdentity identity = make_identity("trust.test.refused");
  ztick::plugin::MemoryTrustStore store;

  {
    RecordingPrompter prompter(ztick::plugin::ConsentDecision::deny);
    ztick::plugin::TrustManager manager(store, &prompter);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK_FALSE(result.authorized);
    CHECK(result.status == ztick::plugin::TrustStatus::denied);
    CHECK(std::string(result.reason) == "first-run confirmation denied");
    CHECK(prompter.calls() == 1);
  }

  {
    RecordingPrompter prompter(ztick::plugin::ConsentDecision::unavailable);
    ztick::plugin::TrustManager manager(store, &prompter);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK(result.status == ztick::plugin::TrustStatus::confirmation_unavailable);
    CHECK(std::string(result.reason) == "first-run confirmation unavailable");
  }

  {
    ztick::plugin::TrustManager manager(store, nullptr);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK(result.status == ztick::plugin::TrustStatus::confirmation_unavailable);
    CHECK(std::string(result.reason) == "first-run confirmation unavailable");
  }

  {
    RecordingPrompter prompter(ztick::plugin::ConsentDecision::approve);
    ztick::plugin::TrustManager manager(store, &prompter);
    ztick::plugin::PluginIdentity malformed = identity;
    malformed.identifier.clear();
    const ztick::plugin::TrustAuthorization result = manager.authorize(malformed);
    CHECK_FALSE(result.permits_execution());
    CHECK(result.status == ztick::plugin::TrustStatus::invalid_identity);
    CHECK(std::string(result.reason) == "malformed plugin identity");
    CHECK(prompter.calls() == 0);
  }

  std::vector<ztick::plugin::PluginIdentity> stored;
  REQUIRE(store.load(stored).ok());
  CHECK(stored.empty());
}

/**
 * @brief Verify storage failures refuse execution and surface a reason.
 */
TEST_CASE("trust store failures never authorize", "[plugin][trust]") {
  const ztick::plugin::PluginIdentity identity = make_identity("trust.test.store");
  RecordingPrompter prompter(ztick::plugin::ConsentDecision::approve);

  {
    ConfigurableStore store;
    store.fail_load = true;
    ztick::plugin::TrustManager manager(store, &prompter);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK(result.status == ztick::plugin::TrustStatus::store_unavailable);
    CHECK(std::string(result.reason) == "injected store failure");
    CHECK(prompter.calls() == 0);
  }

  {
    ConfigurableStore store;
    store.fail_load = true;
    store.failure_reason = nullptr;
    ztick::plugin::TrustManager manager(store, &prompter);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK(std::string(result.reason) == "cannot read trusted-plugin list");
  }

  {
    ConfigurableStore store;
    store.fail_save = true;
    ztick::plugin::TrustManager manager(store, &prompter);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK(result.status == ztick::plugin::TrustStatus::store_unavailable);
    CHECK(std::string(result.reason) == "injected store failure");
    CHECK(prompter.calls() == 1);
    CHECK(store.entries.empty());
  }

  {
    ConfigurableStore store;
    store.fail_save = true;
    store.failure_reason = nullptr;
    ztick::plugin::TrustManager manager(store, &prompter);
    const ztick::plugin::TrustAuthorization result = manager.authorize(identity);
    CHECK_FALSE(result.permits_execution());
    CHECK(result.status == ztick::plugin::TrustStatus::store_unavailable);
    CHECK(std::string(result.reason) == "cannot update trusted-plugin list");
  }
}

/**
 * @brief Verify the trusted list can be listed and revoked explicitly.
 */
TEST_CASE("trusted plugins can be listed and revoked", "[plugin][trust]") {
  const ztick::plugin::PluginIdentity first = make_identity("trust.test.revoke.one");
  const ztick::plugin::PluginIdentity second = make_identity("trust.test.revoke.two");
  ztick::plugin::MemoryTrustStore store;
  std::vector<ztick::plugin::PluginIdentity> seed{first, second};
  REQUIRE(store.save(seed).ok());

  ztick::plugin::TrustManager manager(store, nullptr);
  const ztick::plugin::TrustListResult listed = manager.trusted_plugins();
  REQUIRE(listed.status.ok());
  REQUIRE(listed.entries.size() == 2U);
  CHECK(listed.entries[0].identifier == "trust.test.revoke.one");
  CHECK(listed.entries[1].identifier == "trust.test.revoke.two");

  ztick::plugin::StoreStatus status{ztick_status_internal_error, "not attempted"};
  CHECK(manager.revoke("trust.test.revoke.one", status));
  CHECK(status.ok());

  const ztick::plugin::TrustListResult after = manager.trusted_plugins();
  REQUIRE(after.status.ok());
  REQUIRE(after.entries.size() == 1U);
  CHECK(after.entries[0].identifier == "trust.test.revoke.two");

  CHECK_FALSE(manager.revoke("trust.test.revoke.missing", status));
  CHECK(status.ok());
  CHECK_FALSE(manager.revoke("", status));
  CHECK(status.code == ztick_status_invalid_argument);
  CHECK(std::string(status.reason) == "malformed plugin identifier");

  RecordingPrompter prompter(ztick::plugin::ConsentDecision::deny);
  ztick::plugin::TrustManager approving(store, &prompter);
  const ztick::plugin::TrustAuthorization revoked = approving.authorize(first);
  CHECK_FALSE(revoked.permits_execution());
  CHECK(revoked.status == ztick::plugin::TrustStatus::denied);
  CHECK(prompter.calls() == 1);
}

/**
 * @brief Verify listing a broken store reports failure without partial entries.
 */
TEST_CASE("trusted plugin listing reports store failures", "[plugin][trust]") {
  ConfigurableStore store;
  store.fail_load = true;
  ztick::plugin::TrustManager manager(store, nullptr);
  const ztick::plugin::TrustListResult listed = manager.trusted_plugins();
  CHECK_FALSE(listed.status.ok());
  CHECK(listed.status.code == ztick_status_internal_error);
  CHECK(std::string(listed.status.reason) == "injected store failure");
  CHECK(listed.entries.empty());
}
