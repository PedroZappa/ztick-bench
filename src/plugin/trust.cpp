// SPDX-License-Identifier: MIT
/**
 * @file trust.cpp
 * @brief Implementation of the trusted-plugin list and first-run consent policy.
 *
 * Reads and writes a small versioned text list, enforces explicit consent before an
 * untrusted plugin is used, and records approved identifiers. No plugin code is
 * loaded or executed; failures are reported with a status and reason rather than
 * silently omitted (BR §6.2, §17).
 */

#include <ztick/plugin/trust.hpp>

#include <ztick/types.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ztick::plugin {
namespace {

/** @brief First line of every trusted-plugin list file. */
constexpr const char *kListHeader = "ztick-trust-list-v1";

/** @brief Number of tab-separated fields in one list entry. */
constexpr std::size_t kFieldCount = 9U;

/**
 * @brief Report whether a field can be stored safely in the line format.
 * @param field Text to check.
 * @return True when non-empty and free of control characters.
 */
bool is_safe_field(std::string_view field) noexcept {
  if (field.empty()) {
    return false;
  }
  return std::none_of(field.begin(), field.end(), [](char c) {
    const unsigned char byte = static_cast<unsigned char>(c);
    return byte < 0x20U || byte == 0x7FU;
  });
}

/**
 * @brief Parse a decimal unsigned integer without throwing.
 * @param text Digits to parse; must be non-empty and contain only `0`-`9`.
 * @param value Receives the parsed value on success.
 * @return True when the whole field is a representable unsigned integer.
 */
bool parse_u64(std::string_view text, std::uint64_t &value) noexcept {
  if (text.empty()) {
    return false;
  }
  std::uint64_t parsed = 0U;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
      return false;
    }
    parsed = parsed * 10U + digit;
  }
  value = parsed;
  return true;
}

/**
 * @brief Parse a version component that must fit in `uint32_t`.
 * @param text Decimal component text.
 * @param value Receives the component on success.
 * @return True when the field is a decimal number within range.
 */
bool parse_u32(std::string_view text, std::uint32_t &value) noexcept {
  std::uint64_t wide = 0U;
  if (!parse_u64(text, wide) || wide > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  value = static_cast<std::uint32_t>(wide);
  return true;
}

/**
 * @brief Validate one identity for storage or authorization.
 * @param identity Identity to check.
 * @return True when identifier and name are non-empty and control-free.
 */
bool is_valid_identity(const PluginIdentity &identity) noexcept {
  return is_safe_field(identity.identifier) && is_safe_field(identity.name);
}

/**
 * @brief Build a success status.
 * @return Status carrying `ztick_status_success` and no reason.
 */
StoreStatus success() noexcept { return StoreStatus{ztick_status_success, nullptr}; }

/**
 * @brief Build a failure status with a stable reason.
 * @param code `ztick_status` code to report.
 * @param reason Static explanation; must not be NULL.
 * @return Failure status.
 */
StoreStatus failure(std::int32_t code, const char *reason) noexcept {
  return StoreStatus{code, reason};
}

/**
 * @brief Substitute a fallback for a NULL reason from a custom store.
 * @param reason Reason supplied by a store implementation; may be NULL.
 * @param fallback Static fallback owned by the caller.
 * @return @p reason when non-NULL, otherwise @p fallback.
 */
const char *reason_or(const char *reason, const char *fallback) noexcept {
  return reason != nullptr ? reason : fallback;
}

/**
 * @brief Serialize one identity into a single list line.
 * @param identity Entry to serialize.
 * @param line Receives the tab-separated fields.
 * @return True on success; false when the identity cannot be stored safely.
 */
bool serialize_identity(const PluginIdentity &identity, std::string &line) {
  if (!is_valid_identity(identity)) {
    return false;
  }
  line = identity.identifier;
  line += '\t';
  line += identity.name;
  line += '\t';
  line += std::to_string(identity.adapter_major);
  line += '\t';
  line += std::to_string(identity.adapter_minor);
  line += '\t';
  line += std::to_string(identity.adapter_patch);
  line += '\t';
  line += std::to_string(identity.sdk_major);
  line += '\t';
  line += std::to_string(identity.sdk_minor);
  line += '\t';
  line += std::to_string(identity.sdk_patch);
  line += '\t';
  line += std::to_string(identity.capabilities);
  return true;
}

/**
 * @brief Parse one list line into an identity.
 * @param line Tab-separated entry text.
 * @param identity Receives the parsed entry.
 * @return True when all fields are present, safe and in range.
 */
bool parse_identity(std::string_view line, PluginIdentity &identity) {
  std::vector<std::string_view> fields;
  fields.reserve(kFieldCount);
  std::size_t start = 0U;
  while (true) {
    const std::size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab == std::string_view::npos ? tab : tab - start));
    if (tab == std::string_view::npos) {
      break;
    }
    start = tab + 1U;
  }
  if (fields.size() != kFieldCount) {
    return false;
  }
  PluginIdentity parsed;
  parsed.identifier = std::string(fields[0]);
  parsed.name = std::string(fields[1]);
  if (!is_valid_identity(parsed)) {
    return false;
  }
  if (!parse_u32(fields[2], parsed.adapter_major) || !parse_u32(fields[3], parsed.adapter_minor) ||
      !parse_u32(fields[4], parsed.adapter_patch) || !parse_u32(fields[5], parsed.sdk_major) ||
      !parse_u32(fields[6], parsed.sdk_minor) || !parse_u32(fields[7], parsed.sdk_patch) ||
      !parse_u64(fields[8], parsed.capabilities)) {
    return false;
  }
  identity = std::move(parsed);
  return true;
}

