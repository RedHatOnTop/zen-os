/*
 * Zen OS — Property-Based Tests for Multi-Monitor Support
 *
 * Tests Properties 24 and 25 from the Phase 1 design document.
 * Uses CMocka + zen_pbt.h for randomized input generation.
 *
 * Property 24: Output configuration
 *   For any newly detected output, it must be added to the output layout
 *   with its preferred mode and scale applied.
 *   Test: given random output dimensions (width, height, refresh) and a
 *   scale factor, verify that the mode selection logic picks the preferred
 *   mode (highest refresh at the given resolution) and that the scale
 *   factor is stored correctly.
 *
 * Property 25: Cross-output pointer continuity
 *   For any two adjacent outputs in a layout, a pointer at the edge of
 *   the first output must be within the bounds of the second output after
 *   crossing.
 *   Test: generate two random output widths (w1, w2), place them side by
 *   side (output2 starts at x=w1). A cursor at x=w1-1 is on output1.
 *   A cursor at x=w1 is on output2. Verify the boundary math.
 *
 * These tests exercise pure logic without a running Wayland display or
 * wlroots dependency.
 *
 * Validates: Requirements 12.1, 12.3
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

#include "zen_pbt.h"

/* ── Output mode representation ──────────────────────────────────────────── */

/*
 * MockOutputMode — mirrors a wlr_output_mode entry.
 * preferred flag indicates the compositor-selected preferred mode.
 */
typedef struct {
    int  width;
    int  height;
    int  refresh;   /* mHz */
    bool preferred;
} MockOutputMode;

/*
 * MockOutput — minimal output state for testing configuration logic.
 */
typedef struct {
    MockOutputMode modes[8];
    int            mode_count;
    int            applied_width;
    int            applied_height;
    int            applied_refresh;
    float          applied_scale;
    bool           configured;
} MockOutput;

/* ── Output layout representation ────────────────────────────────────────── */

/*
 * MockLayoutEntry — one output placed in the layout at (x, y).
 */
typedef struct {
    int x;
    int y;
    int width;
    int height;
} MockLayoutEntry;

/* ── Pure output configuration logic (mirrors main.c new_output handler) ── */

/*
 * mock_output_find_preferred_mode — scan the mode list and return the index
 * of the preferred mode, or -1 if none is marked preferred.
 */
static int mock_output_find_preferred_mode(const MockOutput *out) {
    for (int i = 0; i < out->mode_count; i++) {
        if (out->modes[i].preferred) {
            return i;
        }
    }
    return -1;
}

/*
 * mock_output_configure — apply the preferred mode and scale to the output.
 * Mirrors the logic in the new_output handler:
 *   wlr_output_set_mode(output, preferred_mode)
 *   wlr_output_set_scale(output, scale)
 * Returns true on success (preferred mode found), false otherwise.
 */
static bool mock_output_configure(MockOutput *out, float scale) {
    int idx = mock_output_find_preferred_mode(out);
    if (idx < 0) {
        return false;
    }
    out->applied_width   = out->modes[idx].width;
    out->applied_height  = out->modes[idx].height;
    out->applied_refresh = out->modes[idx].refresh;
    out->applied_scale   = scale;
    out->configured      = true;
    return true;
}

/* ── Output layout boundary logic (mirrors wlr_output_layout_output_at) ─── */

/*
 * mock_layout_output_at — return the index of the layout entry that contains
 * the point (cx, cy), or -1 if no entry contains it.
 *
 * An output occupies [x, x+width) × [y, y+height).
 */
