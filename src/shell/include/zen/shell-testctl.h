/*
 * Zen OS Shell — Test State Snapshot Hooks
 *
 * Test-only helpers that serialize observable shell state for zen-test.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_SHELL_TESTCTL_H
#define ZEN_SHELL_SHELL_TESTCTL_H

struct ZenShell;

void zen_shell_testctl_sync(struct ZenShell *shell);
void zen_shell_testctl_clear(void);

#endif /* ZEN_SHELL_SHELL_TESTCTL_H */
