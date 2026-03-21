/*
 * Zen OS Compositor — Screen Lock API
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ZEN_COMPOSITOR_LOCK_H
#define ZEN_COMPOSITOR_LOCK_H

#include <stdbool.h>

struct ZenCompositor;

/*
 * Initialize the session lock manager.
 * Creates wlr_session_lock_manager_v1 and registers the lock listener.
 * Returns 0 on success, -1 on failure.
 */
int  zen_lock_init(struct ZenCompositor *compositor);

/*
 * Destroy the lock manager and any active lock surfaces.
 */
void zen_lock_destroy(struct ZenCompositor *compositor);

/*
 * Activate the lock screen on all outputs.
 * Sets compositor->locked = true, creates lock surfaces, renders lock UI.
 * No-op if already locked.
 */
void zen_lock_activate(struct ZenCompositor *compositor);

/*
 * Returns true if the compositor is currently locked.
 */
bool zen_lock_is_active(struct ZenCompositor *compositor);

#endif /* ZEN_COMPOSITOR_LOCK_H */
