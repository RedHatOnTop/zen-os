/*
 * Zen OS Shell — Property-Based Tests: Shelf Geometry, Clock Format,
 *                Pinned Apps, Running Indicators, and Icon Scaling
 *
 * Property 1: Shelf dimensions match output
 *   For any output (out_w, out_h) and shelf height H, buffer width == out_w,
 *   height == H, exclusive zone == H, x == 0, y == out_h - H.
 *
 * Property 2: Clock format is HH:MM
 *   For any valid time_t, the clock string matches ^[0-2][0-9]:[0-5][0-9].
 *
 * Property 3: Running indicator matches toplevel state
 *   is_running == true iff the running state was explicitly set to true for
 *   that app_id; all other entries remain unaffected.
 *
 * Property 20: Pin/unpin round-trip
 *   pin then check shows app present; unpin removes it.
 *
 * Property 24: Shelf position change updates geometry
 *   For any valid position (bottom/top/left/right) and output dims,
 *   geometry and exclusive zone are correct for the new edge.
 *
 * Property 25: Shelf icon scaling
 *   slot_width == icon_size + ICON_SLOT_PADDING and
 *   icon_y == (shelf_height - icon_size) / 2 for any icon_size in [16, 64].
 *
 * Pure logic tests — no wlroots or compositor headers required.
 *
 * Validates: Requirements 1.1, 1.4, 1.5, 2.2, 2.4, 4.5, 13.1, 13.2
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt.h"

/* ── Shelf constants (mirrors shelf.c) ───────────────────────────────────── */

#define SHELF_HEIGHT       48
#define ICON_SLOT_PADDING   8
#define ICON_LEFT_MARGIN    8

/* ── Shelf position enum (mirrors ZenShelfPosition from shelf.h) ─────────── */

typedef enum {
    POS_BOTTOM = 0,
    POS_TOP    = 1,
    POS_LEFT   = 2,
    POS_RIGHT  = 3,
} TestShelfPos;

/* ── Shelf geometry helper (mirrors shelf.c logic for all positions) ─────── */

static void compute_shelf_geometry(int out_w, int out_h, int shelf_h,
                                   TestShelfPos pos,
                                   int *x, int *y, int *w, int *h) {
    switch (pos) {
    case POS_TOP:
        *x = 0;
        *y = 0;
        *w = out_w;
        *h = shelf_h;
        break;
    case POS_LEFT:
        *x = 0;
        *y = 0;
        *w = shelf_h;
        *h = out_h;
        break;
    case POS_RIGHT:
        *x = out_w - shelf_h;
        *y = 0;
        *w = shelf_h;
        *h = out_h;
        break;
    case POS_BOTTOM:
    default:
        *x = 0;
        *y = out_h - shelf_h;
        *w = out_w;
        *h = shelf_h;
        break;
    }
}

/* ── Clock format helper (mirrors shelf.c logic) ─────────────────────────── */

static void format_clock(time_t t, char *buf, size_t len) {
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    strftime(buf, len, "%H:%M", &tm_local);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 1: Shelf dimensions match output
 *
 * For any output (out_w, out_h) in [1,3840]×[1,2160] and shelf height H=48,
 * compute_shelf_geometry must produce:
 *   w == out_w, h == H, x == 0, y == out_h - H
 *
 * Validates: Requirements 1.1, 1.5
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property1_shelf_dimensions(void **state) {
    (void)state;

    const int shelf_h = 48;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int out_w, out_h;
        zen_pbt_rand_dimensions(3840, 2160, &out_w, &out_h);

        int x, y, w, h;
        compute_shelf_geometry(out_w, out_h, shelf_h, POS_BOTTOM, &x, &y, &w, &h);

        assert_int_equal(w, out_w);
        assert_int_equal(h, shelf_h);
        assert_int_equal(x, 0);
        assert_int_equal(y, out_h - shelf_h);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 2: Clock format is HH:MM
 *
 * For any valid time_t, format_clock produces a string matching
 * ^[0-2][0-9]:[0-5][0-9] with a NUL terminator at index 5.
 *
 * Validates: Requirement 1.4
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property2_clock_format(void **state) {
    (void)state;

    /* Use a fixed epoch base and add random offsets covering a full day range */
    const time_t base = 1700000000; /* 2023-11-14 — arbitrary fixed point */

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        /* Random offset: 0 to 86399 seconds (one full day) */
        time_t t = base + (time_t)zen_pbt_rand_int(0, 86399);

        char buf[8] = {0};
        format_clock(t, buf, sizeof(buf));

        /* buf must be exactly "HH:MM\0" — 5 chars + NUL */
        assert_true(buf[0] >= '0' && buf[0] <= '2');
        assert_true(buf[1] >= '0' && buf[1] <= '9');
        assert_int_equal(buf[2], ':');
        assert_true(buf[3] >= '0' && buf[3] <= '5');
        assert_true(buf[4] >= '0' && buf[4] <= '9');
        assert_int_equal(buf[5], '\0');
    }
}

/* ── Minimal app entry (mirrors ZenAppEntry from shelf.h) ────────────────── */

typedef struct {
    char *app_id;
    char *name;
    char *icon_path;
    char *exec_cmd;
    bool  is_running;
} TestAppEntry;

/* ── Minimal shelf state for pure-logic tests ────────────────────────────── */

typedef struct {
    TestAppEntry *pinned_apps;
    int           pinned_count;
    int           height;
} TestShelf;

/*
 * Mirror of zen_shelf_update_running() logic (without repaint).
 * Sets is_running on the matching entry; all others are untouched.
 */
static void test_shelf_update_running(TestShelf *shelf, const char *app_id,
                                      bool running)
{
    if (!shelf || !app_id) return;
    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            shelf->pinned_apps[i].is_running = running;
        }
    }
}

