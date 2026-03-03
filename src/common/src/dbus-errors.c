/*
 * Zen OS — D-Bus Error Implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/dbus-errors.h"

static const char *error_strings[ZEN_ERR_COUNT] = {
    [ZEN_OK]                    = "Success",
    [ZEN_ERR_INVALID_ARGUMENT]  = "Invalid argument",
    [ZEN_ERR_NOT_FOUND]         = "Not found",
    [ZEN_ERR_PERMISSION_DENIED] = "Permission denied",
    [ZEN_ERR_ALREADY_EXISTS]    = "Already exists",
    [ZEN_ERR_NOT_SUPPORTED]     = "Not supported",
    [ZEN_ERR_TIMEOUT]           = "Timeout",
    [ZEN_ERR_OUT_OF_MEMORY]     = "Out of memory",
    [ZEN_ERR_IO]                = "I/O error",
    [ZEN_ERR_INTERNAL]          = "Internal error",
};

static const char *dbus_names[ZEN_ERR_COUNT] = {
    [ZEN_OK]                    = "org.zenos.Error.None",
    [ZEN_ERR_INVALID_ARGUMENT]  = "org.zenos.Error.InvalidArgument",
    [ZEN_ERR_NOT_FOUND]         = "org.zenos.Error.NotFound",
    [ZEN_ERR_PERMISSION_DENIED] = "org.zenos.Error.PermissionDenied",
    [ZEN_ERR_ALREADY_EXISTS]    = "org.zenos.Error.AlreadyExists",
    [ZEN_ERR_NOT_SUPPORTED]     = "org.zenos.Error.NotSupported",
    [ZEN_ERR_TIMEOUT]           = "org.zenos.Error.Timeout",
    [ZEN_ERR_OUT_OF_MEMORY]     = "org.zenos.Error.OutOfMemory",
    [ZEN_ERR_IO]                = "org.zenos.Error.IO",
    [ZEN_ERR_INTERNAL]          = "org.zenos.Error.Internal",
};

const char *zen_error_string(ZenError err) {
    if (err < 0 || err >= ZEN_ERR_COUNT) {
        return "Unknown error";
    }
    return error_strings[err];
}

const char *zen_error_dbus_name(ZenError err) {
    if (err < 0 || err >= ZEN_ERR_COUNT) {
        return "org.zenos.Error.Internal";
    }
    return dbus_names[err];
}
