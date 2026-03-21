/*
 * Zen OS — Property-Based Tests for Desktop Wallpaper Module
 *
 * Tests Properties 13 and 14 from the Phase 1 design document.
 * Uses CMocka + zen_pbt.h for randomized input generation.
 *
 * Property 13: Wallpaper aspect-fill scaling invariants
 *   For any image (img_w, img_h) and output (out_w, out_h), the
 *   aspect-fill scale factor must produce scaled dimensions such that:
 *     (a) s_w >= out_w  AND  s_h >= out_h   (image covers the output)
 *     (b) |s_w/s_h - img_w/img_h| < epsilon  (aspect ratio preserved)
 *
 * Property 14: theme.json wallpaper path round-trip
 *   For any valid filesystem path written to a temp theme.json as the
 *   "wallpaper_path" value, parsing the file must return the same path.
 *
 * These tests exercise the pure logic directly without requiring a running
 * Wayland display or wlroots.  The aspect-fill scale function and the
 * theme.json parser are re-implemented here as they are static in
 * wallpaper.c — this is the standard white-box PBT approach for static C.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt.h"

/* ── Aspect-fill scale (mirrors wallpaper.c static function) ─────────────── */

/*
 * Compute the aspect-fill (cover) scale factor.
 * Returns the larger of (out_w/img_w) and (out_h/img_h) so that the
 * scaled image covers the output in both dimensions.
 */
static double compute_aspect_fill_scale(int img_w, int img_h,
                                         int out_w, int out_h) {
    double scale_x = (double)out_w / (double)img_w;
    double scale_y = (double)out_h / (double)img_h;
    return (scale_x > scale_y) ? scale_x : scale_y;
}

/* ── theme.json parser (mirrors wallpaper.c static function) ─────────────── */

/*
 * Parse a theme.json file at `path` for the "wallpaper_path" key.
 * Returns a heap-allocated path string on success (caller must free),
 * or NULL if the key is absent or the file cannot be read.
 *
 * This is a verbatim copy of the static parse_wallpaper_path_from_theme()
 * logic from wallpaper.c, adapted to accept an explicit path argument so
 * it can be tested against temp files without touching $HOME.
 */
static char *parse_wallpaper_path_from_file(const char *json_path) {
    char *result = NULL;
    char *buf    = NULL;
    FILE *f      = NULL;

    f = fopen(json_path, "r");
    if (!f) {
        goto cleanup;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize <= 0 || fsize > 65536) {
        goto cleanup;
    }

    buf = malloc((size_t)fsize + 1);
    if (!buf) {
        goto cleanup;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        goto cleanup;
    }
    buf[fsize] = '\0';

    const char *key = "\"wallpaper_path\"";
    char *pos = strstr(buf, key);
    if (!pos) {
        goto cleanup;
    }
    pos += strlen(key);

    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        pos++;
    }
    if (*pos != ':') {
        goto cleanup;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        pos++;
    }
    if (*pos != '"') {
        goto cleanup;
    }
    pos++;

    const char *start = pos;
    while (*pos && *pos != '"') {
        if (*pos == '\\') {
            pos++;
            if (!*pos) break;
        }
        pos++;
    }
    if (*pos != '"') {
        goto cleanup;
    }

    size_t val_len = (size_t)(pos - start);
    result = malloc(val_len + 1);
    if (!result) {
        goto cleanup;
    }
    memcpy(result, start, val_len);
    result[val_len] = '\0';

cleanup:
    free(buf);
    if (f) fclose(f);
    return result;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * Write a minimal theme.json containing only the wallpaper_path key.
 * Returns 0 on success, -1 on failure.
 */
static int write_theme_json(const char *json_path, const char *wallpaper_path) {
    FILE *f = fopen(json_path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "{\n    \"wallpaper_path\": \"%s\"\n}\n", wallpaper_path);
    fclose(f);
    return 0;
}

/*
 * Generate a random absolute-style path string like "/tmp/abc/wall.png".
 * Uses only path-safe characters (alphanumeric + underscore).
 * Caller must free().
 */
