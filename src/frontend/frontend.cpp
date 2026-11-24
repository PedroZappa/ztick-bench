// SPDX-License-Identifier: MIT
/**
 * @file frontend.cpp
 * @brief C-linkage implementation of the private front-end control boundary.
 *
 * The implementation marshals C11 calls into private C++ services, catches every
 * exception before it can cross the boundary and copies all returned data into
 * caller-owned buffers. Screen behavior is owned by TUI_SPECIFICATION; this file
 * only supplies the operations described by SDK_SPECIFICATION §14.
 */

#include <ztick/frontend.h>

#include <ztick/atomic_writer.hpp>
#include <ztick/cli.hpp>
#include <ztick/config_document.hpp>
#include <ztick/csv_projection.hpp>
#include <ztick/json.hpp>
#include <ztick/plugin/discovery.hpp>
#include <ztick/plugin/trust.hpp>
#include <ztick/result_document.hpp>
#include <ztick/run_command.hpp>
#include <ztick/run_session.hpp>
#include <ztick/sdk.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @brief Private implementation namespace for the front-end C boundary.
 */
namespace ztick::frontend {
/**
 * @brief Internal helpers that never appear in the C boundary header.
 */
namespace {

/**
 * @brief Opaque state owned by one front-end session.
 */
struct SessionState {
  /**
   * @brief Construct session state with a trusted-plugin store.
   * @param trust_store_path Store path, or empty for the platform default.
   */
  explicit SessionState(std::string trust_store_path) : trust_store(std::move(trust_store_path)) {}

  /** @brief Retained portable config document, valid only when @ref has_config is set. */
  ztick::ConfigValue config{ztick::ConfigValue::null()};
  /** @brief Whether @ref config holds a parsed document. */
  bool has_config{false};
  /** @brief Most recent human-readable error, empty after success. */
  std::string error;
  /** @brief Trusted-plugin store used by query and consent calls. */
  ztick::plugin::FileTrustStore trust_store;
  /** @brief Queued copied progress events in delivery order. */
  std::deque<ztick_frontend_event> events;
  /** @brief Optional C progress callback. */
  ztick_frontend_progress_callback callback{nullptr};
  /** @brief Opaque context passed to the progress callback. */
  void *user_data{nullptr};
};

/** @brief Owning registry of live session states keyed by opaque handle values. */
using SessionRegistry = std::unordered_map<ztick_frontend_session *, std::unique_ptr<SessionState>>;

/**
 * @brief Return the registry guarding session creation, lookup and destruction.
 * @return Process-lifetime mutex shared by all front-end sessions.
 */
std::mutex &session_registry_mutex() noexcept {
  static std::mutex mutex;
  return mutex;
}

/**
 * @brief Return the owning registry of live session states.
 * @return Process-lifetime registry shared by all front-end sessions.
 */
SessionRegistry &session_registry() {
  static SessionRegistry registry;
  return registry;
}

/**
 * @brief Return the next non-dereferenceable session handle value.
 * @return Process-lifetime monotonically increasing handle counter.
 */
std::uintptr_t &next_session_id() noexcept {
  static std::uintptr_t next = 1U;
  return next;
}

/**
 * @brief Allocate one never-reused opaque session handle.
 * @return Non-NULL handle value, or NULL when the counter is exhausted.
 * @note The value is cast from an integer and only ever used as a registry key;
 *       it is never dereferenced or deleted, so a destroyed handle cannot alias
 *       a later session through allocator address reuse.
 */
ztick_frontend_session *allocate_session_handle() noexcept {
  std::uintptr_t &next = next_session_id();
  if (next == std::numeric_limits<std::uintptr_t>::max()) {
    return nullptr;
  }
  return reinterpret_cast<ztick_frontend_session *>(next++);
}

/**
 * @brief Resolve an opaque session handle to its live implementation state.
 * @param session Handle to resolve; NULL is allowed.
 * @return Owning state pointer, or NULL when the handle is NULL or destroyed.
 * @note The registry lookup avoids dereferencing a stale handle after destruction.
 */
SessionState *state_of(ztick_frontend_session *session) noexcept {
  if (session == nullptr) {
    return nullptr;
  }
  const std::lock_guard<std::mutex> lock(session_registry_mutex());
  const auto entry = session_registry().find(session);
  return entry == session_registry().end() ? nullptr : entry->second.get();
}

/**
 * @brief Resolve a const opaque session handle to its live implementation state.
 * @param session Handle to resolve; NULL is allowed.
 * @return Const owning state pointer, or NULL when the handle is NULL or destroyed.
 * @note The pointer value is used only as a registry key; the handle is not modified.
 */
const SessionState *state_of(const ztick_frontend_session *session) noexcept {
  return state_of(const_cast<ztick_frontend_session *>(session));
}

/**
 * @brief Fixed consent prompter that encodes an explicit C-level decision.
 */
class FixedConsentPrompter final : public ztick::plugin::ConsentPrompter {
public:
  /**
   * @brief Construct a prompter with one fixed decision.
   * @param approve True to approve, false to deny.
   */
  explicit FixedConsentPrompter(bool approve) noexcept : approve_(approve) {}

