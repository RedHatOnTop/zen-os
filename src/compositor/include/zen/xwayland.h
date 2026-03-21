/*
 * Zen OS Compositor — XWayland Bridge (Optional)
 *
 * Provides XWayland integration for running legacy X11 applications.
 * Initialization returns -1 if XWayland support is disabled or unavailable.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_XWAYLAND_H
#define ZEN_COMPOSITOR_XWAYLAND_H

struct ZenCompositor;

/* Initialize XWayland. Returns 0 on success, -1 if disabled or failed. */
int zen_xwayland_init(struct ZenCompositor *compositor);

/* Destroy XWayland state. */
void zen_xwayland_destroy(struct ZenCompositor *compositor);

#endif /* ZEN_COMPOSITOR_XWAYLAND_H */
