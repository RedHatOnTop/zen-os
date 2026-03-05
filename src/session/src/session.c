/*
 * Zen OS — Session Manager
 *
 * Manages user sessions via systemd-logind:
 * - PAM-based local authentication
 * - Seat/session assignment for multi-user
 * - First-boot detection (OOBE flag check)
 * - Session teardown on logout
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/session.h"

int zen_session_init(void) {
    /* TODO: Connect to sd-bus, query logind */
    return 0;
}

void zen_session_run(void) {
    /* TODO: Enter sd-bus event loop */
}

void zen_session_destroy(void) {
    /* TODO: Disconnect from sd-bus */
}

int zen_session_oobe_complete(void) {
    /* TODO: Check /var/lib/zenos/oobe-complete */
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (zen_session_init() != 0) {
        return 1;
    }

    zen_session_run();
    zen_session_destroy();

    return 0;
}
