/*
 * Zen OS Shell — Property-Based Tests: Shelf Config JSON Persistence
 *
 * Property 4: Shelf config JSON round-trip
 *   For any valid ZenShelfConfig, serialize then deserialize produces an
 *   equivalent config (position, auto_hide, icon_size, pinned_apps).
 *
 * Property 5: Shelf config unknown fields preserved
 *   A JSON string with extra unknown fields must survive a from_json →
 *   to_json cycle with the unknown keys still present in the output.
 *
 * Property 6: Shelf config position validation
 *   Invalid position enum values are clamped to ZEN_SHELF_BOTTOM (0) by
 *   zen_shelf_config_validate().
 *
 * Property 7: Shelf config icon_size clamping
 *   For any integer icon_size, zen_shelf_config_validate() clamps the value
 *   to [16, 64].
 *
 * shelf-config.c and models.c are compiled directly to avoid wlroots link
 * dependencies.
 *
 * Validates: Requirements 10.1–10.6
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt_shell.h"
#include "zen/shelf-config.h"
#include "zen/models.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 4: Shelf config JSON round-trip
 *
 * For any valid ZenShelfConfig, serializing then deserializing must produce
 * a config with identical position, auto_hide, icon_size, and pinned_apps.
 *
 * **Validates: Requirements 10.1, 10.2, 10.3**
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: desktop-shell, Property 4: Shelf config JSON round-trip */
static void test_shelf_config_json_roundtrip(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        ZenShelfConfig orig, restored;
        zen_pbt_rand_shelf_config(&orig);

        char *json = zen_shelf_config_to_json(&orig);
        assert_non_null(json);

        int r = zen_shelf_config_from_json(&restored, json);
        assert_int_equal(r, 0);

        /* All known fields must survive the round-trip */
        assert_int_equal((int)orig.position, (int)restored.position);
        assert_int_equal((int)orig.auto_hide, (int)restored.auto_hide);
        assert_int_equal(orig.icon_size, restored.icon_size);
        assert_int_equal(orig.pinned_apps_count, restored.pinned_apps_count);
        for (int i = 0; i < orig.pinned_apps_count; i++) {
            assert_string_equal(orig.pinned_apps[i], restored.pinned_apps[i]);
        }

        free(json);
        zen_shelf_config_cleanup(&orig);
        zen_shelf_config_cleanup(&restored);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 5: Shelf config unknown fields preserved
 *
 * A JSON string containing known fields plus unknown fields must survive a
 * from_json → to_json cycle with the unknown keys still present in the
 * serialized output.
 *
 * **Validates: Requirements 10.4**
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: desktop-shell, Property 5: Shelf config unknown fields preserved */
static void test_shelf_config_unknown_fields_preserved(void **state) {
    (void)state;

    /* JSON with all known fields plus two unknown fields */
    const char *json_with_unknown =
        "{\"position\":\"bottom\",\"auto_hide\":false,\"icon_size\":32,"
        "\"pinned_apps\":[],\"unknown_field\":\"hello\",\"another\":42}";

    ZenShelfConfig cfg;
    int r = zen_shelf_config_from_json(&cfg, json_with_unknown);
    assert_int_equal(r, 0);

    char *out = zen_shelf_config_to_json(&cfg);
    assert_non_null(out);

    /* Both unknown keys must be present in the serialized output */
    assert_non_null(strstr(out, "unknown_field"));
    assert_non_null(strstr(out, "another"));

    free(out);
    zen_shelf_config_cleanup(&cfg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 6: Shelf config position validation
 *
 * Valid position strings ("bottom", "left", "right", "top") must parse to
 * their corresponding enum values without clamping.  Any out-of-range enum
 * value passed to zen_shelf_config_validate() must be clamped to
 * ZEN_SHELF_BOTTOM (0).
 *
 * **Validates: Requirements 10.5**
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: desktop-shell, Property 6: Shelf config position validation */
static void test_shelf_config_position_validation(void **state) {
    (void)state;

    /* Valid positions must parse correctly and survive validate() unchanged */
    static const struct {
        const char      *str;
        ZenShelfPosition expected;
    } valid_cases[] = {
        { "bottom", ZEN_SHELF_BOTTOM },
        { "left",   ZEN_SHELF_LEFT   },
        { "right",  ZEN_SHELF_RIGHT  },
        { "top",    ZEN_SHELF_TOP    },
    };

    for (int i = 0; i < 4; i++) {
        /* Build a minimal JSON with this position */
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"position\":\"%s\",\"auto_hide\":false,"
                 "\"icon_size\":32,\"pinned_apps\":[]}",
                 valid_cases[i].str);

        ZenShelfConfig cfg;
        int r = zen_shelf_config_from_json(&cfg, json);
        assert_int_equal(r, 0);
        assert_int_equal((int)cfg.position, (int)valid_cases[i].expected);

        /* validate() must not change a valid position */
        int clamped = zen_shelf_config_validate(&cfg);
        assert_int_equal((int)cfg.position, (int)valid_cases[i].expected);
        (void)clamped;

        zen_shelf_config_cleanup(&cfg);
    }

    /* Invalid position strings must clamp to ZEN_SHELF_BOTTOM */
    static const char *invalid_positions[] = {
        "center", "middle", "BOTTOM", "Left", "", "42", "null",
    };
    int invalid_count = (int)(sizeof(invalid_positions) /
                               sizeof(invalid_positions[0]));

    for (int i = 0; i < invalid_count; i++) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"position\":\"%s\",\"auto_hide\":false,"
                 "\"icon_size\":32,\"pinned_apps\":[]}",
                 invalid_positions[i]);

        ZenShelfConfig cfg;
        int r = zen_shelf_config_from_json(&cfg, json);
        assert_int_equal(r, 0);
        /* from_json already clamps unknown position strings to BOTTOM */
        assert_int_equal((int)cfg.position, (int)ZEN_SHELF_BOTTOM);

        zen_shelf_config_cleanup(&cfg);
    }

    /* Out-of-range enum values (set directly) must be clamped by validate() */
    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        /* Pick a value outside [0, 3] */
        int raw = zen_pbt_rand_int(4, 200);

        ZenShelfConfig cfg;
        zen_shelf_config_init(&cfg);
        cfg.position = (ZenShelfPosition)raw;
        zen_shelf_config_validate(&cfg);

        assert_int_equal((int)cfg.position, (int)ZEN_SHELF_BOTTOM);

        zen_shelf_config_cleanup(&cfg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 7: Shelf config icon_size clamping
 *
 * For any integer icon_size (including out-of-range values), calling
 * zen_shelf_config_validate() must produce a value in [16, 64].
 *
 * **Validates: Requirements 10.6**
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: desktop-shell, Property 7: Shelf config icon_size clamping */
static void test_shelf_config_icon_size_clamping(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        /* Generate random icon_size including out-of-range values */
        int raw_size = zen_pbt_rand_int(-100, 200);

        ZenShelfConfig cfg;
        zen_shelf_config_init(&cfg);
        cfg.icon_size = raw_size;
        zen_shelf_config_validate(&cfg);

        assert_true(cfg.icon_size >= 16);
        assert_true(cfg.icon_size <= 64);

        zen_shelf_config_cleanup(&cfg);
    }

    /* Boundary values: exactly 16 and 64 must not be changed */
    {
        ZenShelfConfig cfg;
        zen_shelf_config_init(&cfg);
        cfg.icon_size = 16;
        zen_shelf_config_validate(&cfg);
        assert_int_equal(cfg.icon_size, 16);
        zen_shelf_config_cleanup(&cfg);
    }
    {
        ZenShelfConfig cfg;
        zen_shelf_config_init(&cfg);
        cfg.icon_size = 64;
        zen_shelf_config_validate(&cfg);
        assert_int_equal(cfg.icon_size, 64);
        zen_shelf_config_cleanup(&cfg);
    }

    /* Values just outside the bounds must be clamped */
    {
        ZenShelfConfig cfg;
        zen_shelf_config_init(&cfg);
        cfg.icon_size = 15;
        zen_shelf_config_validate(&cfg);
        assert_int_equal(cfg.icon_size, 16);
        zen_shelf_config_cleanup(&cfg);
    }
    {
        ZenShelfConfig cfg;
        zen_shelf_config_init(&cfg);
        cfg.icon_size = 65;
        zen_shelf_config_validate(&cfg);
        assert_int_equal(cfg.icon_size, 64);
        zen_shelf_config_cleanup(&cfg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_shelf_config_json_roundtrip),
        cmocka_unit_test(test_shelf_config_unknown_fields_preserved),
        cmocka_unit_test(test_shelf_config_position_validation),
        cmocka_unit_test(test_shelf_config_icon_size_clamping),
    };

    return cmocka_run_group_tests_name("shelf-config-properties",
                                       tests, NULL, NULL);
}