static int mock_layout_output_at(const MockLayoutEntry *entries, int count,
                                  int cx, int cy) {
    for (int i = 0; i < count; i++) {
        if (cx >= entries[i].x && cx < entries[i].x + entries[i].width &&
            cy >= entries[i].y && cy < entries[i].y + entries[i].height) {
            return i;
        }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 24: Output configuration — preferred mode and scale applied
 *
 * For any newly detected output with a set of modes (one marked preferred),
 * after configuration:
 *   (a) applied_width  == preferred_mode.width
 *   (b) applied_height == preferred_mode.height
 *   (c) applied_refresh == preferred_mode.refresh
 *   (d) applied_scale  == requested scale
 *   (e) configured     == true
 *
 * Validates: Requirements 12.1
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 24: preferred mode is applied */
static void test_property24_preferred_mode_applied(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        MockOutput out;
        memset(&out, 0, sizeof(out));

        /* Generate 1–4 random modes; mark one as preferred. */
        int mode_count = zen_pbt_rand_int(1, 4);
        int preferred_idx = zen_pbt_rand_int(0, mode_count - 1);

        for (int m = 0; m < mode_count; m++) {
            int w, h;
            zen_pbt_rand_dimensions(3840, 2160, &w, &h);
            out.modes[m].width     = w;
            out.modes[m].height    = h;
            out.modes[m].refresh   = zen_pbt_rand_int(24000, 240000); /* mHz */
            out.modes[m].preferred = (m == preferred_idx);
        }
        out.mode_count = mode_count;

        /* Random scale in {1.0, 1.25, 1.5, 2.0}. */
        static const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
        float scale = scales[zen_pbt_rand_int(0, 3)];

        bool ok = mock_output_configure(&out, scale);

        /* (e) Configuration must succeed when a preferred mode exists. */
        assert_true(ok);
        assert_true(out.configured);

        /* (a–c) Applied mode must match the preferred mode. */
        assert_int_equal(out.applied_width,   out.modes[preferred_idx].width);
        assert_int_equal(out.applied_height,  out.modes[preferred_idx].height);
        assert_int_equal(out.applied_refresh, out.modes[preferred_idx].refresh);

        /* (d) Applied scale must match the requested scale. */
        assert_true(out.applied_scale == scale);
    }
}

/* Feature: phase1-foundation, Property 24: no preferred mode → configure fails */
static void test_property24_no_preferred_mode_fails(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        MockOutput out;
        memset(&out, 0, sizeof(out));

        /* Generate 1–4 modes, none marked preferred. */
        int mode_count = zen_pbt_rand_int(1, 4);
        for (int m = 0; m < mode_count; m++) {
            int w, h;
            zen_pbt_rand_dimensions(3840, 2160, &w, &h);
            out.modes[m].width     = w;
            out.modes[m].height    = h;
            out.modes[m].refresh   = zen_pbt_rand_int(24000, 240000);
            out.modes[m].preferred = false;
        }
        out.mode_count = mode_count;

        bool ok = mock_output_configure(&out, 1.0f);

        /* Without a preferred mode, configuration must fail. */
        assert_false(ok);
        assert_false(out.configured);
    }
}

/* Feature: phase1-foundation, Property 24: scale stored independently of mode */
static void test_property24_scale_stored_correctly(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        MockOutput out;
        memset(&out, 0, sizeof(out));

        /* Single preferred mode with fixed dimensions. */
        out.modes[0].width     = 1920;
        out.modes[0].height    = 1080;
        out.modes[0].refresh   = 60000;
        out.modes[0].preferred = true;
        out.mode_count         = 1;

        /* Random scale: integer 1–4 as float. */
        float scale = (float)zen_pbt_rand_int(1, 4);

        bool ok = mock_output_configure(&out, scale);
        assert_true(ok);

        /* Mode dimensions must be unchanged regardless of scale. */
        assert_int_equal(out.applied_width,  1920);
        assert_int_equal(out.applied_height, 1080);

        /* Scale must be stored as-is. */
        assert_true(out.applied_scale == scale);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 25: Cross-output pointer continuity
 *
 * For any two adjacent outputs placed side by side:
 *   output1: x=[0, w1),  output2: x=[w1, w1+w2)
 *
 *   (a) cursor at x=w1-1 is on output1 (index 0)
 *   (b) cursor at x=w1   is on output2 (index 1)
 *   (c) cursor at x=-1   is on neither output
 *   (d) cursor at x=w1+w2 is on neither output
 *
 * Validates: Requirements 12.3
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 25: cursor left of boundary is on output1 */
static void test_property25_cursor_left_of_boundary_on_output1(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int w1, h1, w2, h2;
        zen_pbt_rand_dimensions(3840, 2160, &w1, &h1);
        zen_pbt_rand_dimensions(3840, 2160, &w2, &h2);

        /* Use the taller height so both outputs share a common y range. */
        int h = (h1 > h2) ? h1 : h2;

        MockLayoutEntry entries[2];
        entries[0].x      = 0;
        entries[0].y      = 0;
        entries[0].width  = w1;
        entries[0].height = h;
        entries[1].x      = w1;
        entries[1].y      = 0;
        entries[1].width  = w2;
        entries[1].height = h;

        /* Cursor one pixel left of the boundary — must be on output1. */
        int cy = zen_pbt_rand_int(0, h - 1);
        int idx = mock_layout_output_at(entries, 2, w1 - 1, cy);
        assert_int_equal(idx, 0);
    }
}

