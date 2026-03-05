/*
 * Zen OS Shell — Notification Manager
 *
 * Displays floating toast notifications anchored top-right.
 * Implements org.freedesktop.Notifications D-Bus interface.
 * Rendered via Cairo + Pango; shadows via SceneFX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/notifications.h"

int zen_notifications_init(void) {
    /* TODO: Register D-Bus interface, create notification queue */
    return 0;
}

void zen_notifications_destroy(void) {
    /* TODO: Unregister D-Bus interface, free notification queue */
}

int zen_notifications_show(const char *app_id, const char *summary,
                           const char *body, const char *icon,
                           int urgency, int timeout_ms) {
    (void)app_id;
    (void)summary;
    (void)body;
    (void)icon;
    (void)urgency;
    (void)timeout_ms;
    /* TODO: Create toast surface, add to scene graph, start timer */
    return 0;
}
