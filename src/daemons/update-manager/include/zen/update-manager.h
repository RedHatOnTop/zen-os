/*
 * Zen OS — Update Manager Daemon
 *
 * OSTree-based atomic update system with delta downloads,
 * cryptographic signature verification, and automatic rollback.
 * Exposes org.zenos.UpdateManager D-Bus interface.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_UPDATE_MANAGER_UPDATE_MANAGER_H
#define ZEN_UPDATE_MANAGER_UPDATE_MANAGER_H

/*
 * Initialize the Update Manager daemon.
 * Opens the OSTree sysroot, connects to sd-bus.
 * Returns 0 on success, -1 on failure.
 */
int zen_update_manager_init(void);

/*
 * Run the Update Manager event loop.  Blocks until terminated.
 */
void zen_update_manager_run(void);

/*
 * Shut down the Update Manager, releasing all resources.
 */
void zen_update_manager_destroy(void);

#endif /* ZEN_UPDATE_MANAGER_UPDATE_MANAGER_H */
