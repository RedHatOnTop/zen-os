/*
 * Zen OS Shell — App Launcher
 *
 * Fullscreen overlay grid of installed applications with real-time
 * search filtering.  Rendered via Cairo + Pango; translucent
 * Gaussian blur background via SceneFX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/app-launcher.h"

int zen_app_launcher_init(void) {
    /* TODO: Scan .desktop files, build app list */
    return 0;
}

void zen_app_launcher_destroy(void) {
    /* TODO: Free app list and Cairo surfaces */
}

void zen_app_launcher_show(void) {
    /* TODO: Render overlay, attach to scene graph */
}

void zen_app_launcher_hide(void) {
    /* TODO: Detach overlay from scene graph */
}