/**
 * @brief Report whether an identifier already appears in a list.
 * @param entries List to search.
 * @param identifier Identifier to look for.
 * @return True when a matching entry exists.
 */
bool contains_identifier(const std::vector<PluginIdentity> &entries,
                         const std::string &identifier) noexcept {
  return std::any_of(entries.begin(), entries.end(), [&identifier](const PluginIdentity &entry) {
    return entry.identifier == identifier;
  });
}

/**
 * @brief Build an authorization result.
 * @param status Decision category.
 * @param reason Static explanation.
 * @param authorized Whether execution is permitted.
 * @param newly_trusted Whether this call persisted a new entry.
 * @param previously_trusted Whether the entry was already trusted.
 * @return Fully populated authorization result.
 */
TrustAuthorization authorization(TrustStatus status, const char *reason, bool authorized,
                                 bool newly_trusted = false,
                                 bool previously_trusted = false) noexcept {
  TrustAuthorization result;
  result.status = status;
  result.reason = reason;
  result.authorized = authorized;
  result.newly_trusted = newly_trusted;
  result.previously_trusted = previously_trusted;
  return result;
}

} // namespace

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
PluginIdentity identity_from_metadata(const AdapterMetadata &metadata) {
  PluginIdentity identity;
  identity.identifier = metadata.identifier;
  identity.name = metadata.name;
  identity.adapter_major = metadata.adapter_major;
  identity.adapter_minor = metadata.adapter_minor;
  identity.adapter_patch = metadata.adapter_patch;
  identity.sdk_major = metadata.sdk_major;
  identity.sdk_minor = metadata.sdk_minor;
  identity.sdk_patch = metadata.sdk_patch;
  identity.capabilities = metadata.capabilities;
  return identity;
}

StoreStatus MemoryTrustStore::load(std::vector<PluginIdentity> &entries) const noexcept {
  try {
    entries = entries_;
    return success();
  } catch (...) {
    return failure(ztick_status_internal_error, "cannot read in-memory trusted list");
  }
}

StoreStatus MemoryTrustStore::save(const std::vector<PluginIdentity> &entries) const noexcept {
  try {
    std::vector<PluginIdentity> validated;
    validated.reserve(entries.size());
    for (const PluginIdentity &entry : entries) {
      if (!is_valid_identity(entry)) {
        return failure(ztick_status_invalid_argument, "malformed trusted-list entry");
      }
      if (contains_identifier(validated, entry.identifier)) {
        return failure(ztick_status_invalid_argument, "duplicate trusted-list identifier");
      }
      validated.push_back(entry);
    }
    entries_ = std::move(validated);
    return success();
  } catch (...) {
    return failure(ztick_status_internal_error, "cannot update in-memory trusted list");
  }
}

FileTrustStore::FileTrustStore(std::string path) {
  if (path.empty()) {
    const std::optional<std::string> resolved = default_trust_list_path();
    if (resolved.has_value()) {
      path_ = *resolved;
    }
  } else {
    path_ = std::move(path);
  }
}

StoreStatus FileTrustStore::load(std::vector<PluginIdentity> &entries) const noexcept {
  try {
    entries.clear();
    if (path_.empty()) {
      return failure(ztick_status_invalid_argument, "trusted-plugin list path unavailable");
    }
    std::ifstream stream(path_);
    if (!stream.is_open()) {
      std::error_code ec;
      if (!std::filesystem::exists(path_, ec) && !ec) {
        return success();
      }
      return failure(ztick_status_internal_error, "cannot open trusted-plugin list");
    }
    std::string line;
    if (!std::getline(stream, line) || line != kListHeader) {
      return failure(ztick_status_internal_error, "malformed trusted-plugin list header");
    }
    while (std::getline(stream, line)) {
      if (line.empty()) {
        continue;
      }
      PluginIdentity entry;
      if (!parse_identity(line, entry)) {
        return failure(ztick_status_internal_error, "malformed trusted-plugin list entry");
      }
      if (!contains_identifier(entries, entry.identifier)) {
        entries.push_back(std::move(entry));
      }
    }
    if (stream.bad()) {
      return failure(ztick_status_internal_error, "cannot read trusted-plugin list");
    }
    return success();
  } catch (...) {
    return failure(ztick_status_internal_error, "cannot read trusted-plugin list");
  }
}