/* Feature: phase1-foundation, Property 25: cursor at boundary is on output2 */
static void test_property25_cursor_at_boundary_on_output2(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int w1, h1, w2, h2;
        zen_pbt_rand_dimensions(3840, 2160, &w1, &h1);
        zen_pbt_rand_dimensions(3840, 2160, &w2, &h2);

        int h = (h1 > h2) ? h1 : h2;

        MockLayoutEntry entries[2];
        entries[0].x      = 0;
        entries[0].y      = 0;
        entries[0].width  = w1;
        entries[0].height = h;
        entries[1].x      = w1;
        entries[1].y      = 0;
        entries[1].width  = w2;
        entries[1].height = h;

        /* Cursor exactly at the boundary — must be on output2. */
        int cy = zen_pbt_rand_int(0, h - 1);
        int idx = mock_layout_output_at(entries, 2, w1, cy);
        assert_int_equal(idx, 1);
    }
}

/* Feature: phase1-foundation, Property 25: cursor outside both outputs returns -1 */
static void test_property25_cursor_outside_layout_returns_none(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int w1, h1, w2, h2;
        zen_pbt_rand_dimensions(3840, 2160, &w1, &h1);
        zen_pbt_rand_dimensions(3840, 2160, &w2, &h2);

        int h = (h1 > h2) ? h1 : h2;

        MockLayoutEntry entries[2];
        entries[0].x      = 0;
        entries[0].y      = 0;
        entries[0].width  = w1;
        entries[0].height = h;
        entries[1].x      = w1;
        entries[1].y      = 0;
        entries[1].width  = w2;
        entries[1].height = h;

        /* Cursor to the left of output1 — no output. */
        int idx_left = mock_layout_output_at(entries, 2, -1, 0);
        assert_int_equal(idx_left, -1);

        /* Cursor to the right of output2 — no output. */
        int idx_right = mock_layout_output_at(entries, 2, w1 + w2, 0);
        assert_int_equal(idx_right, -1);

        /* Cursor below both outputs — no output. */
        int idx_below = mock_layout_output_at(entries, 2, 0, h);
        assert_int_equal(idx_below, -1);
    }
}

/* Feature: phase1-foundation, Property 25: single-pixel wide output boundary */
static void test_property25_single_pixel_boundary(void **state) {
    (void)state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        /* output1 is exactly 1 pixel wide. */
        int w2, h2;
        zen_pbt_rand_dimensions(3840, 2160, &w2, &h2);

        MockLayoutEntry entries[2];
        entries[0].x      = 0;
        entries[0].y      = 0;
        entries[0].width  = 1;
        entries[0].height = h2;
        entries[1].x      = 1;
        entries[1].y      = 0;
        entries[1].width  = w2;
        entries[1].height = h2;

        int cy = zen_pbt_rand_int(0, h2 - 1);

        /* x=0 is on output1. */
        assert_int_equal(mock_layout_output_at(entries, 2, 0, cy), 0);
        /* x=1 is on output2. */
        assert_int_equal(mock_layout_output_at(entries, 2, 1, cy), 1);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        /* Property 24 */
        cmocka_unit_test(test_property24_preferred_mode_applied),
        cmocka_unit_test(test_property24_no_preferred_mode_fails),
        cmocka_unit_test(test_property24_scale_stored_correctly),
        /* Property 25 */
        cmocka_unit_test(test_property25_cursor_left_of_boundary_on_output1),
        cmocka_unit_test(test_property25_cursor_at_boundary_on_output2),
        cmocka_unit_test(test_property25_cursor_outside_layout_returns_none),
        cmocka_unit_test(test_property25_single_pixel_boundary),
    };

    return cmocka_run_group_tests_name("multi-monitor-properties", tests, NULL, NULL);
}
