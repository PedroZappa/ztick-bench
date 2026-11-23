// SPDX-License-Identifier: MIT
/**
 * @file dynamic_adapter.cpp
 * @brief Metadata-only export scaffold for the Phase 3 dynamic SDK test app.
 *
 * The final engine harness (TESTING_STRATEGY §3, P3.8) replaces this scaffold.
 * It already exports the descriptor named by @ref ZTICK_ADAPTER_EXPORT_SYMBOL as
 * read-only data, which the host can inspect without executing plugin code
 * (BR §6.1). Lifecycle callbacks remain unset until that work lands.
 */

#include <ztick/sdk.h>

namespace {

/** @brief Placeholder integration version for the dynamic scaffold. */
const ztick_version kScaffoldAdapterVersion{static_cast<uint32_t>(sizeof(ztick_version)),
                                            ZTICK_STRUCT_LAYOUT_VERSION, 0U, 1U, 0U};

/** @brief SDK ABI version this scaffold was compiled against. */
const ztick_version kScaffoldSdkVersion{static_cast<uint32_t>(sizeof(ztick_version)),
                                        ZTICK_STRUCT_LAYOUT_VERSION, ZTICK_SDK_VERSION_MAJOR,
                                        ZTICK_SDK_VERSION_MINOR, ZTICK_SDK_VERSION_PATCH};

/** @brief Empty lifecycle table; callbacks are supplied by P3.8. */
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
 * @brief Read-only descriptor exported for metadata-only host inspection.
 * @note The symbol name matches @ref ZTICK_ADAPTER_EXPORT_SYMBOL. Reading it must
 *       never call adapter code; P3.8 adds the lifecycle callbacks.
 */
extern "C" const ztick_adapter_descriptor ztick_adapter_export = {
    static_cast<uint32_t>(sizeof(ztick_adapter_descriptor)),
    ZTICK_STRUCT_LAYOUT_VERSION,
    {static_cast<uint32_t>(sizeof(ztick_adapter_info)), ZTICK_STRUCT_LAYOUT_VERSION,
     "ztick.test.dynamic.scaffold", "Dynamic SDK test application scaffold",
     kScaffoldAdapterVersion, kScaffoldSdkVersion, 0U,
     "Metadata-only scaffold; lifecycle callbacks arrive with P3.8."},
    kScaffoldOps};
