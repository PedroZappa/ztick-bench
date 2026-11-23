// SPDX-License-Identifier: MIT
/**
 * @file version.h
 * @brief Compile-time SDK ABI version and struct layout version for the C boundary.
 *
 * This header is self-contained C11 and belongs to the installed public include
 * root. It defines only macros; the runtime version query is part of the C ABI
 * surface declared by <ztick/sdk.h>.
 *
 * Versioning rules are defined in SDK_SPECIFICATION §2: major versions must
 * match exactly, minor versions are backward-compatible with a newer host, and
 * patch differences are compatible.
 */

/** @brief Include guard for the public SDK version header. */
#ifndef ZTICK_VERSION_H
#define ZTICK_VERSION_H

/**
 * @brief Major component of the SDK ABI version.
 * @note A major mismatch makes an integration incompatible and the host must
 *       not execute it (SDK_SPECIFICATION §2).
 */
#define ZTICK_SDK_VERSION_MAJOR 1

/**
 * @brief Minor component of the SDK ABI version.
 * @note A host older than an adapter's declared minor version must refuse it;
 *       a newer host remains backward-compatible (SDK_SPECIFICATION §2).
 */
#define ZTICK_SDK_VERSION_MINOR 0

/**
 * @brief Patch component of the SDK ABI version.
 * @note Patch differences are compatible (SDK_SPECIFICATION §2).
 */
#define ZTICK_SDK_VERSION_PATCH 0

/**
 * @brief Layout version expected in the `version` field of every public struct.
 * @note Set `size` to `sizeof(struct)` and `version` to this value before passing a
 *       public struct to the SDK runtime; any other value is refused with
 *       `ztick_status_invalid_argument` (SDK_SPECIFICATION §2). This struct layout
 *       version is independent of the `ZTICK_SDK_VERSION_*` ABI components.
 */
#define ZTICK_STRUCT_LAYOUT_VERSION 1

#endif // ZTICK_VERSION_H
