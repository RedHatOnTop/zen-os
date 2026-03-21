/*
 * Zen OS Compositor — D-Bus Interface
 *
 * Registers org.zenos.Compositor on the session bus and integrates
 * the sd-bus file descriptor into the wl_event_loop.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_DBUS_H
#define ZEN_COMPOSITOR_DBUS_H

struct ZenCompositor;

/* Initialize sd-bus, register org.zenos.Compositor on session bus,
 * and integrate the sd-bus fd into the wl_event_loop.
 * Returns 0 on success. */
int zen_dbus_init(struct ZenCompositor *compositor);

/* Destroy sd-bus connection. */
void zen_dbus_destroy(struct ZenCompositor *compositor);

#endif /* ZEN_COMPOSITOR_DBUS_H */