StoreStatus FileTrustStore::save(const std::vector<PluginIdentity> &entries) const noexcept {
  try {
    if (path_.empty()) {
      return failure(ztick_status_invalid_argument, "trusted-plugin list path unavailable");
    }
    std::ostringstream document;
    document << kListHeader << '\n';
    std::vector<PluginIdentity> validated;
    validated.reserve(entries.size());
    for (const PluginIdentity &entry : entries) {
      std::string line;
      if (!serialize_identity(entry, line)) {
        return failure(ztick_status_invalid_argument, "malformed trusted-list entry");
      }
      if (contains_identifier(validated, entry.identifier)) {
        return failure(ztick_status_invalid_argument, "duplicate trusted-list identifier");
      }
      validated.push_back(entry);
      document << line << '\n';
    }

    const std::filesystem::path target(path_);
    const std::filesystem::path parent = target.parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        return failure(ztick_status_internal_error, "cannot create trusted-plugin list directory");
      }
    }

    const std::string temporary = path_ + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return failure(ztick_status_internal_error, "cannot create trusted-list temporary file");
    }
    output << document.str();
    output.close();
    if (!output) {
      std::remove(temporary.c_str());
      return failure(ztick_status_internal_error, "cannot write trusted-plugin list");
    }
    if (std::rename(temporary.c_str(), path_.c_str()) != 0) {
      std::remove(temporary.c_str());
      return failure(ztick_status_internal_error, "cannot replace trusted-plugin list");
    }
    return success();
  } catch (...) {
    return failure(ztick_status_internal_error, "cannot write trusted-plugin list");
  }
}

TrustManager::TrustManager(const TrustStore &store, const ConsentPrompter *prompter) noexcept
    : store_(store), prompter_(prompter) {}

TrustAuthorization TrustManager::authorize(const PluginIdentity &identity) noexcept {
  if (!is_valid_identity(identity)) {
    return authorization(TrustStatus::invalid_identity, "malformed plugin identity", false);
  }
  try {
    std::vector<PluginIdentity> entries;
    const StoreStatus loaded = store_.load(entries);
    if (!loaded.ok()) {
      return authorization(TrustStatus::store_unavailable,
                           reason_or(loaded.reason, "cannot read trusted-plugin list"), false);
    }
    if (contains_identifier(entries, identity.identifier)) {
      return authorization(TrustStatus::authorized, "already trusted", true, false, true);
    }
    if (prompter_ == nullptr) {
      return authorization(TrustStatus::confirmation_unavailable,
                           "first-run confirmation unavailable", false);
    }
    const ConsentDecision decision = prompter_->confirm_first_run(identity);
    if (decision == ConsentDecision::deny) {
      return authorization(TrustStatus::denied, "first-run confirmation denied", false);
    }
    if (decision != ConsentDecision::approve) {
      return authorization(TrustStatus::confirmation_unavailable,
                           "first-run confirmation unavailable", false);
    }
    entries.push_back(identity);
    const StoreStatus saved = store_.save(entries);
    if (!saved.ok()) {
      return authorization(TrustStatus::store_unavailable,
                           reason_or(saved.reason, "cannot update trusted-plugin list"), false);
    }
    return authorization(TrustStatus::authorized, "first-run confirmation approved", true, true,
                         false);
  } catch (...) {
    return authorization(TrustStatus::internal_error, "trust decision failed", false);
  }
}

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
TrustListResult TrustManager::trusted_plugins() const noexcept {
  TrustListResult result;
  try {
    result.status = store_.load(result.entries);
    if (!result.status.ok()) {
      result.entries.clear();
    }
  } catch (...) {
    result.status = failure(ztick_status_internal_error, "cannot read trusted-plugin list");
    result.entries.clear();
  }
  return result;
}

// Consumed by in-tree tests and future host front ends; cppcheck scans src/ but not tests/.
// cppcheck-suppress unusedFunction
bool TrustManager::revoke(const std::string &identifier, StoreStatus &status) noexcept {
  status = failure(ztick_status_internal_error, "not attempted");
  if (!is_safe_field(identifier)) {
    status = failure(ztick_status_invalid_argument, "malformed plugin identifier");
    return false;
  }
  try {
    std::vector<PluginIdentity> entries;
    status = store_.load(entries);
    if (!status.ok()) {
      return false;
    }
    const auto position =
        std::find_if(entries.begin(), entries.end(), [&identifier](const PluginIdentity &entry) {
          return entry.identifier == identifier;
        });
    if (position == entries.end()) {
      status = success();
      return false;
    }
    entries.erase(position);
    status = store_.save(entries);
    return status.ok();
  } catch (...) {
    status = failure(ztick_status_internal_error, "cannot revoke trusted plugin");
    return false;
  }
}

} // namespace ztick::plugin