  /**
   * @brief Return the configured decision without executing plugin code.
   * @param identity Identity that the caller displayed to the user.
   * @return Approval or denial matching the C-level decision.
   */
  ztick::plugin::ConsentDecision
  confirm_first_run(const ztick::plugin::PluginIdentity &identity) const noexcept override {
    (void)identity;
    return approve_ ? ztick::plugin::ConsentDecision::approve
                    : ztick::plugin::ConsentDecision::deny;
  }

private:
  /** @brief Decision returned for every prompt. */
  bool approve_;
};

/**
 * @brief Validate a caller-owned struct header.
 * @param pointer Struct pointer to check.
 * @param reported_size Caller-provided `size` field.
 * @param expected_size Compile-time sizeof value for the struct.
 * @param version Caller-provided layout version.
 * @return True when @p pointer is non-null, @p reported_size matches @p expected_size and
 *         @p version is current.
 */
bool valid_header(const void *pointer, std::uint32_t reported_size, std::size_t expected_size,
                  std::uint32_t version) noexcept {
  return pointer != nullptr && reported_size == expected_size &&
         version == ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
}

/**
 * @brief Record a human-readable failure reason on a session.
 * @param session Session to update, or NULL when no session is available.
 * @param reason Explanation to copy.
 */
void set_error(SessionState *session, std::string_view reason) {
  if (session != nullptr) {
    session->error.assign(reason);
  }
}

/**
 * @brief Clear the recorded error after a successful operation.
 * @param session Session to update.
 */
void clear_error(SessionState *session) noexcept {
  if (session != nullptr) {
    session->error.clear();
  }
}

/**
 * @brief Copy a string into a caller-owned NUL-terminated buffer.
 * @param text Text to copy.
 * @param buffer Destination buffer.
 * @param capacity Byte capacity of @p buffer.
 * @param out_length Optional receiver for the text length excluding the NUL.
 * @param error Optional receiver for an explanation when the buffer is invalid.
 * @return Success, or `ztick_status_invalid_argument` when arguments or capacity are invalid.
 */
std::int32_t copy_string(std::string_view text, char *buffer, std::size_t capacity,
                         std::size_t *out_length, std::string *error) {
  if (out_length != nullptr) {
    *out_length = text.size();
  }
  if (buffer == nullptr || capacity == 0U || text.size() >= capacity) {
    const char *reason = buffer == nullptr ? "output buffer is NULL" : "output buffer is too small";
    if (error != nullptr) {
      error->assign(reason);
    }
    return ztick_status_invalid_argument;
  }
  std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = '\0';
  return ztick_status_success;
}

/**
 * @brief Map a CLI exit code onto the shared `ztick_status` inventory.
 * @param code CLI exit code to map.
 * @return Invalid argument for usage errors, unsupported for unavailable commands,
 *         validation failure for regressions and processing failure for runtime failures.
 */
std::int32_t map_exit_code(ztick::cli::ExitCode code) noexcept {
  switch (code) {
  case ztick::cli::ExitCode::success:
    return ztick_status_success;
  case ztick::cli::ExitCode::usage_error:
    return ztick_status_invalid_argument;
  case ztick::cli::ExitCode::unavailable:
    return ztick_status_unsupported;
  case ztick::cli::ExitCode::failure:
    return ztick_status_processing_failure;
  case ztick::cli::ExitCode::regression:
    return ztick_status_validation_failure;
  }
  return ztick_status_internal_error;
}

/**
 * @brief Queue one progress event and invoke the registered callback.
 * @param session Session that owns the event queue.
 * @param kind Event kind code.
 * @param state Run state code.
 * @param test_point One-based test point, or zero.
 * @param timing_level Timing level code.
 * @param isolation_mode Isolation mode code.
 * @param message Human-readable context, truncated to the event message field.
 */
void enqueue_event(SessionState *session, std::int32_t kind, std::int32_t state,
                   std::uint32_t test_point, std::int32_t timing_level, std::int32_t isolation_mode,
                   std::string_view message) {
  ztick_frontend_event event{};
  event.size = sizeof(event);
  event.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
  event.kind = kind;
  event.state = state;
  event.test_point = test_point;
  event.timing_level = timing_level;
  event.isolation_mode = isolation_mode;
  const std::size_t copy_size =
      std::min<std::size_t>(message.size(), ZTICK_FRONTEND_EVENT_MESSAGE_SIZE - 1U);
  if (copy_size > 0U) {
    std::memcpy(event.message, message.data(), copy_size);
  }
  event.message[copy_size] = '\0';

  session->events.push_back(event);
  if (session->callback != nullptr) {
    session->callback(&event, session->user_data);
  }
}

/**
 * @brief Parse and retain one portable config document.
 * @param session Session to update.
 * @param text Complete config JSON.
 * @return Success or a status explaining the parse, schema or semantic failure.
 * @note A document the core planner would reject is never retained, so a front
 *       end cannot later report it as runnable.
 */
std::int32_t parse_config_text(SessionState *session, std::string_view text) {
  auto parsed = ztick::parse_config_document(text);
  if (!parsed.ok) {
    set_error(session, parsed.error);
    return parsed.offset == 0U ? ztick_status_validation_failure : ztick_status_invalid_argument;
  }
  std::string semantic_error;
  if (!ztick::validate_run_config(parsed.document, &semantic_error)) {
    set_error(session, semantic_error);
    return ztick_status_validation_failure;
  }
  session->config = std::move(parsed.document);
  session->has_config = true;
  clear_error(session);
  enqueue_event(session, ztick_frontend_event_state_change, ztick_frontend_state_configuring, 0U,
                ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, "config parsed");
  return ztick_status_success;
}

/**
 * @brief Serialize the retained config document as compact JSON.
 * @param session Session holding a parsed config.
 * @param output Receiver for the serialized text.
 * @return Success, invalid argument when no config is retained, or validation failure.
 */
std::int32_t serialize_config(SessionState *session, std::string &output) {
  if (!session->has_config) {
    set_error(session, "no config is retained by this session");
    return ztick_status_invalid_argument;
  }
  const auto written = ztick::write_config_document(session->config);
  if (!written.ok) {
    set_error(session, written.error);
    return ztick_status_validation_failure;
  }
  output = written.text;
  clear_error(session);
  return ztick_status_success;
}

/**
 * @brief Read a complete UTF-8 text file.
 * @param path File path to read.
 * @param output Receiver for the file contents.
 * @param error Receiver for a failure explanation.
 * @return True on success; false leaves @p output empty.
 */
bool read_text_file(const char *path, std::string &output, std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "config file could not be opened";
    return false;
  }
  output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (input.bad()) {
    error = "config file could not be read";
    output.clear();
    return false;
  }
  return true;
}

/**
 * @brief Dispatch one CLI command through the private boundary.
 * @param session Session to use.
 * @param args Command tokens excluding the executable name and format option.
 * @param out_json Destination buffer for the JSON envelope.
 * @param capacity Byte capacity of @p out_json.
 * @param out_length Optional receiver for the copied JSON length.
 * @return CLI status mapped to `ztick_status`, or invalid argument for a short buffer.
 */
std::int32_t dispatch_command(SessionState *session, const std::vector<std::string_view> &args,
                              char *out_json, std::size_t capacity, std::size_t *out_length) {
  std::vector<std::string_view> full_args;
  full_args.reserve(args.size() + 2U);
  full_args.emplace_back("--format");
  full_args.emplace_back("json");
  std::copy(args.begin(), args.end(), std::back_inserter(full_args));

  enqueue_event(session, ztick_frontend_event_state_change, ztick_frontend_state_running, 0U,
                ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, "command started");
  const auto result = ztick::cli::dispatch(full_args);
  const std::int32_t status = map_exit_code(result.code);
  if (status == ztick_status_success) {
    clear_error(session);
  } else {
    set_error(session, result.error.empty() ? ztick_status_message(status) : result.error);
  }
  const auto copied = copy_string(result.output, out_json, capacity, out_length, &session->error);
  if (status == ztick_status_success) {
    enqueue_event(session, ztick_frontend_event_completion, ztick_frontend_state_complete, 0U,
                  ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown,
                  "command completed");
  } else {
    enqueue_event(session, ztick_frontend_event_completion, ztick_frontend_state_failed, 0U,
                  ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, session->error);
  }
  return copied == ztick_status_success ? status : copied;
}

/**
 * @brief Run one operation under the C ABI exception guard.
 * @param session Session whose error string receives exception details.
 * @param operation Operation to execute.
 * @return The operation's status, or `ztick_status_internal_error` on an exception.
 */
template <typename Operation>
std::int32_t guard(SessionState *session, Operation &&operation) noexcept {
  try {
    return operation();
  } catch (const std::exception &error) {
    set_error(session, error.what());
    return ztick_status_internal_error;
  } catch (...) {
    set_error(session, "unknown internal error");
    return ztick_status_internal_error;
  }
}

/**
 * @brief Resolve a caller handle and run one operation under the exception guard.
 * @param session Opaque session handle; must refer to a live session.
 * @param operation Operation receiving the resolved session state.
 * @return Invalid argument for a NULL or destroyed handle, otherwise the operation's
 *         status or `ztick_status_internal_error` on an exception.
 */
template <typename Operation>
std::int32_t guard_session(ztick_frontend_session *handle, Operation &&operation) noexcept {
  SessionState *state = state_of(handle);
  if (state == nullptr) {
    return ztick_status_invalid_argument;
  }
  return guard(state, [&]() -> std::int32_t { return operation(state); });
}

/**
 * @brief Resolve a const caller handle and run one read-only operation under the guard.
 * @param session Opaque session handle; must refer to a live session.
 * @param operation Operation receiving the resolved const session state.
 * @return Invalid argument for a NULL or destroyed handle, otherwise the operation's
 *         status or `ztick_status_internal_error` on an exception.
 */
template <typename Operation>
std::int32_t guard_session(const ztick_frontend_session *handle, Operation &&operation) noexcept {
  const SessionState *state = state_of(handle);
  if (state == nullptr) {
    return ztick_status_invalid_argument;
  }
  try {
    return operation(state);
  } catch (...) {
    return ztick_status_internal_error;
  }
}

} // namespace
} // namespace ztick::frontend

