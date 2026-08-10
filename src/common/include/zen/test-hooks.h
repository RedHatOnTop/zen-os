/*
 * Zen OS — Test Hook Utilities
 *
 * Shared helpers for test-only state snapshots written by compositor and
 * shell debug hooks. These helpers are intentionally dependency-free.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMMON_TEST_HOOKS_H
#define ZEN_COMMON_TEST_HOOKS_H

#include <stdio.h>

#define ZEN_TEST_HOOKS_DIR "/run/zenos-test"
#define ZEN_SHELL_STATE_PATH ZEN_TEST_HOOKS_DIR "/shell-state.json"
#define ZEN_COMPOSITOR_STATE_PATH ZEN_TEST_HOOKS_DIR "/compositor-state.json"

/*
 * Ensure the runtime directory for test state files exists.
 * Returns 0 on success, -1 on failure.
 */
int zen_test_hooks_ensure_dir(void);

/*
 * Atomically write a UTF-8 JSON document to the given path.
 * Returns 0 on success, -1 on failure.
 */
int zen_test_hooks_write_atomic(const char *path, const char *content);

/*
 * Remove a previously written state file. Missing files are ignored.
 * Returns 0 on success, -1 on failure.
 */
int zen_test_hooks_remove_file(const char *path);

/*
 * Write a JSON string literal, including surrounding quotes and escaping.
 * NULL values are serialized as the empty string.
 */
void zen_test_hooks_write_json_string(FILE *stream, const char *value);

#endif /* ZEN_COMMON_TEST_HOOKS_H */
