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

/*
 * Initialize the App Launcher overlay.
 * Returns 0 on success, -1 on failure.
 */
int zen_app_launcher_init(void);

/*
 * Destroy the App Launcher, releasing all resources.
 */
void zen_app_launcher_destroy(void);

/*
 * Show the App Launcher overlay.
 */
void zen_app_launcher_show(void);

/*
 * Hide the App Launcher overlay.
 */
void zen_app_launcher_hide(void);

#endif /* ZEN_SHELL_APP_LAUNCHER_H */
