// SPDX-License-Identifier: MIT
/**
 * @file trust.hpp
 * @brief Persisted trusted-plugin list and explicit first-run consent flow.
 *
 * A plugin is never executed merely because it was discovered. Before first use an
 * untrusted integration must be shown to the user and explicitly approved; the
 * approval is then persisted in a trusted-plugin list so later runs do not prompt
 * again (BR §6.2, REQ-ARCH-08). The manager consumes metadata only, loads no module
 * and calls no adapter code, so a refusal happens before any plugin execution.
 *
 * @note This header is private to ztick-bench and is not installed. Storage and user
 *       interaction are injected interfaces: the CLI/TUI front end supplies a
 *       @ref ztick::plugin::ConsentPrompter, and a @ref ztick::plugin::TrustStore
 *       selects where the list lives. Platform path resolution sits behind
 *       @ref ztick::plugin::default_trust_list_path so no OS calls appear in
 *       portable policy code (REQ-ARCH-02).
 */

#ifndef ZTICK_PLUGIN_TRUST_HPP
#define ZTICK_PLUGIN_TRUST_HPP

#include <ztick/plugin/dynamic_loader.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Host-side discovery, trust and execution support.
 * @note Trust policy is implemented in P3.4; the in-process and out-of-process
 *       runners are implemented in P3.5/P3.6 (ARCHITECTURE §5).
 */
namespace ztick::plugin {

/**
 * @brief Metadata shown to the user when first-run consent is requested.
 * @note Contains only data from read-only descriptor inspection; capabilities are
 *       declared bits, never executable operations.
 */
struct PluginIdentity {
  /** @brief Stable machine-readable integration identifier; must be non-empty. */
  std::string identifier;
  /** @brief Human-readable integration name; must be non-empty. */
  std::string name;
  /** @brief Major version of the integration itself. */
  std::uint32_t adapter_major{};
  /** @brief Minor version of the integration itself. */
  std::uint32_t adapter_minor{};
  /** @brief Patch version of the integration itself. */
  std::uint32_t adapter_patch{};
  /** @brief Major SDK ABI version the adapter was built against. */
  std::uint32_t sdk_major{};
  /** @brief Minor SDK ABI version the adapter was built against. */
  std::uint32_t sdk_minor{};
  /** @brief Patch SDK ABI version the adapter was built against. */
  std::uint32_t sdk_patch{};
  /** @brief Additive bitmask of declared capabilities. */
  std::uint64_t capabilities{};
};

/**
 * @brief Convert inspected metadata into the identity shown to the user.
 * @param metadata Owning metadata returned by a successful inspection.
 * @return Identity carrying the same data, ready for display or persistence.
 * @note Performs no validation and no plugin call; callers should inspect and
 *       negotiate first (P3.2/P3.3).
 */
PluginIdentity identity_from_metadata(const AdapterMetadata &metadata);

/**
 * @brief Outcome of one trust authorization attempt.
 * @note Only @ref TrustStatus::authorized permits execution. Every other status
 *       refuses the integration and carries a human-readable reason.
 */
enum class TrustStatus : std::uint8_t {
  /** @brief Already trusted or explicitly approved for first use. */
  authorized = 0,
  /** @brief The user explicitly refused first-run consent. */
  denied = 1,
  /** @brief No front end could ask the user, or the prompt failed. */
  confirmation_unavailable = 2,
  /** @brief The trusted-plugin list could not be read or updated. */
  store_unavailable = 3,
  /** @brief The supplied identity was malformed. */
  invalid_identity = 4,
  /** @brief An internal error prevented a decision. */
  internal_error = 5,
};

/**
 * @brief Result of one authorization attempt.
 * @note @ref reason is a static, NUL-terminated string that outlives the result.
 */
struct TrustAuthorization {
  /** @brief Decision category; only `authorized` allows execution. */
  TrustStatus status{TrustStatus::internal_error};
  /** @brief Static explanation suitable for display in the front end. */
  const char *reason{"not authorized"};
  /** @brief True only when execution is permitted. */
  bool authorized{false};
  /** @brief True when this call added the plugin to the trusted list. */
  bool newly_trusted{false};
  /** @brief True when the plugin was already in the trusted list. */
  bool previously_trusted{false};

