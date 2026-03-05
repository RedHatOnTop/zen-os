/*
 * Zen OS — sd-bus Utility Helpers
 *
 * Convenience wrappers around libsystemd sd-bus for common D-Bus
 * operations used by all Zen OS daemons.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMMON_DBUS_HELPERS_H
#define ZEN_COMMON_DBUS_HELPERS_H

#include "zen/dbus-errors.h"

/* Forward declaration — avoids requiring systemd headers in every consumer */
struct sd_bus_message;

/*
 * Send a D-Bus error reply from a ZenError code.
 * Maps the error to the appropriate org.zenos.Error.* name.
 * Returns negative errno on failure.
 */
int zen_dbus_reply_error(struct sd_bus_message *msg, ZenError err);

/*
 * Helper to read a string property from a D-Bus message.
 * Caller must free the returned string.
 * Returns NULL on failure.
 */
char *zen_dbus_get_property_string(struct sd_bus_message *msg,
                                   const char *interface,
                                   const char *property);

/*
 * Helper to emit a D-Bus signal on the given path and interface.
 * Returns 0 on success, negative errno on failure.
 */
int zen_dbus_emit_signal(const char *path, const char *interface,
                         const char *member);

#endif /* ZEN_COMMON_DBUS_HELPERS_H */
