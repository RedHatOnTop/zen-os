/*
 * Zen OS Shell — App Launcher
 *
 * Fullscreen overlay grid triggered from the Shelf, displaying all
 * installed applications with a search field for real-time filtering.
 * Sources: .desktop files, Flatpak exports, PWA registrations, Waydroid.
 *
 * Rendered via Cairo + Pango with translucent Gaussian blur via SceneFX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_APP_LAUNCHER_H
#define ZEN_SHELL_APP_LAUNCHER_H

#include <stdbool.h>
#include <stdint.h>

struct ZenShell;
struct ZenCairoBuffer;
struct ZenDesktopEntry;

struct ZenAppLauncher {
    struct ZenShell        *shell;
    struct ZenCairoBuffer  *overlay_buffer;
    struct ZenDesktopEntry *entries;
    int                     entry_count;
    struct ZenDesktopEntry **filtered;
    int                     filtered_count;
    char                    search_text[256];
    int                     selected_index;
    bool                    visible;
    int                     grid_cols;
    int                     grid_rows;
};

/* Initialize the App Launcher. Returns 0 on success, -1 on failure. */
int zen_app_launcher_init(struct ZenAppLauncher *launcher,
                          struct ZenShell *shell);

/* Destroy the App Launcher, releasing all resources. */
void zen_app_launcher_destroy(struct ZenAppLauncher *launcher);

/* Toggle visibility (show if hidden, hide if shown). */
void zen_app_launcher_toggle(struct ZenAppLauncher *launcher);

/* Show the App Launcher overlay. */
void zen_app_launcher_show(struct ZenAppLauncher *launcher);

/* Hide the App Launcher overlay. */
void zen_app_launcher_hide(struct ZenAppLauncher *launcher);

/* Repaint the overlay (after theme change or search update). */
void zen_app_launcher_repaint(struct ZenAppLauncher *launcher);

/* Handle a key event. Returns true if consumed. */
bool zen_app_launcher_handle_key(struct ZenAppLauncher *launcher,
                                 uint32_t keysym, uint32_t modifiers);

/* Handle text input for search field. */
void zen_app_launcher_handle_text(struct ZenAppLauncher *launcher,
                                  const char *text);

/* Rescan .desktop files and rebuild the entry list. */
int zen_app_launcher_rescan(struct ZenAppLauncher *launcher);

#endif /* ZEN_SHELL_APP_LAUNCHER_H */
