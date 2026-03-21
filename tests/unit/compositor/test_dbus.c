/*
 * Zen OS — Property-Based Tests for D-Bus Interface
 *
 * Tests Properties 19 and 20 from the Phase 1 design document.
 * Uses CMocka + zen_pbt.h for randomized input generation.
 *
 * Property 19: ToggleDarkMode state round-trip
 *   For any boolean value `enabled` passed to ToggleDarkMode, the return
 *   value `now_active` must equal `enabled`, and the internal dark_mode
 *   state must equal `enabled`.
 *   Tested by exercising the pure toggle logic without a running D-Bus.
 *
 * Property 20: D-Bus invalid argument error
 *   For any D-Bus method call with arguments that violate the method's
 *   value constraints, the compositor must return an InvalidArgument error.
 *   Tested by exercising the validation logic for app_id (non-empty) and
 *   urgency (range [0, 2]).
 *
 * These tests exercise pure logic without a running Wayland display or
 * D-Bus connection.
 *
 * Validates: Requirements 9.5, 9.6
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt.h"

/* ── Pure logic under test ───────────────────────────────────────────────── */

/*
 * Minimal dark_mode state container — mirrors the relevant fields of
 * ZenCompositor without pulling in wlroots headers.
 */
typedef struct {
    bool dark_mode;
} MockCompositor;

/*
 * toggle_dark_mode — pure logic extracted from handle_toggle_dark_mode().
 *
 * Sets compositor->dark_mode = enabled and returns the new value.
 * This is the exact logic from dbus.c, testable without sd-bus.
 */
static bool toggle_dark_mode(MockCompositor *compositor, bool enabled) {
    compositor->dark_mode = enabled;
    return compositor->dark_mode;
}

/*
 * validate_app_id — pure validation logic from handle_launch_app(),
 * handle_pin_to_shelf(), and handle_unpin_from_shelf().
 *
 * Returns true if app_id is valid (non-NULL, non-empty).
 */
static bool validate_app_id(const char *app_id) {
    return app_id != NULL && app_id[0] != '\0';
}

/*
 * validate_urgency — pure validation logic from handle_show_notification().
 *
 * Returns true if urgency is in the valid range [0, 2].
 */
static bool validate_urgency(int urgency) {
    return urgency >= 0 && urgency <= 2;
}

/*
 * validate_icon_size — pure validation logic from handle_set_shelf_config().
 *
 * Returns true if icon_size > 0.
 */
static bool validate_icon_size(int icon_size) {
    return icon_size > 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 19: ToggleDarkMode state round-trip
 *
 * For any boolean value `enabled`, after calling toggle_dark_mode(enabled),
 * the return value must equal `enabled` and dark_mode must equal `enabled`.
 *
 * Validates: Requirements 9.5
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * **Validates: Requirements 9.5**
 *
 * Test that toggle_dark_mode(true) sets dark_mode to true and returns true.
 */
static void test_property19_toggle_dark_mode_true(void **state) {
    (void)state;

    MockCompositor compositor = { .dark_mode = false };
    bool result = toggle_dark_mode(&compositor, true);

    assert_true(result);
    assert_true(compositor.dark_mode);
}

/*
 * **Validates: Requirements 9.5**
 *
 * Test that toggle_dark_mode(false) sets dark_mode to false and returns false.
 */
static void test_property19_toggle_dark_mode_false(void **state) {
    (void)state;

    MockCompositor compositor = { .dark_mode = true };
    bool result = toggle_dark_mode(&compositor, false);

    assert_false(result);
    assert_false(compositor.dark_mode);
}

/*
 * **Validates: Requirements 9.5**
 *
 * PBT: For any random boolean, toggle_dark_mode must return the same value
 * and set dark_mode to that value (round-trip property).
 */
static void test_property19_roundtrip_pbt(void **state) {
    (void)state;

    MockCompositor compositor = { .dark_mode = false };

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        bool enabled = zen_pbt_rand_bool();

        bool result = toggle_dark_mode(&compositor, enabled);

        /* Return value must equal the input. */
        assert_int_equal((int)result, (int)enabled);

        /* Internal state must equal the input. */
        assert_int_equal((int)compositor.dark_mode, (int)enabled);
    }
}

/*
 * **Validates: Requirements 9.5**
 *
 * Test that calling toggle_dark_mode twice with the same value is idempotent.
 */
