/*
 * Zen OS Compositor — Test State Snapshot Hooks
 *
 * Test-only helpers that serialize observable compositor state for zen-test.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_COMPOSITOR_TESTCTL_H
#define ZEN_COMPOSITOR_COMPOSITOR_TESTCTL_H

struct ZenCompositor;

void zen_compositor_testctl_sync(struct ZenCompositor *compositor);
void zen_compositor_testctl_clear(void);

#endif /* ZEN_COMPOSITOR_COMPOSITOR_TESTCTL_H */