static char *rand_path(void) {
    /* prefix + 8-char dir + "/" + 8-char file + ".png" + NUL */
    const char *prefix = "/tmp/zen_test_";
    int dir_len  = zen_pbt_rand_int(4, 12);
    int file_len = zen_pbt_rand_int(4, 12);
    size_t total = strlen(prefix) + (size_t)dir_len + 1 + (size_t)file_len
                   + sizeof(".png");
    char *path = calloc(1, total);
    if (!path) {
        return NULL;
    }

    static const char safe[] = "abcdefghijklmnopqrstuvwxyz0123456789_";
    size_t safe_len = sizeof(safe) - 1;

    strcpy(path, prefix);
    char *p = path + strlen(prefix);
    for (int i = 0; i < dir_len; i++) {
        *p++ = safe[rand() % safe_len];
    }
    *p++ = '/';
    for (int i = 0; i < file_len; i++) {
        *p++ = safe[rand() % safe_len];
    }
    strcpy(p, ".png");
    return path;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 13: Wallpaper aspect-fill scaling invariants
 *
 * For any image (img_w, img_h) and output (out_w, out_h), the scaled
 * dimensions s_w = img_w * scale and s_h = img_h * scale must satisfy:
 *   (a) s_w >= out_w  AND  s_h >= out_h   (covers the output)
 *   (b) |s_w/s_h - img_w/img_h| < 1e-9   (aspect ratio preserved)
 *
 * Validates: Requirement 6.2
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 13: aspect-fill covers output */
static void test_property13_aspect_fill_covers_output(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int img_w, img_h, out_w, out_h;
        zen_pbt_rand_dimensions(4096, 4096, &img_w, &img_h);
        zen_pbt_rand_dimensions(3840, 2160, &out_w, &out_h);

        double scale    = compute_aspect_fill_scale(img_w, img_h, out_w, out_h);
        double scaled_w = img_w * scale;
        double scaled_h = img_h * scale;

        /*
         * (a) Scaled image must cover the output in both dimensions.
         * Allow a tiny floating-point epsilon for rounding.
         */
        assert_true(scaled_w >= (double)out_w - 1e-9);
        assert_true(scaled_h >= (double)out_h - 1e-9);
    }
}

/* Feature: phase1-foundation, Property 13: aspect-fill preserves aspect ratio */
static void test_property13_aspect_fill_preserves_ratio(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int img_w, img_h, out_w, out_h;
        zen_pbt_rand_dimensions(4096, 4096, &img_w, &img_h);
        zen_pbt_rand_dimensions(3840, 2160, &out_w, &out_h);

        double scale    = compute_aspect_fill_scale(img_w, img_h, out_w, out_h);
        double scaled_w = img_w * scale;
        double scaled_h = img_h * scale;

        /*
         * (b) Aspect ratio of scaled image must match original.
         * |s_w/s_h - img_w/img_h| < epsilon
         */
        double orig_ratio   = (double)img_w / (double)img_h;
        double scaled_ratio = scaled_w / scaled_h;
        assert_true(fabs(scaled_ratio - orig_ratio) < 1e-9);
    }
}

/* Feature: phase1-foundation, Property 13: scale >= 1 when image smaller than output */
static void test_property13_scale_upscales_small_image(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        /* Image is strictly smaller than output in at least one dimension. */
        int out_w = zen_pbt_rand_int(800, 3840);
        int out_h = zen_pbt_rand_int(600, 2160);
        /* Image is at most half the output size. */
        int img_w = zen_pbt_rand_int(1, out_w / 2);
        int img_h = zen_pbt_rand_int(1, out_h / 2);

        double scale    = compute_aspect_fill_scale(img_w, img_h, out_w, out_h);
        double scaled_w = img_w * scale;
        double scaled_h = img_h * scale;

        /* Must still cover the output. */
        assert_true(scaled_w >= (double)out_w - 1e-9);
        assert_true(scaled_h >= (double)out_h - 1e-9);
        /* Scale must be >= 1 since image is smaller. */
        assert_true(scale >= 1.0);
    }
}

