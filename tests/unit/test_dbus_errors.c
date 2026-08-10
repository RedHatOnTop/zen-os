/*
 * Zen OS — Unit Tests for D-Bus Error Definitions
 *
 * Verifies that zen_error_string() and zen_error_dbus_name() return
 * correct, non-NULL strings for all defined error codes and handle
 * out-of-range values gracefully.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen/dbus-errors.h"

/* ------------------------------------------------------------------ */
/* Test: zen_error_string returns correct strings for all known codes  */
/* ------------------------------------------------------------------ */

static void test_error_string_known_codes(void **state) {
    (void)state;

    /* ZEN_OK must return "Success" */
    assert_string_equal(zen_error_string(ZEN_OK), "Success");

    /* Spot-check several error codes */
    assert_string_equal(zen_error_string(ZEN_ERR_NOT_FOUND), "Not found");
    assert_string_equal(zen_error_string(ZEN_ERR_PERMISSION_DENIED),
                        "Permission denied");
    assert_string_equal(zen_error_string(ZEN_ERR_OUT_OF_MEMORY),
                        "Out of memory");
    assert_string_equal(zen_error_string(ZEN_ERR_IO), "I/O error");
    assert_string_equal(zen_error_string(ZEN_ERR_INTERNAL), "Internal error");

    /* Every valid code must return a non-NULL string */
    for (int i = 0; i < ZEN_ERR_COUNT; i++) {
        assert_non_null(zen_error_string((ZenError)i));
    }
}

/* ------------------------------------------------------------------ */
/* Test: zen_error_string handles out-of-range values                  */
/* ------------------------------------------------------------------ */

static void test_error_string_out_of_range(void **state) {
    (void)state;

    /* Sentinel itself is out of range */
    assert_string_equal(zen_error_string(ZEN_ERR_COUNT), "Unknown error");

    /* Negative value */
    assert_string_equal(zen_error_string((ZenError)-1), "Unknown error");

    /* Large positive value */
    assert_string_equal(zen_error_string((ZenError)9999), "Unknown error");
}

/* ------------------------------------------------------------------ */
/* Test: zen_error_dbus_name returns valid D-Bus names for known codes */
/* ------------------------------------------------------------------ */

static void test_dbus_name_known_codes(void **state) {
    (void)state;

    assert_string_equal(zen_error_dbus_name(ZEN_OK),
                        "org.zenos.Error.None");
    assert_string_equal(zen_error_dbus_name(ZEN_ERR_NOT_FOUND),
                        "org.zenos.Error.NotFound");
    assert_string_equal(zen_error_dbus_name(ZEN_ERR_INVALID_ARGUMENT),
                        "org.zenos.Error.InvalidArgument");
    assert_string_equal(zen_error_dbus_name(ZEN_ERR_TIMEOUT),
                        "org.zenos.Error.Timeout");

    /* Every valid code must start with "org.zenos.Error." */
    for (int i = 0; i < ZEN_ERR_COUNT; i++) {
        const char *name = zen_error_dbus_name((ZenError)i);
        assert_non_null(name);
        assert_true(strncmp(name, "org.zenos.Error.", 16) == 0);
    }
}

/* ------------------------------------------------------------------ */
/* Test: zen_error_dbus_name handles out-of-range values               */
/* ------------------------------------------------------------------ */

static void test_dbus_name_out_of_range(void **state) {
    (void)state;

    /* Out-of-range falls back to Internal */
    assert_string_equal(zen_error_dbus_name(ZEN_ERR_COUNT),
                        "org.zenos.Error.Internal");
    assert_string_equal(zen_error_dbus_name((ZenError)-1),
                        "org.zenos.Error.Internal");
    assert_string_equal(zen_error_dbus_name((ZenError)9999),
                        "org.zenos.Error.Internal");
}

/* ------------------------------------------------------------------ */
/* Main — register and run all tests                                   */
/* ------------------------------------------------------------------ */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_error_string_known_codes),
        cmocka_unit_test(test_error_string_out_of_range),
        cmocka_unit_test(test_dbus_name_known_codes),
        cmocka_unit_test(test_dbus_name_out_of_range),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