extern "C" {

int32_t ztick_frontend_get_version(ztick_frontend_version *out_version) {
  return ztick::frontend::guard(nullptr, [&]() -> std::int32_t {
    if (out_version == nullptr) {
      return ztick_status_invalid_argument;
    }
    if (!ztick::frontend::valid_header(out_version, out_version->size, sizeof(*out_version),
                                       out_version->version)) {
      return ztick_status_invalid_argument;
    }
    out_version->major = ZTICK_FRONTEND_VERSION_MAJOR;
    out_version->minor = ZTICK_FRONTEND_VERSION_MINOR;
    out_version->patch = ZTICK_FRONTEND_VERSION_PATCH;
    return ztick_status_success;
  });
}

const char *ztick_frontend_status_message(int32_t status) { return ztick_status_message(status); }

ztick_frontend_session *
ztick_frontend_session_create(const ztick_frontend_session_options *options) {
  try {
    std::string trust_store_path;
    if (options != nullptr) {
      if (options->size != sizeof(*options) ||
          options->version != ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION) {
        return nullptr;
      }
      if (options->trust_store_path != nullptr) {
        trust_store_path = options->trust_store_path;
      }
    }
    auto state = std::make_unique<ztick::frontend::SessionState>(std::move(trust_store_path));
    const std::lock_guard<std::mutex> lock(ztick::frontend::session_registry_mutex());
    auto &registry = ztick::frontend::session_registry();
    ztick_frontend_session *const handle = ztick::frontend::allocate_session_handle();
    if (handle == nullptr) {
      return nullptr;
    }
    registry.emplace(handle, std::move(state));
    return handle;
  } catch (...) {
    return nullptr;
  }
}

void ztick_frontend_session_destroy(ztick_frontend_session *session) {
  if (session == nullptr) {
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(ztick::frontend::session_registry_mutex());
    auto &registry = ztick::frontend::session_registry();
    const auto entry = registry.find(session);
    if (entry == registry.end()) {
      return;
    }
    registry.erase(entry);
  }
}

int32_t ztick_frontend_last_error(const ztick_frontend_session *session, char *buffer,
                                  size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(session, [&](const ztick::frontend::SessionState *state) {
    return ztick::frontend::copy_string(state->error, buffer, capacity, out_length, nullptr);
  });
}

int32_t ztick_frontend_config_parse(ztick_frontend_session *session, const char *text,
                                    size_t length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || (text == nullptr && length != 0U)) {
          return ztick_status_invalid_argument;
        }
        const std::string_view view =
            text == nullptr ? std::string_view{} : std::string_view(text, length);
        return ztick::frontend::parse_config_text(state, view);
      });
}