/* Feature: phase1-foundation, Property 13: square image on square output */
static void test_property13_square_image_square_output(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int side = zen_pbt_rand_int(1, 4096);
        double scale = compute_aspect_fill_scale(side, side, side, side);

        /* Exact same dimensions: scale must be exactly 1.0. */
        assert_true(fabs(scale - 1.0) < 1e-12);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 14: theme.json wallpaper path round-trip
 *
 * For any valid path string written to a temp theme.json as the
 * "wallpaper_path" value, parsing the file must return the same path.
 *
 * Validates: Requirement 6.5
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 14: theme.json path round-trip */
static void test_property14_theme_json_path_roundtrip(void **state) {
    (void)state;

    /* Use a fixed temp file path; overwrite on each iteration. */
    char tmp_json[] = "/tmp/zen_test_theme_XXXXXX.json";
    int fd = mkstemps(tmp_json, 5); /* 5 = strlen(".json") */
    if (fd < 0) {
        fail_msg("failed to create temp file for theme.json test");
        return;
    }
    close(fd);

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        char *expected_path = rand_path();
        assert_non_null(expected_path);

        int rc = write_theme_json(tmp_json, expected_path);
        assert_int_equal(rc, 0);

        char *parsed_path = parse_wallpaper_path_from_file(tmp_json);
        assert_non_null(parsed_path);
        assert_string_equal(parsed_path, expected_path);

        free(parsed_path);
        free(expected_path);
    }

    unlink(tmp_json);
}

/* Feature: phase1-foundation, Property 14: missing key returns NULL */
static void test_property14_missing_key_returns_null(void **state) {
    (void)state;

    char tmp_json[] = "/tmp/zen_test_theme_nokey_XXXXXX.json";
    int fd = mkstemps(tmp_json, 5);
    if (fd < 0) {
        fail_msg("failed to create temp file");
        return;
    }
    close(fd);

    /* Write JSON without the wallpaper_path key. */
    FILE *f = fopen(tmp_json, "w");
    assert_non_null(f);
    fprintf(f, "{\n    \"other_key\": \"some_value\"\n}\n");
    fclose(f);

    char *result = parse_wallpaper_path_from_file(tmp_json);
    assert_null(result);

    unlink(tmp_json);
}

/* Feature: phase1-foundation, Property 14: nonexistent file returns NULL */
static void test_property14_nonexistent_file_returns_null(void **state) {
    (void)state;

    char *result = parse_wallpaper_path_from_file(
        "/tmp/zen_test_this_file_does_not_exist_12345.json");
    assert_null(result);
}

/* Feature: phase1-foundation, Property 14: extra keys do not affect parse */
static void test_property14_extra_keys_ignored(void **state) {
    (void)state;

    char tmp_json[] = "/tmp/zen_test_theme_extra_XXXXXX.json";
    int fd = mkstemps(tmp_json, 5);
    if (fd < 0) {
        fail_msg("failed to create temp file");
        return;
    }
    close(fd);

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        char *expected_path = rand_path();
        assert_non_null(expected_path);

        /* Write JSON with extra keys before and after wallpaper_path. */
        FILE *f = fopen(tmp_json, "w");
        assert_non_null(f);
        fprintf(f,
                "{\n"
                "    \"theme\": \"dark\",\n"
                "    \"font_size\": 14,\n"
                "    \"wallpaper_path\": \"%s\",\n"
                "    \"accent_color\": \"#7c3aed\"\n"
                "}\n",
                expected_path);
        fclose(f);

        char *parsed_path = parse_wallpaper_path_from_file(tmp_json);
        assert_non_null(parsed_path);
        assert_string_equal(parsed_path, expected_path);

        free(parsed_path);
        free(expected_path);
    }

    unlink(tmp_json);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        /* Property 13 */
        cmocka_unit_test(test_property13_aspect_fill_covers_output),
        cmocka_unit_test(test_property13_aspect_fill_preserves_ratio),
        cmocka_unit_test(test_property13_scale_upscales_small_image),
        cmocka_unit_test(test_property13_square_image_square_output),
        /* Property 14 */
        cmocka_unit_test(test_property14_theme_json_path_roundtrip),
        cmocka_unit_test(test_property14_missing_key_returns_null),
        cmocka_unit_test(test_property14_nonexistent_file_returns_null),
        cmocka_unit_test(test_property14_extra_keys_ignored),
    };

    return cmocka_run_group_tests_name("wallpaper-properties", tests, NULL, NULL);
}
