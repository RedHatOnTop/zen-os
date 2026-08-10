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

#include <stdbool.h>

struct ZenCompositor;
struct ZenShelf;
struct ZenAppLauncher;
struct ZenQuickSettings;
struct ZenNotificationMgr;
struct ZenThemeConfig;

/* Top-level shell state, owned by ZenCompositor. */
struct ZenShell {
    struct ZenCompositor      *compositor;
    struct ZenShelf           *shelf;
    struct ZenAppLauncher     *launcher;
    struct ZenQuickSettings   *quick_settings;
    struct ZenNotificationMgr *notifications;
    struct ZenThemeConfig     *theme;
    bool                       dark_mode;
};

/* Initialize the desktop shell. Must be called after scene graph is ready.
 * Returns 0 on success, -1 on failure. */
int zen_shell_init(struct ZenShell *shell, struct ZenCompositor *compositor);

/* Tear down the desktop shell, releasing all resources.
 * Destroys in reverse order: notifications, quick_settings, launcher, shelf. */
void zen_shell_destroy(struct ZenShell *shell);

/* Switch all shell elements to dark or light mode. Repaints all surfaces. */
void zen_shell_set_dark_mode(struct ZenShell *shell, bool enabled);

/* Repaint all visible shell surfaces (called after theme change). */
void zen_shell_repaint_all(struct ZenShell *shell);

#endif /* ZEN_SHELL_SHELL_H */
