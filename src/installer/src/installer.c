/*
 * Zen OS — System Installer
 *
 * Installation workflow:
 * 1. Language selection
 * 2. Disk selection
 * 3. LUKS passphrase (optional)
 * 4. User account creation
 * 5. Confirm and install
 * 6. Partition → mkfs → LUKS → ostree deploy → GRUB → reboot
 *
 * Error handling: any step failure shows Cairo error dialog
 * with retry/cancel.  Disk not modified until user confirms.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/installer.h"

int zen_installer_init(void) {
    /* TODO: Create Cairo surface, enumerate disks */
    return 0;
}

int zen_installer_run(void) {
    /* TODO: Step through installer pages */
    return 0;
}

void zen_installer_destroy(void) {
    /* TODO: Free Cairo surfaces */
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (zen_installer_init() != 0) {
        return 1;
    }

    int ret = zen_installer_run();
    zen_installer_destroy();

    return ret;
}
