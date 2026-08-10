/*
 * Zen OS — Resource Manager Daemon
 *
 * Monitors system memory pressure via PSI, manages cgroups v2 budgets,
 * controls zram, and handles Waydroid container freeze/thaw lifecycle.
 * Exposes org.zenos.ResourceManager D-Bus interface.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_RESOURCE_MANAGER_RESOURCE_MANAGER_H
#define ZEN_RESOURCE_MANAGER_RESOURCE_MANAGER_H

/*
 * Initialize the Resource Manager daemon.
 * Connects to sd-bus, sets up PSI polling, configures cgroups.
 * Returns 0 on success, -1 on failure.
 */
int zen_resource_manager_init(void);

/*
 * Run the Resource Manager event loop.  Blocks until terminated.
 */
void zen_resource_manager_run(void);

/*
 * Shut down the Resource Manager, releasing all resources.
 */
void zen_resource_manager_destroy(void);

#endif /* ZEN_RESOURCE_MANAGER_RESOURCE_MANAGER_H */
