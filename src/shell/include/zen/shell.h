/*
 * Zen OS Shell — Desktop Shell API Boundary
 *
 * Top-level API for the in-process desktop shell module.
 * The shell is compiled into the compositor binary and renders
 * widget content via Cairo + Pango into SceneFX scene graph buffers.
 *
 * Components: Shelf, App Launcher, Quick Settings, Notifications.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_SHELL_H
#define ZEN_SHELL_SHELL_H

/*
 * Initialize the desktop shell module.
 * Must be called after the compositor scene graph is ready.
 * Returns 0 on success, -1 on failure.
 */
int zen_shell_init(void);

/*
 * Tear down the desktop shell, releasing all resources.
 */
void zen_shell_destroy(void);

/*
 * Toggle system-wide dark mode.
 * Repaints all shell surfaces with the new theme.
 */
void zen_shell_set_dark_mode(int enabled);

#endif /* ZEN_SHELL_SHELL_H */