int32_t ztick_frontend_config_load(ztick_frontend_session *session, const char *path) {
  return ztick::frontend::guard_session(session,
                                        [&](ztick::frontend::SessionState *state) -> std::int32_t {
                                          if (session == nullptr || path == nullptr) {
                                            return ztick_status_invalid_argument;
                                          }
                                          std::string text;
                                          std::string error;
                                          if (!ztick::frontend::read_text_file(path, text, error)) {
                                            ztick::frontend::set_error(state, error);
                                            return ztick_status_processing_failure;
                                          }
                                          return ztick::frontend::parse_config_text(state, text);
                                        });
}

int32_t ztick_frontend_config_validate(ztick_frontend_session *session) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || !state->has_config) {
          ztick::frontend::set_error(state, "no config is retained by this session");
          return ztick_status_invalid_argument;
        }
        ztick::frontend::enqueue_event(
            state, ztick_frontend_event_state_change, ztick_frontend_state_validating, 0U,
            ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, "config validation");
        std::string error;
        if (!ztick::validate_run_config(state->config, &error)) {
          ztick::frontend::set_error(state, error);
          return ztick_status_validation_failure;
        }
        ztick::frontend::clear_error(state);
        return ztick_status_success;
      });
}

int32_t ztick_frontend_config_override(ztick_frontend_session *session,
                                       const char *const *assignments, size_t count, char *buffer,
                                       size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || (assignments == nullptr && count != 0U) || !state->has_config) {
          ztick::frontend::set_error(state, "no config is retained by this session");
          return ztick_status_invalid_argument;
        }
        std::vector<std::string> overrides;
        overrides.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
          if (assignments[index] == nullptr) {
            ztick::frontend::set_error(state, "override assignment is NULL");
            return ztick_status_invalid_argument;
          }
          overrides.emplace_back(assignments[index]);
        }
        auto candidate = state->config;
        std::string error;
        if (!ztick::cli::apply_config_overrides(candidate, overrides, &error)) {
          ztick::frontend::set_error(state, error);
          return ztick_status_validation_failure;
        }
        if (!ztick::validate_run_config(candidate, &error)) {
          ztick::frontend::set_error(state, error);
          return ztick_status_validation_failure;
        }
        state->config = std::move(candidate);
        std::string text;
        const std::int32_t status = ztick::frontend::serialize_config(state, text);
        if (status != ztick_status_success) {
          return status;
        }
        ztick::frontend::enqueue_event(
            state, ztick_frontend_event_state_change, ztick_frontend_state_configuring, 0U,
            ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, "config overridden");
        return ztick::frontend::copy_string(text, buffer, capacity, out_length, &state->error);
      });
}