  /**
   * @brief Report whether execution is permitted by this result.
   * @return True only when @ref status is `authorized`.
   */
  // Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
  // cppcheck-suppress unusedFunction
  bool permits_execution() const noexcept { return status == TrustStatus::authorized; }
};

/**
 * @brief User's answer to a first-run consent prompt.
 * @note There is no implicit approval: an absent or failed prompt is reported
 *       explicitly rather than treated as consent.
 */
enum class ConsentDecision : std::uint8_t {
  /** @brief The user explicitly approved this plugin. */
  approve = 0,
  /** @brief The user explicitly refused this plugin. */
  deny = 1,
  /** @brief The front end could not obtain an answer. */
  unavailable = 2,
};

/**
 * @brief Front-end interface for asking the user before first execution.
 * @note Implementations must display the identity, version, SDK version and declared
 *       capabilities in @ref PluginIdentity before returning a decision (BR §6.2).
 *       A missing prompter never means approval.
 */
class ConsentPrompter {
public:
  /** @brief Allow derived prompters to be destroyed through this interface. */
  virtual ~ConsentPrompter() = default;

  /**
   * @brief Ask the user to approve one untrusted plugin.
   * @param identity Metadata to display in the confirmation dialog.
   * @return Explicit decision; `unavailable` when no answer could be obtained.
   * @note Must not load or execute the plugin, and must not block on plugin code.
   *       Implementations that record statistics may use mutable state.
   */
  virtual ConsentDecision confirm_first_run(const PluginIdentity &identity) const noexcept = 0;
};

/**
 * @brief Status of one trusted-list storage operation.
 * @note Uses `ztick_status` codes so failures follow the SDK status model; reasons
 *       are static strings, never dynamically allocated.
 */
struct StoreStatus {
  /** @brief `ztick_status` code describing the operation. */
  std::int32_t code{ztick_status_internal_error};
  /** @brief Static explanation, or NULL on success. */
  const char *reason{"not attempted"};

  /**
   * @brief Report whether the operation succeeded.
   * @return True only when @ref code is `ztick_status_success`.
   */
  bool ok() const noexcept { return code == ztick_status_success; }
};

/**
 * @brief Read/write access to the persisted trusted-plugin list.
 * @note Implementations must fail explicitly; a missing list is a valid empty list,
 *       but a malformed or unreadable list is an error and must never be silently
 *       replaced with an empty one.
 */
class TrustStore {
public:
  /** @brief Allow derived stores to be destroyed through this interface. */
  virtual ~TrustStore() = default;

  /**
   * @brief Read the trusted-plugin list.
   * @param entries Receives the entries; cleared before any data is appended.
   * @return Success with zero entries when no list exists, or an explicit failure.
   * @note Duplicate identifiers are collapsed to the first occurrence.
   */
  virtual StoreStatus load(std::vector<PluginIdentity> &entries) const noexcept = 0;

  /**
   * @brief Replace the persisted trusted-plugin list.
   * @param entries Complete list to write; must contain unique, safe identifiers.
   * @return Success, or an explicit failure without claiming a partial write.
   * @note Implemented as an atomic replace where the platform permits it.
   */
  virtual StoreStatus save(const std::vector<PluginIdentity> &entries) const noexcept = 0;
};

/**
 * @brief In-memory trusted-plugin store.
 * @note Useful for tests and for front ends that do not persist consent; not shared
 *       between processes.
 */
class MemoryTrustStore final : public TrustStore {
public:
  /** @brief Start with an empty trusted list. */
  MemoryTrustStore() noexcept = default;

  /**
   * @brief Read the in-memory entries.
   * @param entries Receives a copy of the current entries.
   * @return Always success.
   */
  StoreStatus load(std::vector<PluginIdentity> &entries) const noexcept override;

