/*
 * Zen OS — Package Manager Daemon
 *
 * Unified management of Flatpak apps and OSTree-layered native packages.
 * Handles install, remove, search, and permission management.
 * Exposes org.zenos.PackageManager D-Bus interface.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_PACKAGE_MANAGER_PACKAGE_MANAGER_H
#define ZEN_PACKAGE_MANAGER_PACKAGE_MANAGER_H

/*
 * Initialize the Package Manager daemon.
 * Opens Flatpak installation, OSTree sysroot, connects to sd-bus.
 * Returns 0 on success, -1 on failure.
 */
int zen_package_manager_init(void);

/*
 * Run the Package Manager event loop.  Blocks until terminated.
 */
void zen_package_manager_run(void);

/*
 * Shut down the Package Manager, releasing all resources.
 */
void zen_package_manager_destroy(void);

#endif /* ZEN_PACKAGE_MANAGER_PACKAGE_MANAGER_H */