int32_t ztick_frontend_config_serialize(ztick_frontend_session *session, char *buffer,
                                        size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr) {
          return ztick_status_invalid_argument;
        }
        std::string text;
        const std::int32_t status = ztick::frontend::serialize_config(state, text);
        if (status != ztick_status_success) {
          return status;
        }
        return ztick::frontend::copy_string(text, buffer, capacity, out_length, &state->error);
      });
}

int32_t ztick_frontend_config_save(ztick_frontend_session *session, const char *path) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || path == nullptr) {
          return ztick_status_invalid_argument;
        }
        std::string text;
        const std::int32_t status = ztick::frontend::serialize_config(state, text);
        if (status != ztick_status_success) {
          return status;
        }
        const auto written = ztick::write_file_atomic(std::filesystem::path(path), text);
        if (!written.ok) {
          ztick::frontend::set_error(state, written.error);
          return ztick_status_processing_failure;
        }
        ztick::frontend::clear_error(state);
        ztick::frontend::enqueue_event(
            state, ztick_frontend_event_state_change, ztick_frontend_state_configuring, 0U,
            ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, "config saved");
        return ztick_status_success;
      });
}

int32_t ztick_frontend_list(ztick_frontend_session *session, char *out_json, size_t capacity,
                            size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"list"}, out_json, capacity, out_length);
      });
}

