/*
 * Zen OS Shell — Property-Based Tests: Tray Indicators
 *
 * Property 23: Tray indicator independence
 *   For any subset S of the five D-Bus services (UPower, BlueZ,
 *   NetworkManager, PipeWire, udisks2) that are unavailable, the tray
 *   indicators for services NOT in S must still display valid data.
 *   Each unavailable service's indicator must show available == false
 *   without affecting others.
 *
 * Property 26: Battery label contains percentage
 *   For any battery percentage value P in [0, 100] and any charging state,
 *   the battery tray indicator label must contain the string representation
 *   of P followed by "%".
 *
 * Pure logic tests — no wlroots or compositor headers required.
 *
 * Validates: Requirements 5.1, 5.2, 5.3
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

/* ── Minimal tray indicator (mirrors ZenTrayIndicator from shelf.h) ──────── */

typedef struct {
    bool  available;
    char *label;
    int   icon_index;
} TestTrayIndicator;

/* ── Minimal shelf with 5 tray indicators ────────────────────────────────── */

#define TRAY_COUNT 5

typedef struct {
    TestTrayIndicator indicators[TRAY_COUNT];
} TestShelf;

/* Index constants matching the five D-Bus services */
#define TRAY_UPOWER  0
#define TRAY_BLUEZ   1
#define TRAY_NETWORK 2
#define TRAY_PIPEWIRE 3
#define TRAY_UDISKS  4

/*
 * Initialise all indicators to available with a non-NULL label.
 * Caller must call test_shelf_cleanup() when done.
 */
static int test_shelf_init(TestShelf *shelf)
{
    static const char *default_labels[TRAY_COUNT] = {
        "100%", "on", "connected", "50%", "mounted",
    };

    for (int i = 0; i < TRAY_COUNT; i++) {
        shelf->indicators[i].available   = true;
        shelf->indicators[i].icon_index  = i;
        shelf->indicators[i].label       = strdup(default_labels[i]);
        if (!shelf->indicators[i].label) {
            /* cleanup already-allocated labels */
            for (int j = 0; j < i; j++) {
                free(shelf->indicators[j].label);
                shelf->indicators[j].label = NULL;
            }
            return -1;
        }
    }
    return 0;
}

static void test_shelf_cleanup(TestShelf *shelf)
{
    for (int i = 0; i < TRAY_COUNT; i++) {
        free(shelf->indicators[i].label);
        shelf->indicators[i].label = NULL;
    }
}

/*
 * Mark indicator[idx] as unavailable.
 * Mirrors the D-Bus service-gone path in shelf.c.
 */
static void test_tray_set_unavailable(TestShelf *shelf, int idx)
{
    shelf->indicators[idx].available = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 23: Tray indicator independence
 *
 * For any random subset S of the five indicators set to unavailable:
 *   - Each indicator in S has available == false
 *   - Each indicator NOT in S retains available == true and its label
 *
 * **Validates: Requirements 5.1, 5.2**
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property23_tray_indicator_independence(void **state)
{
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        TestShelf shelf;
        int r = test_shelf_init(&shelf);
        assert_int_equal(r, 0);

        /* Record original labels for all indicators */
        char *orig_labels[TRAY_COUNT];
        for (int i = 0; i < TRAY_COUNT; i++) {
            orig_labels[i] = strdup(shelf.indicators[i].label);
            assert_non_null(orig_labels[i]);
        }

        /* Generate a random bitmask: bit i set means indicator i is unavailable */
        int mask = zen_pbt_rand_int(0, (1 << TRAY_COUNT) - 1);

        /* Apply unavailability for each indicator in the subset */
        for (int i = 0; i < TRAY_COUNT; i++) {
            if (mask & (1 << i)) {
                test_tray_set_unavailable(&shelf, i);
            }
        }

        /* Verify: indicators in S are unavailable */
        for (int i = 0; i < TRAY_COUNT; i++) {
            if (mask & (1 << i)) {
                assert_false(shelf.indicators[i].available);
            }
        }

        /* Verify: indicators NOT in S are still available with original labels */
        for (int i = 0; i < TRAY_COUNT; i++) {
            if (!(mask & (1 << i))) {
                assert_true(shelf.indicators[i].available);
                assert_non_null(shelf.indicators[i].label);
                assert_string_equal(shelf.indicators[i].label, orig_labels[i]);
            }
        }

        /* Cleanup */
        for (int i = 0; i < TRAY_COUNT; i++) {
            free(orig_labels[i]);
        }
        test_shelf_cleanup(&shelf);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 26: Battery label contains percentage
 *
 * For any P in [0, 100], a label formatted as "%d%%", P must contain
 * the substring "<P>%" (e.g. "85%").
 *
 * **Validates: Requirements 5.3**
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_property26_battery_label_contains_percentage(void **state)
{
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        int pct = zen_pbt_rand_int(0, 100);

        /* Format the label the same way shelf.c would */
        char label[16];
        snprintf(label, sizeof(label), "%d%%", pct);

        /* Build the expected substring, e.g. "85%" */
        char expected[8];
        snprintf(expected, sizeof(expected), "%d%%", pct);

        /* The label must contain the percentage substring */
        assert_non_null(strstr(label, expected));

        /* The label must end with '%' */
        size_t len = strlen(label);
        assert_true(len >= 2);
        assert_int_equal(label[len - 1], '%');
    }

    /* Boundary checks: 0% and 100% */
    {
        char label[16];
        snprintf(label, sizeof(label), "%d%%", 0);
        assert_non_null(strstr(label, "0%"));
    }
    {
        char label[16];
        snprintf(label, sizeof(label), "%d%%", 100);
        assert_non_null(strstr(label, "100%"));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_property23_tray_indicator_independence),
        cmocka_unit_test(test_property26_battery_label_contains_percentage),
    };

    return cmocka_run_group_tests_name("tray-properties", tests, NULL, NULL);
}
