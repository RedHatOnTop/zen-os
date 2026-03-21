/*
 * Zen OS — Property-Based Tests for Session Manager
 *
 * Tests Property 21 from the Phase 1 design document.
 * Uses CMocka + zen_pbt.h for randomized input generation.
 *
 * Property 21: First-boot detection
 *   For any filesystem state, zen_session_oobe_complete() must return 1
 *   if and only if /var/lib/zenos/oobe-complete exists, and 0 otherwise.
 *
 *   Tested by exercising the pure access(path, F_OK) logic against a
 *   temporary file that is randomly created or absent each iteration.
 *
 * Validates: Requirements 10.5
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt.h"

/* ── Pure logic under test ───────────────────────────────────────────────── */

/*
 * oobe_complete_check — mirrors the logic of zen_session_oobe_complete()
 * but accepts an arbitrary path so we can test with a temp file.
 *
 * Returns 1 if the file at `path` exists and is accessible, 0 otherwise.
 */
static int oobe_complete_check(const char *path) {
    return access(path, F_OK) == 0 ? 1 : 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 21: First-boot detection
 *
 * For any filesystem state, oobe_complete_check(path) must return 1 iff
 * the file at `path` exists, and 0 otherwise.
 *
 * Validates: Requirements 10.5
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * **Validates: Requirements 10.5**
 *
 * Test that oobe_complete_check returns 1 when the file exists.
 */
static void test_property21_file_exists_returns_1(void **state) {
    (void)state;

    /* Create a temp file */
    char path[] = "/tmp/zen-oobe-test-XXXXXX";
    int fd = mkstemp(path);
    assert_true(fd >= 0);
    close(fd);

    int result = oobe_complete_check(path);
    assert_int_equal(result, 1);

    unlink(path);
}

/*
 * **Validates: Requirements 10.5**
 *
 * Test that oobe_complete_check returns 0 when the file does not exist.
 */
static void test_property21_file_absent_returns_0(void **state) {
    (void)state;

    /* Use a path that is guaranteed not to exist */
    const char *path = "/tmp/zen-oobe-nonexistent-sentinel-file-xyz123";
    unlink(path); /* ensure it's gone */

    int result = oobe_complete_check(path);
    assert_int_equal(result, 0);
}

/*
 * **Validates: Requirements 10.5**
 *
 * PBT: For 100 random iterations, randomly decide to create or not create
 * a temp file, then assert oobe_complete_check returns the correct value.
 */
static void test_property21_pbt(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/zen-oobe-pbt-%d", iter);

        /* Ensure clean state */
        unlink(path);

        bool file_exists = zen_pbt_rand_bool();

        if (file_exists) {
            /* Create the file */
            FILE *f = fopen(path, "w");
            assert_non_null(f);
            fclose(f);
        }

        int result = oobe_complete_check(path);
        int expected = file_exists ? 1 : 0;

        assert_int_equal(result, expected);

        /* Cleanup */
        unlink(path);
    }
}

/*
 * **Validates: Requirements 10.5**
 *
 * Test that creating and then deleting the file transitions the result
 * from 1 back to 0 (state change is reflected immediately).
 */
static void test_property21_create_then_delete(void **state) {
    (void)state;

    char path[] = "/tmp/zen-oobe-toggle-XXXXXX";
    int fd = mkstemp(path);
    assert_true(fd >= 0);
    close(fd);

    /* File exists — should return 1 */
    assert_int_equal(oobe_complete_check(path), 1);

    /* Delete the file */
    unlink(path);

    /* File gone — should return 0 */
    assert_int_equal(oobe_complete_check(path), 0);
}

/*
 * **Validates: Requirements 10.5**
 *
 * Test that the function is idempotent: calling it twice with the same
 * filesystem state returns the same value both times.
 */
static void test_property21_idempotent(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/zen-oobe-idem-%d", iter);
        unlink(path);

        bool file_exists = zen_pbt_rand_bool();
        if (file_exists) {
            FILE *f = fopen(path, "w");
            assert_non_null(f);
            fclose(f);
        }

        int r1 = oobe_complete_check(path);
        int r2 = oobe_complete_check(path);

        assert_int_equal(r1, r2);

        unlink(path);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        /* Property 21: First-boot detection */
        cmocka_unit_test(test_property21_file_exists_returns_1),
        cmocka_unit_test(test_property21_file_absent_returns_0),
        cmocka_unit_test(test_property21_pbt),
        cmocka_unit_test(test_property21_create_then_delete),
        cmocka_unit_test(test_property21_idempotent),
    };

    return cmocka_run_group_tests_name("session-properties", tests, NULL, NULL);
}