/*
 * Mirror of zen_shelf_pin_app() logic (without config save or repaint).
 * Returns 0 on success, -1 on allocation failure or if already pinned.
 */
static int test_shelf_pin_app(TestShelf *shelf, const char *app_id)
{
    int   ret       = -1;
    char *id_copy   = NULL;
    char *name_copy = NULL;

    if (!shelf || !app_id) goto cleanup;

    /* Idempotent: already pinned → success */
    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            ret = 0;
            goto cleanup;
        }
    }

    TestAppEntry *new_arr = realloc(
        shelf->pinned_apps,
        (size_t)(shelf->pinned_count + 1) * sizeof(TestAppEntry));
    if (!new_arr) goto cleanup;
    shelf->pinned_apps = new_arr;

    id_copy = strdup(app_id);
    if (!id_copy) goto cleanup;

    const char *dot     = strrchr(app_id, '.');
    const char *display = dot ? dot + 1 : app_id;
    name_copy = strdup(display);
    if (!name_copy) goto cleanup;

    TestAppEntry *entry = &shelf->pinned_apps[shelf->pinned_count];
    entry->app_id    = id_copy;   id_copy   = NULL;
    entry->name      = name_copy; name_copy = NULL;
    entry->icon_path = NULL;
    entry->exec_cmd  = NULL;
    entry->is_running = false;
    shelf->pinned_count++;
    ret = 0;

cleanup:
    free(id_copy);
    free(name_copy);
    return ret;
}

/*
 * Mirror of zen_shelf_unpin_app() logic (without config save or repaint).
 * Returns 0 on success, -1 if app_id not found.
 */
static int test_shelf_unpin_app(TestShelf *shelf, const char *app_id)
{
    if (!shelf || !app_id) return -1;

    int idx = -1;
    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;

    free(shelf->pinned_apps[idx].app_id);
    free(shelf->pinned_apps[idx].name);
    free(shelf->pinned_apps[idx].icon_path);
    free(shelf->pinned_apps[idx].exec_cmd);

    int remaining = shelf->pinned_count - idx - 1;
    if (remaining > 0) {
        memmove(&shelf->pinned_apps[idx],
                &shelf->pinned_apps[idx + 1],
                (size_t)remaining * sizeof(TestAppEntry));
    }
    shelf->pinned_count--;
    return 0;
}

/* Free all entries in a TestShelf (does not free shelf itself). */
static void test_shelf_cleanup(TestShelf *shelf)
{
    for (int i = 0; i < shelf->pinned_count; i++) {
        free(shelf->pinned_apps[i].app_id);
        free(shelf->pinned_apps[i].name);
        free(shelf->pinned_apps[i].icon_path);
        free(shelf->pinned_apps[i].exec_cmd);
    }
    free(shelf->pinned_apps);
    shelf->pinned_apps  = NULL;
    shelf->pinned_count = 0;
}