int32_t ztick_frontend_system(ztick_frontend_session *session, char *out_json, size_t capacity,
                              size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"system"}, out_json, capacity, out_length);
      });
}

int32_t ztick_frontend_run(ztick_frontend_session *session, const char *config_path, char *out_json,
                           size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || config_path == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"run", config_path}, out_json, capacity,
                                                 out_length);
      });
}

int32_t ztick_frontend_report(ztick_frontend_session *session, const char *result_path,
                              char *out_json, size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || result_path == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"report", result_path}, out_json, capacity,
                                                 out_length);
      });
}

int32_t ztick_frontend_compare(ztick_frontend_session *session, const char *baseline_path,
                               const char *candidate_path, char *out_json, size_t capacity,
                               size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || baseline_path == nullptr || candidate_path == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"compare", baseline_path, candidate_path},
                                                 out_json, capacity, out_length);
      });
}

int32_t ztick_frontend_result_read(ztick_frontend_session *session, const char *result_path,
                                   char *out_json, size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || result_path == nullptr) {
          return ztick_status_invalid_argument;
        }
        const auto parsed = ztick::read_result_document(std::filesystem::path(result_path));
        if (!parsed.ok) {
          ztick::frontend::set_error(state, parsed.error);
          return ztick_status_processing_failure;
        }
        const auto written = ztick::json_write(parsed.document);
        if (!written.ok) {
          ztick::frontend::set_error(state, written.error);
          return ztick_status_validation_failure;
        }
        ztick::frontend::clear_error(state);
        return ztick::frontend::copy_string(written.text, out_json, capacity, out_length,
                                            &state->error);
      });
}

int32_t ztick_frontend_result_export_csv(ztick_frontend_session *session, const char *result_path,
                                         const char *row_mode, char *out_csv, size_t capacity,
                                         size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || result_path == nullptr) {
          return ztick_status_invalid_argument;
        }
        ztick::CsvExportOptions options;
        if (row_mode != nullptr) {
          const auto parsed_mode = ztick::parse_csv_row_mode(row_mode);
          if (!parsed_mode) {
            ztick::frontend::set_error(state, "unknown CSV row mode");
            return ztick_status_invalid_argument;
          }
          options.row_mode = *parsed_mode;
        }
        const auto parsed = ztick::read_result_document(std::filesystem::path(result_path));
        if (!parsed.ok) {
          ztick::frontend::set_error(state, parsed.error);
          return ztick_status_processing_failure;
        }
        const auto projected = ztick::project_result_document(parsed.document, options);
        if (!projected.ok) {
          ztick::frontend::set_error(state, projected.error);
          return ztick_status_validation_failure;
        }
        ztick::frontend::clear_error(state);
        return ztick::frontend::copy_string(projected.text, out_csv, capacity, out_length,
                                            &state->error);
      });
}

int32_t ztick_frontend_plugin_list(ztick_frontend_session *session, char *out_json, size_t capacity,
                                   size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"plugin", "list"}, out_json, capacity,
                                                 out_length);
      });
}

int32_t ztick_frontend_plugin_inspect(ztick_frontend_session *session, const char *plugin_path,
                                      char *out_json, size_t capacity, size_t *out_length) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || plugin_path == nullptr) {
          return ztick_status_invalid_argument;
        }
        return ztick::frontend::dispatch_command(state, {"plugin", "inspect", plugin_path},
                                                 out_json, capacity, out_length);
      });
}

