// SPDX-License-Identifier: MIT
/**
 * @file static_adapter.cpp
 * @brief Metadata-only descriptor scaffold for the Phase 3 static SDK test app.
 *
 * The final processor/chain harness (TESTING_STRATEGY §3, P3.7) replaces this
 * scaffold. It already publishes a complete descriptor header so the registry can
 * be exercised, but intentionally leaves every lifecycle callback unset: no
 * conformant processing is claimed until that work lands.
 */

#include <ztick/sdk.h>

namespace {

/** @brief Placeholder integration version for the static scaffold. */
const ztick_version kScaffoldAdapterVersion{static_cast<uint32_t>(sizeof(ztick_version)),
                                            ZTICK_STRUCT_LAYOUT_VERSION, 0U, 1U, 0U};

/** @brief SDK ABI version this scaffold was compiled against. */
const ztick_version kScaffoldSdkVersion{static_cast<uint32_t>(sizeof(ztick_version)),
                                        ZTICK_STRUCT_LAYOUT_VERSION, ZTICK_SDK_VERSION_MAJOR,
                                        ZTICK_SDK_VERSION_MINOR, ZTICK_SDK_VERSION_PATCH};

/** @brief Empty lifecycle table; callbacks are supplied by P3.7. */
const ztick_adapter_ops kScaffoldOps{static_cast<uint32_t>(sizeof(ztick_adapter_ops)),
                                     ZTICK_STRUCT_LAYOUT_VERSION,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr};

} // namespace

/**
 * @brief Static registration-point descriptor for the test application scaffold.
 * @note The descriptor has static storage duration so it can outlive the SDK
 *       registry. P3.7 adds the registration call and the lifecycle callbacks.
 */
extern "C" const ztick_adapter_descriptor ztick_test_app_static_descriptor = {
    static_cast<uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     "ztick.test.static.scaffold", "Static SDK test application scaffold", kScaffoldAdapterVersion,
     kScaffoldSdkVersion, 0U, "Metadata-only scaffold; lifecycle callbacks arrive with P3.7."},
    kScaffoldOps};