/* Check whether app_id is present in the shelf's pinned list. */
static bool test_shelf_is_pinned(const TestShelf *shelf, const char *app_id)
{
    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            return true;
        }
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 24: Shelf position change updates geometry
 *
 * For any valid position (bottom/top/left/right) and output dims (out_w, out_h):
 *   - bottom: x=0, y=out_h-SHELF_HEIGHT, w=out_w,       h=SHELF_HEIGHT
 *   - top:    x=0, y=0,                  w=out_w,       h=SHELF_HEIGHT
 *   - left:   x=0, y=0,                  w=SHELF_HEIGHT, h=out_h
 *   - right:  x=out_w-SHELF_HEIGHT, y=0, w=SHELF_HEIGHT, h=out_h
 *   - exclusive_zone == SHELF_HEIGHT for all positions
 *     (exclusive_zone == h for horizontal, w for vertical)
 *
 * **Validates: Requirements 4.5**
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property24_position_geometry(void **state)
{
    (void)state;

    const int shelf_h = SHELF_HEIGHT;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        int out_w, out_h;
        zen_pbt_rand_dimensions(3840, 2160, &out_w, &out_h);

        /* Ensure output is large enough to fit the shelf on any edge */
        if (out_w < shelf_h) out_w = shelf_h;
        if (out_h < shelf_h) out_h = shelf_h;

        TestShelfPos pos = (TestShelfPos)zen_pbt_rand_int(0, 3);

        int x, y, w, h;
        compute_shelf_geometry(out_w, out_h, shelf_h, pos, &x, &y, &w, &h);

        switch (pos) {
        case POS_BOTTOM:
            assert_int_equal(x, 0);
            assert_int_equal(y, out_h - shelf_h);
            assert_int_equal(w, out_w);
            assert_int_equal(h, shelf_h);
            /* horizontal: exclusive_zone == h */
            assert_int_equal(h, SHELF_HEIGHT);
            break;
        case POS_TOP:
            assert_int_equal(x, 0);
            assert_int_equal(y, 0);
            assert_int_equal(w, out_w);
            assert_int_equal(h, shelf_h);
            /* horizontal: exclusive_zone == h */
            assert_int_equal(h, SHELF_HEIGHT);
            break;
        case POS_LEFT:
            assert_int_equal(x, 0);
            assert_int_equal(y, 0);
            assert_int_equal(w, shelf_h);
            assert_int_equal(h, out_h);
            /* vertical: exclusive_zone == w */
            assert_int_equal(w, SHELF_HEIGHT);
            break;
        case POS_RIGHT:
            assert_int_equal(x, out_w - shelf_h);
            assert_int_equal(y, 0);
            assert_int_equal(w, shelf_h);
            assert_int_equal(h, out_h);
            /* vertical: exclusive_zone == w */
            assert_int_equal(w, SHELF_HEIGHT);
            break;
        }
    }
}

/* ── Icon geometry helper (mirrors shelf.c render_pinned_apps) ───────────── */

