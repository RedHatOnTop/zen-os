/*
 * Zen OS — Session Manager
 *
 * User authentication and session lifecycle management.
 * Integrates with systemd-logind and PAM for local credential auth.
 * Detects first-boot condition and launches OOBE when needed.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SESSION_SESSION_H
#define ZEN_SESSION_SESSION_H

/*
 * Initialize the Session Manager.
 * Connects to systemd-logind via sd-bus.
 * Returns 0 on success, -1 on failure.
 */
int zen_session_init(void);

/*
 * Run the Session Manager event loop.  Blocks until terminated.
 */
void zen_session_run(void);

/*
 * Shut down the Session Manager, releasing all resources.
 */
void zen_session_destroy(void);

/*
 * Check whether the OOBE has been completed.
 * Returns 1 if /var/lib/zenos/oobe-complete exists, 0 otherwise.
 */
int zen_session_oobe_complete(void);

#endif /* ZEN_SESSION_SESSION_H */
