/*
 * Zen OS — OOBE (Out-Of-Box Experience)
 *
 * First-boot wizard steps:
 * 1. Locale selection
 * 2. Timezone selection
 * 3. Keyboard layout
 * 4. User account creation (username + password)
 * 5. Network setup (optional)
 * 6. Apply config, create user, set oobe-complete flag, reboot
 *
 * All steps completable offline.  Keyboard-only navigable.
 * On interruption: flag not set, OOBE restarts on next boot.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/oobe.h"

int zen_oobe_init(void) {
    /* TODO: Create Cairo surface, connect to Wayland display */
    return 0;
}

int zen_oobe_run(void) {
    /* TODO: Step through wizard pages, collect config */
    return 0;
}

void zen_oobe_destroy(void) {
    /* TODO: Free Cairo surfaces, disconnect from Wayland */
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (zen_oobe_init() != 0) {
        return 1;
    }

    int ret = zen_oobe_run();
    zen_oobe_destroy();

    return ret;
}
