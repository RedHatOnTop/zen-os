/*
 * Zen OS Shell — Notification Manager
 *
 * Floating toast elements anchored top-right of the primary display.
 * Implements the org.freedesktop.Notifications D-Bus interface.
 * Styled consistently with the system theme; shadows via SceneFX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_NOTIFICATIONS_H
#define ZEN_SHELL_NOTIFICATIONS_H

/*
 * Initialize the Notification Manager.
 * Returns 0 on success, -1 on failure.
 */
int zen_notifications_init(void);

/*
 * Destroy the Notification Manager, releasing all resources.
 */
void zen_notifications_destroy(void);

/*
 * Show a notification toast.
 * Returns the notification id, or -1 on failure.
 */
int zen_notifications_show(const char *app_id, const char *summary,
                           const char *body, const char *icon,
                           int urgency, int timeout_ms);

#endif /* ZEN_SHELL_NOTIFICATIONS_H */
