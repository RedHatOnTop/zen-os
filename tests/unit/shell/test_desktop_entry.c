/*
 * Zen OS Shell — Desktop Entry Property Tests
 *
 * Property 10: Desktop entry parse/format round-trip
 *   format() then parse_string() produces an equivalent entry.
 *
 * Property 11: Desktop entry visibility filter
 *   is_visible() == true iff Type=Application AND NoDisplay=false AND Hidden=false.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "zen/desktop-entry.h"
#include "zen_pbt.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * Build a minimal valid .desktop file string from the given fields.
 * Caller must free().
 */
static char *make_desktop_string(const char *name, const char *exec,
                                  const char *type, bool no_display,
                                  bool hidden)
{
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "[Desktop Entry]\n"
        "Name=%s\n"
        "Exec=%s\n"
        "Type=%s\n"
        "NoDisplay=%s\n"
        "Hidden=%s\n",
        name  ? name  : "TestApp",
        exec  ? exec  : "/usr/bin/test",
        type  ? type  : "Application",
        no_display ? "true" : "false",
        hidden     ? "true" : "false");
    if (n <= 0 || (size_t)n >= sizeof(buf))
        return NULL;
    return strdup(buf);
}

/* ── Property 10: parse/format round-trip ────────────────────────────────── */

static void test_desktop_entry_roundtrip(void **state)
{
    (void)state;
    zen_pbt_seed();

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        /* Generate a random valid entry */
        char *name = zen_pbt_rand_string(1, 32);
        char *exec = zen_pbt_rand_string(1, 64);
        assert_non_null(name);
        assert_non_null(exec);

        /* Sanitize: replace characters that break .desktop parsing */
        for (char *p = name; *p; p++) {
            if (*p == '\n' || *p == '=' || *p == '[' || *p == ']')
                *p = '_';
        }
        for (char *p = exec; *p; p++) {
            if (*p == '\n' || *p == '=' || *p == '[' || *p == ']')
                *p = '_';
        }

        struct ZenDesktopEntry original;
        memset(&original, 0, sizeof(original));
        original.name = name;
        original.exec = exec;
        original.type = strdup("Application");
        original.no_display = false;
        original.hidden     = false;

        /* Format to string */
        char *formatted = zen_desktop_entry_format(&original);
        assert_non_null(formatted);

        /* Parse back */
        struct ZenDesktopEntry parsed;
        memset(&parsed, 0, sizeof(parsed));
        int rc = zen_desktop_entry_parse_string(&parsed, formatted);
        assert_int_equal(rc, 0);

        /* Verify name and exec round-trip */
        assert_non_null(parsed.name);
        assert_non_null(parsed.exec);
        assert_string_equal(parsed.name, original.name);
        assert_string_equal(parsed.exec, original.exec);

        /* Verify type round-trip */
        assert_non_null(parsed.type);
        assert_string_equal(parsed.type, "Application");

        /* Cleanup */
        free(formatted);
        /* original owns name/exec/type — free them manually */
        free(original.name);
        free(original.exec);
        free(original.type);
        zen_desktop_entry_cleanup(&parsed);
    }
}

/* ── Property 11: visibility filter ─────────────────────────────────────── */

static void test_desktop_entry_visibility(void **state)
{
    (void)state;
    zen_pbt_seed();

    /* Exhaustive check over all combinations of the three boolean fields */
    const char *types[]      = { "Application", "Link", "Directory", NULL };
    bool        no_displays[] = { false, true };
    bool        hiddens[]     = { false, true };

    for (int ti = 0; types[ti] != NULL; ti++) {
        for (int ni = 0; ni < 2; ni++) {
            for (int hi = 0; hi < 2; hi++) {
                char *content = make_desktop_string(
                    "TestApp", "/usr/bin/test",
                    types[ti], no_displays[ni], hiddens[hi]);
                assert_non_null(content);

                struct ZenDesktopEntry entry;
                memset(&entry, 0, sizeof(entry));
                int rc = zen_desktop_entry_parse_string(&entry, content);
                free(content);
                assert_int_equal(rc, 0);

                bool expected_visible =
                    (entry.type != NULL &&
                     strcmp(entry.type, "Application") == 0 &&
                     !entry.no_display &&
                     !entry.hidden);

                bool actual_visible = zen_desktop_entry_is_visible(&entry);

                assert_int_equal((int)actual_visible, (int)expected_visible);

                zen_desktop_entry_cleanup(&entry);
            }
        }
    }
}

/* ── Property 11b: random visibility ────────────────────────────────────── */

static void test_desktop_entry_visibility_random(void **state)
{
    (void)state;
    zen_pbt_seed();

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        bool no_display = zen_pbt_rand_bool();
        bool hidden     = zen_pbt_rand_bool();

        char *content = make_desktop_string(
            "App", "/usr/bin/app", "Application", no_display, hidden);
        assert_non_null(content);

        struct ZenDesktopEntry entry;
        memset(&entry, 0, sizeof(entry));
        int rc = zen_desktop_entry_parse_string(&entry, content);
        free(content);
        assert_int_equal(rc, 0);

        bool expected = (!no_display && !hidden);
        bool actual   = zen_desktop_entry_is_visible(&entry);

        assert_int_equal((int)actual, (int)expected);

        zen_desktop_entry_cleanup(&entry);
    }
}

/* ── Test: parse rejects non-Application types ───────────────────────────── */

static void test_desktop_entry_non_application_not_visible(void **state)
{
    (void)state;

    const char *non_app_types[] = { "Link", "Directory", "Service", NULL };

    for (int i = 0; non_app_types[i] != NULL; i++) {
        char *content = make_desktop_string(
            "Test", "/usr/bin/test", non_app_types[i], false, false);
        assert_non_null(content);

        struct ZenDesktopEntry entry;
        memset(&entry, 0, sizeof(entry));
        int rc = zen_desktop_entry_parse_string(&entry, content);
        free(content);
        assert_int_equal(rc, 0);

        assert_false(zen_desktop_entry_is_visible(&entry));

        zen_desktop_entry_cleanup(&entry);
    }
}

/* ── Test: cleanup is idempotent ─────────────────────────────────────────── */

static void test_desktop_entry_cleanup_idempotent(void **state)
{
    (void)state;

    struct ZenDesktopEntry entry;
    memset(&entry, 0, sizeof(entry));

    /* Cleanup on zeroed struct must not crash */
    zen_desktop_entry_cleanup(&entry);
    zen_desktop_entry_cleanup(&entry);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_desktop_entry_roundtrip),
        cmocka_unit_test(test_desktop_entry_visibility),
        cmocka_unit_test(test_desktop_entry_visibility_random),
        cmocka_unit_test(test_desktop_entry_non_application_not_visible),
        cmocka_unit_test(test_desktop_entry_cleanup_idempotent),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
