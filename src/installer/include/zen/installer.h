/*
 * Zen OS — System Installer
 *
 * Installs Zen OS to target disk from live USB/ISO.
 * Minimal Cairo + Pango GUI for disk selection and LUKS passphrase.
 * Backend: partition, mkfs, LUKS, ostree admin deploy, GRUB install.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_INSTALLER_INSTALLER_H
#define ZEN_INSTALLER_INSTALLER_H

/*
 * Initialize the installer.
 * Returns 0 on success, -1 on failure.
 */
int zen_installer_init(void);

/*
 * Run the installer wizard.  Blocks until installation completes
 * or the user cancels.
 * Returns 0 on success, -1 on failure.
 */
int zen_installer_run(void);

/*
 * Destroy the installer, releasing all resources.
 */
void zen_installer_destroy(void);

#endif /* ZEN_INSTALLER_INSTALLER_H */
