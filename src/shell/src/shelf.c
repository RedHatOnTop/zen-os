/*
 * Zen OS Shell — Shelf (Taskbar)
 *
 * Renders the persistent bottom bar with pinned app icons, running
 * app indicators, and system tray.  Cairo + Pango for content,
 * SceneFX for rounded corners and drop shadows.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/shelf.h"

int zen_shelf_init(void) {
    /* TODO: Create Cairo surface, load pinned apps from config */
    return 0;
}

void zen_shelf_destroy(void) {
    /* TODO: Free Cairo surfaces, save pinned app state */
}

int zen_shelf_pin_app(const char *app_id) {
    (void)app_id;
    /* TODO: Add app_id to pinned list, repaint shelf */
    return 0;
}

int zen_shelf_unpin_app(const char *app_id) {
    (void)app_id;
    /* TODO: Remove app_id from pinned list, repaint shelf */
    return 0;
}
