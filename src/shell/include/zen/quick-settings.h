/*
 * Zen OS Shell — Quick Settings Panel
 *
 * Popup panel from the system tray area with toggles for Wi-Fi,
 * Bluetooth, volume, brightness, and Do Not Disturb.  Includes
 * per-app volume control in expanded view.
 *
 * Rendered via Cairo + Pango with translucent blur via SceneFX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_QUICK_SETTINGS_H
#define ZEN_SHELL_QUICK_SETTINGS_H

/*
 * Initialize the Quick Settings panel.
 * Returns 0 on success, -1 on failure.
 */
int zen_quick_settings_init(void);

/*
 * Destroy the Quick Settings panel, releasing all resources.
 */
void zen_quick_settings_destroy(void);

/*
 * Toggle the Quick Settings panel visibility.
 */
void zen_quick_settings_toggle(void);

#endif /* ZEN_SHELL_QUICK_SETTINGS_H */
