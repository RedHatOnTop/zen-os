/*
 * Zen OS — OOBE (Out-Of-Box Experience)
 *
 * First-boot guided setup wizard rendered via Cairo + Pango.
 * Collects locale, timezone, keyboard layout, user account,
 * and optional network configuration.  Runs as a Wayland client.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_OOBE_OOBE_H
#define ZEN_OOBE_OOBE_H

/*
 * Initialize the OOBE wizard.
 * Returns 0 on success, -1 on failure.
 */
int zen_oobe_init(void);

/*
 * Run the OOBE wizard.  Blocks until the user completes setup.
 * Returns 0 on success, -1 on failure or interruption.
 */
int zen_oobe_run(void);

/*
 * Destroy the OOBE wizard, releasing all resources.
 */
void zen_oobe_destroy(void);

#endif /* ZEN_OOBE_OOBE_H */
