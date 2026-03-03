/*
 * Zen OS — D-Bus Error Definitions
 *
 * Standardized error codes for all D-Bus interface methods.
 * Each daemon maps these enums to org.zenos.Error.* D-Bus error names.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMMON_DBUS_ERRORS_H
#define ZEN_COMMON_DBUS_ERRORS_H

typedef enum {
    ZEN_OK = 0,
    ZEN_ERR_INVALID_ARGUMENT,
    ZEN_ERR_NOT_FOUND,
    ZEN_ERR_PERMISSION_DENIED,
    ZEN_ERR_ALREADY_EXISTS,
    ZEN_ERR_NOT_SUPPORTED,
    ZEN_ERR_TIMEOUT,
    ZEN_ERR_OUT_OF_MEMORY,
    ZEN_ERR_IO,
    ZEN_ERR_INTERNAL,
    ZEN_ERR_COUNT,  /* sentinel — must be last */
} ZenError;

/*
 * Return a human-readable string for the given error code.
 * Never returns NULL — returns "Unknown error" for out-of-range values.
 */
const char *zen_error_string(ZenError err);

/*
 * Return the D-Bus error name (e.g., "org.zenos.Error.NotFound").
 * Never returns NULL.
 */
const char *zen_error_dbus_name(ZenError err);

#endif /* ZEN_COMMON_DBUS_ERRORS_H */