int32_t ztick_frontend_trust_query(ztick_frontend_session *session, const char *identifier,
                                   int32_t *out_trusted) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || identifier == nullptr || *identifier == '\0' ||
            out_trusted == nullptr) {
          return ztick_status_invalid_argument;
        }
        const auto list = ztick::plugin::read_trust_list(state->trust_store);
        if (!list.status.ok()) {
          ztick::frontend::set_error(state, list.status.reason);
          return list.status.code == ztick_status_success ? ztick_status_processing_failure
                                                          : list.status.code;
        }
        *out_trusted = std::any_of(list.entries.begin(), list.entries.end(),
                                   [identifier](const ztick::plugin::PluginIdentity &entry) {
                                     return entry.identifier == identifier;
                                   })
                           ? 1
                           : 0;
        ztick::frontend::clear_error(state);
        return ztick_status_success;
      });
}

int32_t ztick_frontend_trust_consent(ztick_frontend_session *session, const char *identifier,
                                     int32_t granted) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || identifier == nullptr || *identifier == '\0' ||
            (granted != 0 && granted != 1)) {
          return ztick_status_invalid_argument;
        }
        ztick::plugin::PluginIdentity identity;
        identity.identifier = identifier;
        identity.name = identifier;
        ztick::frontend::FixedConsentPrompter prompter(granted == 1);
        ztick::plugin::TrustManager manager(state->trust_store, &prompter);
        const auto authorization = manager.authorize(identity);
        if (granted == 1 && !authorization.authorized) {
          ztick::frontend::set_error(state, authorization.reason);
          return ztick_status_processing_failure;
        }
        ztick::frontend::clear_error(state);
        ztick::frontend::enqueue_event(
            state, ztick_frontend_event_state_change,
            granted == 1 ? ztick_frontend_state_configuring : ztick_frontend_state_warning, 0U,
            ztick_frontend_timing_unknown, ztick_frontend_isolation_unknown, authorization.reason);
        return ztick_status_success;
      });
}

int32_t ztick_frontend_progress_subscribe(ztick_frontend_session *session,
                                          ztick_frontend_progress_callback callback,
                                          void *user_data) {
  return ztick::frontend::guard_session(session,
                                        [&](ztick::frontend::SessionState *state) -> std::int32_t {
                                          if (session == nullptr) {
                                            return ztick_status_invalid_argument;
                                          }
                                          state->callback = callback;
                                          state->user_data = user_data;
                                          ztick::frontend::clear_error(state);
                                          return ztick_status_success;
                                        });
}

int32_t ztick_frontend_progress_poll(ztick_frontend_session *session,
                                     ztick_frontend_event *out_event, int32_t *out_available) {
  return ztick::frontend::guard_session(
      session, [&](ztick::frontend::SessionState *state) -> std::int32_t {
        if (session == nullptr || out_event == nullptr || out_available == nullptr ||
            out_event->size != sizeof(*out_event) ||
            out_event->version != ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION) {
          return ztick_status_invalid_argument;
        }
        auto &queue = state->events;
        if (queue.empty()) {
          *out_available = 0;
          ztick::frontend::clear_error(state);
          return ztick_status_success;
        }
        *out_event = queue.front();
        queue.pop_front();
        *out_available = 1;
        ztick::frontend::clear_error(state);
        return ztick_status_success;
      });
}

int32_t ztick_frontend_progress_drain(ztick_frontend_session *session,
                                      ztick_frontend_progress_callback callback, void *user_data,
                                      int32_t *out_count) {
  return ztick::frontend::guard_session(session,
                                        [&](ztick::frontend::SessionState *state) -> std::int32_t {
                                          if (session == nullptr || callback == nullptr) {
                                            return ztick_status_invalid_argument;
                                          }
                                          std::deque<ztick_frontend_event> events;
                                          events.swap(state->events);
                                          for (const auto &event : events) {
                                            callback(&event, user_data);
                                          }
                                          if (out_count != nullptr) {
                                            *out_count = static_cast<std::int32_t>(events.size());
                                          }
                                          ztick::frontend::clear_error(state);
                                          return ztick_status_success;
                                        });
}

} // extern "C"
