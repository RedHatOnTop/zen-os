/*
 * Zen OS Shell — Property-Based Tests: Theme Config JSON Round-Trip
 *
 * Property 8: Theme config JSON round-trip
 *   For any valid ZenThemeConfig (dark_mode boolean, wallpaper_path string),
 *   serialize then deserialize produces an equivalent config.
 *
 * Property 9: Theme config unknown fields preserved
 *   For any valid theme.json string with extra unknown fields,
 *   load then save preserves the unknown fields.
 *
 * theme-config.c is compiled directly to avoid wlroots link dependencies.
 *
 * Validates: Requirements 9.5, 11.1–11.4
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt.h"
#include "zen/theme-config.h"

/* Generate a path-safe string: printable ASCII excluding '"' and '\' */
static char *rand_safe_path(int min_len, int max_len) {
    int len = zen_pbt_rand_int(min_len, max_len);
    char *s = calloc(1, (size_t)len + 1);
    if (!s) return NULL;
    for (int i = 0; i < len; i++) {
        char c;
        do {
            c = (char)zen_pbt_rand_int(0x20, 0x7E);
        } while (c == '"' || c == '\\');
        s[i] = c;
    }
    s[len] = '\0';
    return s;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 8: Theme config JSON round-trip
 *
 * For any dark_mode boolean and wallpaper_path string, serializing then
 * deserializing must produce a config with identical dark_mode and
 * wallpaper_path values.
 *
 * Validates: Requirements 9.5, 11.1–11.3
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property8_theme_config_roundtrip(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        struct ZenThemeConfig cfg;
        zen_theme_config_init(&cfg);

        /* Random dark_mode */
        cfg.dark_mode = zen_pbt_rand_bool();

        /* Random wallpaper path (safe chars: no quotes or backslashes) */
        char *path = rand_safe_path(1, 64);
        assert_non_null(path);
        free(cfg.wallpaper_path);
        cfg.wallpaper_path = path;

        /* Serialize */
        char *json = zen_theme_config_to_json(&cfg);
        assert_non_null(json);

        /* Deserialize into a fresh config */
        struct ZenThemeConfig cfg2;
        int rc = zen_theme_config_from_json(&cfg2, json);
        assert_int_equal(rc, 0);

        /* Verify dark_mode matches */
        assert_int_equal((int)cfg.dark_mode, (int)cfg2.dark_mode);

        /* Verify wallpaper_path matches */
        assert_non_null(cfg2.wallpaper_path);
        assert_string_equal(cfg.wallpaper_path, cfg2.wallpaper_path);

        /* Cleanup */
        free(json);
        zen_theme_config_cleanup(&cfg);
        zen_theme_config_cleanup(&cfg2);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 9: Theme config unknown fields preserved
 *
 * A JSON string with extra unknown fields must survive a from_json → to_json
 * cycle with the unknown keys and values still present in the output.
 *
 * Validates: Requirements 9.5, 11.4
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property9_unknown_fields_preserved(void **state) {
    (void)state;

    const char *json_with_extra =
        "{\"dark_mode\": true, \"wallpaper_path\": \"/test\","
        " \"custom_key\": \"custom_value\"}";

    struct ZenThemeConfig cfg;
    int rc = zen_theme_config_from_json(&cfg, json_with_extra);
    assert_int_equal(rc, 0);

    char *out = zen_theme_config_to_json(&cfg);
    assert_non_null(out);

    /* Unknown key and value must be present in the serialized output */
    assert_non_null(strstr(out, "custom_key"));
    assert_non_null(strstr(out, "custom_value"));

    free(out);
    zen_theme_config_cleanup(&cfg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_property8_theme_config_roundtrip),
        cmocka_unit_test(test_property9_unknown_fields_preserved),
    };

    return cmocka_run_group_tests_name("theme-config-properties",
                                       tests, NULL, NULL);
}