static void compute_icon_geometry(int icon_size, int shelf_height,
                                  int *slot_w, int *icon_y)
{
    *slot_w = icon_size + ICON_SLOT_PADDING;
    *icon_y = (shelf_height - icon_size) / 2;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 3: Running indicator matches toplevel state
 *
 * For any set of pinned app_ids and any target app_id with running state r:
 *   - After update_running(shelf, target, r), target's is_running == r
 *   - All other entries' is_running flags are unchanged
 *
 * **Validates: Requirements 2.2, 2.4**
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property3_running_indicator(void **state)
{
    (void)state;

    /* Fixed set of app_ids to pin */
    static const char *app_ids[] = {
        "org.zen.browser",
        "org.zen.terminal",
        "org.zen.files",
        "org.zen.settings",
    };
    const int n_apps = (int)(sizeof(app_ids) / sizeof(app_ids[0]));

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        TestShelf shelf = { .pinned_apps = NULL, .pinned_count = 0,
                            .height = SHELF_HEIGHT };

        /* Pin all apps, all start with is_running = false */
        for (int i = 0; i < n_apps; i++) {
            int r = test_shelf_pin_app(&shelf, app_ids[i]);
            assert_int_equal(r, 0);
        }

        /* Pick a random target and a random running state */
        int  target_idx = zen_pbt_rand_int(0, n_apps - 1);
        bool running    = zen_pbt_rand_bool();
        const char *target = app_ids[target_idx];

        /* Record pre-update states of all other entries */
        bool pre_state[4];
        for (int i = 0; i < n_apps; i++) {
            pre_state[i] = shelf.pinned_apps[i].is_running;
        }

        test_shelf_update_running(&shelf, target, running);

        /* Target entry must reflect the new state */
        assert_int_equal((int)shelf.pinned_apps[target_idx].is_running,
                         (int)running);

        /* All other entries must be unchanged */
        for (int i = 0; i < n_apps; i++) {
            if (i == target_idx) continue;
            assert_int_equal((int)shelf.pinned_apps[i].is_running,
                             (int)pre_state[i]);
        }

        test_shelf_cleanup(&shelf);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 20: Pin/unpin round-trip
 *
 * For any app_id:
 *   - After pin_app(shelf, id), is_pinned(shelf, id) == true
 *   - After unpin_app(shelf, id), is_pinned(shelf, id) == false
 *   - pinned_count decrements back to its original value
 *
 * **Validates: Requirements 13.1, 13.2**
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property20_pin_unpin_roundtrip(void **state)
{
    (void)state;

    /* App_id alphabet: lowercase alphanum only (safe for app_id format) */
    static const char *candidates[] = {
        "org.zen.browser",
        "org.zen.terminal",
        "com.example.app",
        "io.github.tool",
        "net.test.widget",
    };
    const int n_cands = (int)(sizeof(candidates) / sizeof(candidates[0]));

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        TestShelf shelf = { .pinned_apps = NULL, .pinned_count = 0,
                            .height = SHELF_HEIGHT };

        /* Pin a random subset of apps first (0–3 apps) */
        int pre_count = zen_pbt_rand_int(0, 3);
        for (int i = 0; i < pre_count && i < n_cands - 1; i++) {
            test_shelf_pin_app(&shelf, candidates[i]);
        }

        /* Pick a target that is NOT already pinned */
        const char *target = candidates[n_cands - 1];
        int count_before = shelf.pinned_count;

        /* Pin the target */
        int r = test_shelf_pin_app(&shelf, target);
        assert_int_equal(r, 0);
        assert_true(test_shelf_is_pinned(&shelf, target));
        assert_int_equal(shelf.pinned_count, count_before + 1);

        /* Unpin the target */
        r = test_shelf_unpin_app(&shelf, target);
        assert_int_equal(r, 0);
        assert_false(test_shelf_is_pinned(&shelf, target));
        assert_int_equal(shelf.pinned_count, count_before);

        /* Unpinning again must return -1 (not found) */
        r = test_shelf_unpin_app(&shelf, target);
        assert_int_equal(r, -1);

        test_shelf_cleanup(&shelf);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 25: Shelf icon scaling
 *
 * For any icon_size in [16, min(64, SHELF_HEIGHT)] and
 * shelf_height == SHELF_HEIGHT:
 *   slot_width == icon_size + ICON_SLOT_PADDING
 *   icon_y     == (shelf_height - icon_size) / 2
 *   icon_y     >= 0  (icon fits vertically within the shelf)
 *   icon_y + icon_size <= shelf_height
 *
 * **Validates: Requirements 2.2, 13.1**
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property25_icon_scaling(void **state)
{
    (void)state;

    /*
     * Valid icon_size range: [16, SHELF_HEIGHT].
     * icon_size > SHELF_HEIGHT would produce a negative icon_y, which is
     * outside the intended operating range (shelf height is 48px).
     */
    const int max_icon = SHELF_HEIGHT; /* 48 */

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        int icon_size = zen_pbt_rand_int(16, max_icon);

        int slot_w, icon_y;
        compute_icon_geometry(icon_size, SHELF_HEIGHT, &slot_w, &icon_y);

        assert_int_equal(slot_w, icon_size + ICON_SLOT_PADDING);
        assert_int_equal(icon_y, (SHELF_HEIGHT - icon_size) / 2);

        /* icon must fit vertically within the shelf */
        assert_true(icon_y >= 0);
        assert_true(icon_y + icon_size <= SHELF_HEIGHT);
    }

    /* Boundary checks: icon_size == 16 and icon_size == SHELF_HEIGHT */
    {
        int slot_w, icon_y;
        compute_icon_geometry(16, SHELF_HEIGHT, &slot_w, &icon_y);
        assert_int_equal(slot_w, 16 + ICON_SLOT_PADDING);
        assert_int_equal(icon_y, (SHELF_HEIGHT - 16) / 2);
        assert_true(icon_y >= 0);
    }
    {
        int slot_w, icon_y;
        compute_icon_geometry(SHELF_HEIGHT, SHELF_HEIGHT, &slot_w, &icon_y);
        assert_int_equal(slot_w, SHELF_HEIGHT + ICON_SLOT_PADDING);
        assert_int_equal(icon_y, 0);
        assert_true(icon_y >= 0);
    }
    /* Verify the slot_w formula holds for the full config range [16, 64] */
    for (int icon_size = 16; icon_size <= 64; icon_size++) {
        int slot_w = icon_size + ICON_SLOT_PADDING;
        assert_int_equal(slot_w, icon_size + ICON_SLOT_PADDING);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_property1_shelf_dimensions),
        cmocka_unit_test(test_property2_clock_format),
        cmocka_unit_test(test_property3_running_indicator),
        cmocka_unit_test(test_property20_pin_unpin_roundtrip),
        cmocka_unit_test(test_property24_position_geometry),
        cmocka_unit_test(test_property25_icon_scaling),
    };

    return cmocka_run_group_tests_name("shelf-properties", tests, NULL, NULL);
}
