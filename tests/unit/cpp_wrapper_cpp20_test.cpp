// SPDX-License-Identifier: MIT
/**
 * @file cpp_wrapper_cpp20_test.cpp
 * @brief Deterministic C++20 checks for the header-only SDK wrappers.
 *
 * The wrapper header is included first and on its own, proving it is
 * self-contained in namespace `ztick::sdk`. The checks cover status and version
 * conversion plus every observable path of the RAII handle, using a local fake
 * adapter so no SDK implementation is linked.
 */

#include <ztick/cpp/adapter.hpp>

#include <cstdio>
#include <type_traits>

/**
 * @brief Minimal concrete stand-in for the opaque C adapter handle.
 * @note Exists only in this test translation unit to observe destroy calls.
 */
struct ztick_adapter {
  /** Number of times the destroy callback was invoked. */
  int destroy_calls = 0;
};

/**
 * @brief Fake C ABI destroy callback used by the RAII handle tests.
 * @param adapter Fake adapter to destroy; may be NULL.
 * @return Always `ztick_status_success`.
 */
extern "C" int32_t ztick_test_destroy(ztick_adapter *adapter) {
  if (adapter != nullptr) {
    ++adapter->destroy_calls;
  }
  return ztick_status_success;
}

namespace {

/** @brief Number of failed runtime checks. */
int g_failures = 0;

/**
 * @brief Record the result of one runtime check.
 * @param condition Condition expected to be true.
 * @param message Message printed when @p condition is false.
 */
void check(bool condition, const char *message) {
  if (!condition) {
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
  }
}

/**
 * @brief Build a minimal lifecycle table exposing the fake destroy callback.
 * @return A zero-initialized ops table with `destroy` set.
 */
ztick_adapter_ops make_ops() {
  ztick_adapter_ops ops{};
  ops.size = sizeof(ztick_adapter_ops);
  ops.version = 1;
  ops.destroy = &ztick_test_destroy;
  return ops;
}

} // namespace

// The build selects C++20 with extensions disabled; verify the effective mode.
static_assert(__cplusplus >= 202002L, "wrapper test requires C++20");

// Status values match the C ABI exactly and round-trip without loss.
static_assert(ztick::sdk::to_c(ztick::sdk::Status::success) == ztick_status_success);
static_assert(ztick::sdk::to_c(ztick::sdk::Status::unsupported) == ztick_status_unsupported);
static_assert(ztick::sdk::to_c(ztick::sdk::Status::incompatible) == ztick_status_incompatible);
static_assert(ztick::sdk::to_c(ztick::sdk::Status::invalid_argument) ==
              ztick_status_invalid_argument);
static_assert(ztick::sdk::to_c(ztick::sdk::Status::processing_failure) ==
              ztick_status_processing_failure);
static_assert(ztick::sdk::to_c(ztick::sdk::Status::validation_failure) ==
              ztick_status_validation_failure);
static_assert(ztick::sdk::to_c(ztick::sdk::Status::internal_error) == ztick_status_internal_error);
static_assert(ztick::sdk::status_from_c(ztick_status_success) == ztick::sdk::Status::success);
static_assert(ztick::sdk::status_from_c(ztick_status_internal_error) ==
              ztick::sdk::Status::internal_error);
static_assert(ztick::sdk::is_success(ztick::sdk::Status::success));
static_assert(!ztick::sdk::is_success(ztick::sdk::Status::unsupported));

// Version construction and the compiled-in SDK version are compile-time values.
static_assert(ztick::sdk::Version(1, 2, 3).major == 1);
static_assert(ztick::sdk::Version(1, 2, 3).minor == 2);
static_assert(ztick::sdk::Version(1, 2, 3).patch == 3);
static_assert(ztick::sdk::Version(ztick_version{0, 0, 4, 5, 6}).major == 4);
static_assert(ztick::sdk::Version(ztick_version{0, 0, 4, 5, 6}).minor == 5);
static_assert(ztick::sdk::Version(ztick_version{0, 0, 4, 5, 6}).patch == 6);
static_assert(ztick::sdk::kSdkVersion.major == ZTICK_SDK_VERSION_MAJOR);
static_assert(ztick::sdk::kSdkVersion.minor == ZTICK_SDK_VERSION_MINOR);
static_assert(ztick::sdk::kSdkVersion.patch == ZTICK_SDK_VERSION_PATCH);