  /**
   * @brief Replace the in-memory entries.
   * @param entries Complete list to store.
   * @return Success, or `invalid_argument` when an entry is malformed.
   */
  StoreStatus save(const std::vector<PluginIdentity> &entries) const noexcept override;

private:
  /** @brief Trusted entries held for the lifetime of this store; mutable so the
   *         const store interface can update them. */
  mutable std::vector<PluginIdentity> entries_;
};

/**
 * @brief File-backed trusted-plugin store with atomic replacement.
 * @note The file uses a small versioned line format owned by the host; no third-party
 *       serialization library is required (BR §15). Concurrent writers in different
 *       processes are not coordinated.
 */
class FileTrustStore final : public TrustStore {
public:
  /**
   * @brief Construct a store for one file path.
   * @param path Path to the trusted-plugin list, or an empty string to use
   *             @ref default_trust_list_path.
   * @note A default path that cannot be resolved is reported by @ref load and
   *       @ref save as `invalid_argument`; the constructor does not throw.
   */
  explicit FileTrustStore(std::string path);

  /**
   * @brief Read and parse the trusted-plugin list.
   * @param entries Receives the parsed entries.
   * @return Success with zero entries when the file does not exist; explicit failure
   *         for malformed content or unreadable files.
   */
  StoreStatus load(std::vector<PluginIdentity> &entries) const noexcept override;

  /**
   * @brief Write the list to a temporary file and rename it into place.
   * @param entries Complete list to write.
   * @return Success, or an explicit failure; a failed write leaves the old file intact.
   */
  StoreStatus save(const std::vector<PluginIdentity> &entries) const noexcept override;

private:
  /** @brief Path of the persisted trusted-plugin list. */
  std::string path_;
};

/**
 * @brief Resolve the platform default trusted-plugin list path.
 * @return Absolute path, or `std::nullopt` when no suitable location exists.
 * @note POSIX uses `XDG_CONFIG_HOME` or `HOME/.config`; other platforms return an
 *       explicit absence until their platform work lands post-v1.0.
 */
std::optional<std::string> default_trust_list_path() noexcept;

/**
 * @brief Result of reading the trusted-plugin list.
 * @note Entries are meaningful only when @ref status reports success.
 */
struct TrustListResult {
  /** @brief Storage outcome of the read. */
  StoreStatus status;
  /** @brief Trusted identities, in store order; empty on failure. */
  std::vector<PluginIdentity> entries;
};

/**
 * @brief Enforce the trusted-plugin list and first-run consent policy.
 * @note The manager holds references only; the store and prompter must outlive it.
 *       It performs no plugin loading or execution and never approves implicitly.
 */
class TrustManager {
public:
  /**
   * @brief Construct a trust manager.
   * @param store Backing store for the trusted list; must outlive the manager.
   * @param prompter Front-end consent callback; NULL means no consent is possible.
   */
  TrustManager(const TrustStore &store, const ConsentPrompter *prompter) noexcept;

  /**
   * @brief Decide whether one inspected plugin may be used.
   * @param identity Identity to check and, if approved, persist.
   * @return Authorization carrying an explicit reason for every refusal.
   * @note Trusted identifiers are accepted without prompting. Untrusted identifiers
   *       require an `approve` decision and a successful store update; a `deny` or
   *       unavailable prompt never authorizes execution. Trust is keyed by the
   *       stable identifier, so metadata changes for an already-trusted identifier
   *       do not trigger a new prompt in v1.0.
   */
  TrustAuthorization authorize(const PluginIdentity &identity) noexcept;

  /**
   * @brief List every persisted trusted plugin.
   * @return Store status plus entries; meaningful entries only on success.
   * @note Read-only; never prompts or executes plugin code.
   */
  TrustListResult trusted_plugins() const noexcept;

  /**
   * @brief Remove one identifier from the trusted list.
   * @param identifier Stable identifier to revoke.
   * @param status Receives the storage outcome.
   * @return True when the identifier was present and the updated list was saved.
   * @note Revoking requires no consent and takes effect immediately; the removed
   *       identifier will be untrusted on its next first use.
   */
  bool revoke(const std::string &identifier, StoreStatus &status) noexcept;

private:
  /** @brief Backing store for the trusted list. */
  const TrustStore &store_;
  /** @brief Optional front-end consent callback; NULL when unavailable. */
  const ConsentPrompter *prompter_;
};

} // namespace ztick::plugin

#endif // ZTICK_PLUGIN_TRUST_HPP
