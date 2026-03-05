/*
 * Zen OS Shell — Shelf (Taskbar)
 *
 * Persistent bottom bar on the primary display containing pinned
 * application icons, running app indicators, and the system tray
 * (battery, Bluetooth, network, volume, storage).
 *
 * Rendered via Cairo + Pango; composited with SceneFX effects.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_SHELF_H
#define ZEN_SHELL_SHELF_H

/*
 * Initialize the Shelf component.
 * Returns 0 on success, -1 on failure.
 */
int zen_shelf_init(void);

/*
 * Destroy the Shelf, releasing all Cairo surfaces and resources.
 */
void zen_shelf_destroy(void);

/*
 * Pin an application to the Shelf by app_id.
 * Returns 0 on success, -1 if app_id is invalid.
 */
int zen_shelf_pin_app(const char *app_id);

/*
 * Unpin an application from the Shelf.
 * Returns 0 on success, -1 if app_id is not pinned.
 */
int zen_shelf_unpin_app(const char *app_id);

#endif /* ZEN_SHELL_SHELF_H */
