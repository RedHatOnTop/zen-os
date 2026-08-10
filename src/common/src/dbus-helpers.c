/*
 * Zen OS — sd-bus Utility Helpers Implementation
 *
 * Stub implementations for D-Bus helper functions.
 * Will be fleshed out when daemon D-Bus interfaces are implemented.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/dbus-helpers.h"

#include <stdlib.h>

int zen_dbus_reply_error(struct sd_bus_message *msg, ZenError err) {
    (void)msg;
    (void)err;
    /* TODO: Call sd_bus_reply_method_error with mapped error name */
    return -1;
}

char *zen_dbus_get_property_string(struct sd_bus_message *msg,
                                   const char *interface,
                                   const char *property) {
    (void)msg;
    (void)interface;
    (void)property;
    /* TODO: Read string property from D-Bus message */
    return NULL;
}

int zen_dbus_emit_signal(const char *path, const char *interface,
                         const char *member) {
    (void)path;
    (void)interface;
    (void)member;
    /* TODO: Emit signal via sd_bus_emit_signal */
    return -1;
}