// The RAII owner is move-only.
static_assert(!std::is_copy_constructible_v<ztick::sdk::AdapterHandle>);
static_assert(!std::is_copy_assignable_v<ztick::sdk::AdapterHandle>);
static_assert(std::is_move_constructible_v<ztick::sdk::AdapterHandle>);
static_assert(std::is_move_assignable_v<ztick::sdk::AdapterHandle>);
static_assert(std::is_nothrow_destructible_v<ztick::sdk::AdapterHandle>);

/**
 * @brief Exercise the standalone C++20 wrapper checks.
 * @return Zero when all checks pass, otherwise the number of failures.
 */
int main() {
  // Empty handle: queries are safe and reset/release do nothing.
  ztick::sdk::AdapterHandle empty;
  check(!empty, "default handle must be empty");
  check(empty.get() == nullptr, "default handle must have a null pointer");
  check(empty.release() == nullptr, "releasing an empty handle must return null");
  empty.reset();
  check(!empty, "reset on an empty handle must stay empty");

  // Owning handle: the destructor destroys exactly once.
  ztick_adapter owned;
  {
    ztick::sdk::AdapterHandle handle(&owned, make_ops());
    check(static_cast<bool>(handle), "owning handle must be non-empty");
    check(handle.get() == &owned, "get must return the owned pointer");
    check(owned.destroy_calls == 0, "construction must not destroy");
  }
  check(owned.destroy_calls == 1, "destructor must destroy exactly once");

  // Explicit reset destroys once; a second reset is harmless.
  ztick_adapter reset_adapter;
  ztick::sdk::AdapterHandle reset_handle(&reset_adapter, make_ops());
  reset_handle.reset();
  check(reset_adapter.destroy_calls == 1, "reset must destroy exactly once");
  check(!reset_handle, "reset must empty the handle");
  reset_handle.reset();
  check(reset_adapter.destroy_calls == 1, "repeated reset must not destroy again");

  // Release transfers ownership without destroying.
  ztick_adapter released;
  ztick::sdk::AdapterHandle release_handle(&released, make_ops());
  check(release_handle.release() == &released, "release must return the owned pointer");
  check(!release_handle, "release must empty the handle");
  check(released.destroy_calls == 0, "release must not destroy");
  check(ztick_test_destroy(&released) == ztick_status_success, "manual destroy must succeed");
  check(released.destroy_calls == 1, "manual destroy must be observed");

  // Move construction empties the source and transfers the destroy duty.
  ztick_adapter moved;
  {
    ztick::sdk::AdapterHandle source(&moved, make_ops());
    ztick::sdk::AdapterHandle destination(std::move(source));
    check(!source, "moved-from handle must be empty");
    check(destination.get() == &moved, "move construction must transfer the pointer");
    check(moved.destroy_calls == 0, "move construction must not destroy");
  }
  check(moved.destroy_calls == 1, "moved-to handle must destroy once");

  // Move assignment destroys the previous instance and empties the source.
  ztick_adapter old_adapter;
  ztick_adapter new_adapter;
  {
    ztick::sdk::AdapterHandle target(&old_adapter, make_ops());
    ztick::sdk::AdapterHandle source(&new_adapter, make_ops());
    target = std::move(source);
    check(old_adapter.destroy_calls == 1, "move assignment must destroy the old instance");
    check(!source, "move-assigned-from handle must be empty");
    check(target.get() == &new_adapter, "move assignment must transfer the pointer");
    check(new_adapter.destroy_calls == 0, "move assignment must not destroy the new instance");
  }
  check(new_adapter.destroy_calls == 1, "moved-to handle must destroy on scope exit");

  // Self move-assignment leaves the owned instance intact.
  ztick_adapter self_adapter;
  {
    ztick::sdk::AdapterHandle handle(&self_adapter, make_ops());
    ztick::sdk::AdapterHandle &alias = handle;
    handle = std::move(alias);
    check(static_cast<bool>(handle), "self move-assignment must keep ownership");
    check(self_adapter.destroy_calls == 0, "self move-assignment must not destroy");
  }
  check(self_adapter.destroy_calls == 1, "self move-assignment must still destroy once");

  return g_failures == 0 ? 0 : 1;
}