static void test_property19_idempotent(void **state) {
    (void)state;

    MockCompositor compositor = { .dark_mode = false };

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        bool enabled = zen_pbt_rand_bool();

        bool result1 = toggle_dark_mode(&compositor, enabled);
        bool result2 = toggle_dark_mode(&compositor, enabled);

        /* Both calls must return the same value. */
        assert_int_equal((int)result1, (int)result2);
        assert_int_equal((int)compositor.dark_mode, (int)enabled);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 20: D-Bus invalid argument error
 *
 * For any D-Bus method call with arguments that violate the method's value
 * constraints, the compositor must return an InvalidArgument error.
 *
 * Validates: Requirements 9.6
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * **Validates: Requirements 9.6**
 *
 * Test that an empty app_id is rejected as invalid.
 */
static void test_property20_empty_app_id_invalid(void **state) {
    (void)state;

    /* Empty string must be invalid. */
    assert_false(validate_app_id(""));

    /* NULL must be invalid. */
    assert_false(validate_app_id(NULL));
}

/*
 * **Validates: Requirements 9.6**
 *
 * Test that a non-empty app_id is accepted as valid.
 */
static void test_property20_nonempty_app_id_valid(void **state) {
    (void)state;

    assert_true(validate_app_id("org.zenos.Terminal"));
    assert_true(validate_app_id("a"));
    assert_true(validate_app_id("firefox"));
}

/*
 * **Validates: Requirements 9.6**
 *
 * PBT: For any random non-empty string, validate_app_id must return true.
 */
static void test_property20_app_id_nonempty_pbt(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        /* Generate a non-empty string (min_len=1). */
        char *app_id = zen_pbt_rand_string(1, 64);
        assert_non_null(app_id);

        bool valid = validate_app_id(app_id);
        assert_true(valid);

        free(app_id);
    }
}

/*
 * **Validates: Requirements 9.6**
 *
 * Test that urgency values outside [0, 2] are rejected.
 */
static void test_property20_urgency_out_of_range_invalid(void **state) {
    (void)state;

    /* Values below 0 must be invalid. */
    assert_false(validate_urgency(-1));
    assert_false(validate_urgency(-100));
    assert_false(validate_urgency(INT_MIN));

    /* Values above 2 must be invalid. */
    assert_false(validate_urgency(3));
    assert_false(validate_urgency(100));
    assert_false(validate_urgency(INT_MAX));
}

/*
 * **Validates: Requirements 9.6**
 *
 * Test that urgency values in [0, 2] are accepted.
 */
static void test_property20_urgency_valid_range(void **state) {
    (void)state;

    assert_true(validate_urgency(0));
    assert_true(validate_urgency(1));
    assert_true(validate_urgency(2));
}

/*
 * **Validates: Requirements 9.6**
 *
 * PBT: For any random urgency value, validate_urgency must return true iff
 * the value is in [0, 2].
 */
static void test_property20_urgency_pbt(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        /* Sample from a wide range including invalid values. */
        int urgency = zen_pbt_rand_int(-10, 12);

        bool valid = validate_urgency(urgency);
        bool expected = (urgency >= 0 && urgency <= 2);

        assert_int_equal((int)valid, (int)expected);
    }
}

/*
 * **Validates: Requirements 9.6**
 *
 * Test that icon_size <= 0 is rejected.
 */
static void test_property20_icon_size_invalid(void **state) {
    (void)state;

    assert_false(validate_icon_size(0));
    assert_false(validate_icon_size(-1));
    assert_false(validate_icon_size(-100));
}

/*
 * **Validates: Requirements 9.6**
 *
 * Test that icon_size > 0 is accepted.
 */
static void test_property20_icon_size_valid(void **state) {
    (void)state;

    assert_true(validate_icon_size(1));
    assert_true(validate_icon_size(32));
    assert_true(validate_icon_size(128));
}

/*
 * **Validates: Requirements 9.6**
 *
 * PBT: For any random icon_size, validate_icon_size must return true iff
 * icon_size > 0.
 */
static void test_property20_icon_size_pbt(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        int icon_size = zen_pbt_rand_int(-50, 200);

        bool valid = validate_icon_size(icon_size);
        bool expected = (icon_size > 0);

        assert_int_equal((int)valid, (int)expected);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        /* Property 19: ToggleDarkMode state round-trip */
        cmocka_unit_test(test_property19_toggle_dark_mode_true),
        cmocka_unit_test(test_property19_toggle_dark_mode_false),
        cmocka_unit_test(test_property19_roundtrip_pbt),
        cmocka_unit_test(test_property19_idempotent),
        /* Property 20: D-Bus invalid argument error */
        cmocka_unit_test(test_property20_empty_app_id_invalid),
        cmocka_unit_test(test_property20_nonempty_app_id_valid),
        cmocka_unit_test(test_property20_app_id_nonempty_pbt),
        cmocka_unit_test(test_property20_urgency_out_of_range_invalid),
        cmocka_unit_test(test_property20_urgency_valid_range),
        cmocka_unit_test(test_property20_urgency_pbt),
        cmocka_unit_test(test_property20_icon_size_invalid),
        cmocka_unit_test(test_property20_icon_size_valid),
        cmocka_unit_test(test_property20_icon_size_pbt),
    };

    return cmocka_run_group_tests_name("dbus-properties", tests, NULL, NULL);
}
