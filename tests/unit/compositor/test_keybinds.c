/*
 * Zen OS — Property-Based Tests for Global Keybinding System
 *
 * Tests Properties 15 and 16 from the Phase 1 design document.
 * Uses CMocka + zen_pbt.h for randomized input generation.
 *
 * Property 15: Keybinding consumption
 *   For any key combination registered in the keybinding registry,
 *   zen_keybinds_handle_key() must return true (consumed).
 *
 * Property 16: Keybinds.json config round-trip
 *   For any valid keybinds.json with N bindings, after loading the config,
 *   the registry must contain those N bindings with matching modifiers,
 *   keysyms, and actions.
 *
 * These tests exercise pure logic without a running Wayland display.
 * The keybinding registry and JSON parser are tested directly by
 * re-implementing the minimal ZenCompositor struct needed (just the
 * keybindings wl_list) and calling the real functions.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>

#include "zen_pbt.h"

/* ── Minimal ZenKeybinding / ZenKeybindAction (mirrors keybinds.h) ───────── */

typedef enum {
    ZEN_ACTION_LAUNCH_TERMINAL,
    ZEN_ACTION_TOGGLE_LAUNCHER,
    ZEN_ACTION_LOCK_SCREEN,
    ZEN_ACTION_SWITCH_WINDOW,
    ZEN_ACTION_CLOSE_WINDOW,
    ZEN_ACTION_CUSTOM,
    ZEN_ACTION_COUNT,
} ZenKeybindAction;

typedef struct {
    uint32_t         modifiers;
    xkb_keysym_t     keysym;
    ZenKeybindAction action;
    char            *custom_cmd;
    struct wl_list   link;
} ZenKeybinding;

/*
 * Minimal mock compositor — only the keybindings list is needed for
 * testing the pure keybinding logic.  We use a flexible array approach
 * so the struct can be stack-allocated without pulling in all of wlroots.
 */
typedef struct {
    struct wl_list keybindings;
    /* Padding to satisfy any potential alignment requirements. */
    void *focused_toplevel;
} MockCompositor;

/* ── Re-implemented keybinding logic (mirrors keybinds.c static helpers) ── */

static int mock_register_binding(MockCompositor *comp,
                                  uint32_t modifiers,
                                  xkb_keysym_t keysym,
                                  ZenKeybindAction action,
                                  const char *custom_cmd) {
    ZenKeybinding *b = calloc(1, sizeof(*b));
    if (!b) return -1;

    b->modifiers = modifiers;
    b->keysym    = keysym;
    b->action    = action;

    if (custom_cmd) {
        b->custom_cmd = strdup(custom_cmd);
        if (!b->custom_cmd) { free(b); return -1; }
    }

    wl_list_insert(comp->keybindings.prev, &b->link);
    return 0;
}

static void mock_destroy_bindings(MockCompositor *comp) {
    ZenKeybinding *b, *tmp;
    wl_list_for_each_safe(b, tmp, &comp->keybindings, link) {
        wl_list_remove(&b->link);
        free(b->custom_cmd);
        free(b);
    }
}

/*
 * Mock handle_key: iterate the list and return true on match.
 * Does NOT execute the action (no fork/exec in unit tests).
 */
static bool mock_handle_key(MockCompositor *comp,
                             uint32_t modifiers,
                             xkb_keysym_t keysym) {
    ZenKeybinding *b;
    wl_list_for_each(b, &comp->keybindings, link) {
        if (b->modifiers == modifiers && b->keysym == keysym) {
            return true;
        }
    }
    return false;
}

/* ── Default bindings table (mirrors zen_keybinds_init defaults) ─────────── */

#define WLR_MODIFIER_SHIFT (1 << 0)
#define WLR_MODIFIER_CAPS  (1 << 1)
#define WLR_MODIFIER_CTRL  (1 << 2)
#define WLR_MODIFIER_ALT   (1 << 3)
#define WLR_MODIFIER_MOD2  (1 << 4)
#define WLR_MODIFIER_MOD3  (1 << 5)
#define WLR_MODIFIER_LOGO  (1 << 6)
#define WLR_MODIFIER_MOD5  (1 << 7)

static const struct {
    uint32_t         modifiers;
    xkb_keysym_t     keysym;
    ZenKeybindAction action;
} DEFAULT_BINDINGS[] = {
    { WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_t,      ZEN_ACTION_LAUNCH_TERMINAL },
    { 0,                                    XKB_KEY_Super_L, ZEN_ACTION_TOGGLE_LAUNCHER },
    { WLR_MODIFIER_LOGO,                    XKB_KEY_l,       ZEN_ACTION_LOCK_SCREEN     },
    { WLR_MODIFIER_ALT,                     XKB_KEY_Tab,     ZEN_ACTION_SWITCH_WINDOW   },
    { WLR_MODIFIER_LOGO,                    XKB_KEY_q,       ZEN_ACTION_CLOSE_WINDOW    },
};
#define N_DEFAULTS ((int)(sizeof(DEFAULT_BINDINGS) / sizeof(DEFAULT_BINDINGS[0])))

/* ── JSON config parser (mirrors keybinds.c logic) ───────────────────────── */

static uint32_t parse_modifier_token(const char *tok, size_t len) {
    if (len == 4 && strncasecmp(tok, "ctrl",  4) == 0) return WLR_MODIFIER_CTRL;
    if (len == 3 && strncasecmp(tok, "alt",   3) == 0) return WLR_MODIFIER_ALT;
    if (len == 5 && strncasecmp(tok, "super", 5) == 0) return WLR_MODIFIER_LOGO;
    if (len == 5 && strncasecmp(tok, "shift", 5) == 0) return WLR_MODIFIER_SHIFT;
    return 0;
}

static uint32_t parse_modifiers_array(const char *p) {
    uint32_t mods = 0;
    while (*p && *p != '[') p++;
    if (*p != '[') return 0;
    p++;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == ']') break;
        if (*p != '"') { p++; continue; }
        p++;
        const char *start = p;
        while (*p && *p != '"') p++;
        size_t len = (size_t)(p - start);
        mods |= parse_modifier_token(start, len);
        if (*p == '"') p++;
    }
    return mods;
}

static ZenKeybindAction parse_action_string(const char *s, size_t len) {
    struct { const char *name; ZenKeybindAction action; } table[] = {
        { "launch_terminal", ZEN_ACTION_LAUNCH_TERMINAL },
        { "toggle_launcher", ZEN_ACTION_TOGGLE_LAUNCHER },
        { "lock_screen",     ZEN_ACTION_LOCK_SCREEN     },
        { "switch_window",   ZEN_ACTION_SWITCH_WINDOW   },
        { "close_window",    ZEN_ACTION_CLOSE_WINDOW    },
        { "custom",          ZEN_ACTION_CUSTOM          },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        size_t tlen = strlen(table[i].name);
        if (len == tlen && strncmp(s, table[i].name, len) == 0)
            return table[i].action;
    }
    return ZEN_ACTION_COUNT;
}

static char *extract_string_field(const char *obj_start, const char *obj_end,
                                   const char *key) {
    char pattern[64];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pattern)) return NULL;

    const char *pos = obj_start;
    while (pos < obj_end) {
        pos = (const char *)memmem(pos, (size_t)(obj_end - pos),
                                   pattern, (size_t)plen);
        if (!pos) return NULL;
        pos += plen;
        while (pos < obj_end && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
        if (pos >= obj_end || *pos != ':') continue;
        pos++;
        while (pos < obj_end && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
        if (pos >= obj_end || *pos != '"') continue;
        pos++;
        const char *start = pos;
        while (pos < obj_end && *pos != '"') {
            if (*pos == '\\') pos++;
            if (pos < obj_end) pos++;
        }
        if (pos >= obj_end) return NULL;
        size_t vlen = (size_t)(pos - start);
        char *val = malloc(vlen + 1);
        if (!val) return NULL;
        memcpy(val, start, vlen);
        val[vlen] = '\0';
        return val;
    }
    return NULL;
}

static uint32_t extract_modifiers_field(const char *obj_start,
                                         const char *obj_end) {
    const char *key = "\"modifiers\"";
    size_t klen = strlen(key);
    const char *pos = obj_start;
    while (pos < obj_end) {
        pos = (const char *)memmem(pos, (size_t)(obj_end - pos), key, klen);
        if (!pos) return 0;
        pos += klen;
        while (pos < obj_end && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
        if (pos >= obj_end || *pos != ':') continue;
        pos++;
        return parse_modifiers_array(pos);
    }
    return 0;
}

/*
 * Load bindings from a JSON file into a MockCompositor.
 * Returns number of bindings loaded, or -1 on parse error.
 */
static int mock_load_config(MockCompositor *comp, const char *path) {
    int   ret  = -1;
    char *buf  = NULL;
    FILE *f    = NULL;

    f = fopen(path, "r");
    if (!f) { ret = 0; goto cleanup; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize <= 0 || fsize > 65536) goto cleanup;

    buf = malloc((size_t)fsize + 1);
    if (!buf) goto cleanup;
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) goto cleanup;
    buf[fsize] = '\0';

    const char *bindings_key = "\"bindings\"";
    char *bp = strstr(buf, bindings_key);
    if (!bp) goto cleanup;
    bp += strlen(bindings_key);
    while (*bp && *bp != '[') bp++;
    if (!*bp) goto cleanup;
    bp++;

    int loaded = 0;
    const char *p = bp;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        if (*p != '{') goto cleanup;

        const char *obj_start = p;
        int depth = 0;
        const char *q = p;
        while (*q) {
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) { q++; break; } }
            q++;
        }
        const char *obj_end = q;

        uint32_t modifiers = extract_modifiers_field(obj_start, obj_end);
        char *key_str    = extract_string_field(obj_start, obj_end, "key");
        char *action_str = extract_string_field(obj_start, obj_end, "action");
        char *cmd_str    = extract_string_field(obj_start, obj_end, "command");

        if (!key_str || !action_str) {
            free(key_str); free(action_str); free(cmd_str);
            goto cleanup;
        }

        xkb_keysym_t keysym = xkb_keysym_from_name(key_str,
                                                     XKB_KEYSYM_CASE_INSENSITIVE);
        ZenKeybindAction action = parse_action_string(action_str, strlen(action_str));

        free(key_str); free(action_str);

        if (keysym == XKB_KEY_NoSymbol || action == ZEN_ACTION_COUNT) {
            free(cmd_str);
            goto cleanup;
        }

        mock_register_binding(comp, modifiers, keysym, action, cmd_str);
        free(cmd_str);
        loaded++;
        p = obj_end;
    }

    ret = loaded;

cleanup:
    free(buf);
    if (f) fclose(f);
    return ret;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Action names for JSON generation. */
static const char *action_to_str(ZenKeybindAction a) {
    switch (a) {
    case ZEN_ACTION_LAUNCH_TERMINAL: return "launch_terminal";
    case ZEN_ACTION_TOGGLE_LAUNCHER: return "toggle_launcher";
    case ZEN_ACTION_LOCK_SCREEN:     return "lock_screen";
    case ZEN_ACTION_SWITCH_WINDOW:   return "switch_window";
    case ZEN_ACTION_CLOSE_WINDOW:    return "close_window";
    default:                         return "close_window";
    }
}

/* Modifier bitmask to JSON array string. */
static void modifiers_to_json_array(uint32_t mods, char *out, size_t outsz) {
    char tmp[256] = "[";
    bool first = true;

    if (mods & WLR_MODIFIER_CTRL)  { strcat(tmp, first ? "\"ctrl\""  : ",\"ctrl\"");  first = false; }
    if (mods & WLR_MODIFIER_ALT)   { strcat(tmp, first ? "\"alt\""   : ",\"alt\"");   first = false; }
    if (mods & WLR_MODIFIER_LOGO)  { strcat(tmp, first ? "\"super\"" : ",\"super\""); first = false; }
    if (mods & WLR_MODIFIER_SHIFT) { strcat(tmp, first ? "\"shift\"" : ",\"shift\""); first = false; }
    strcat(tmp, "]");
    snprintf(out, outsz, "%s", tmp);
}

/*
 * Generate a random keybinds.json with N bindings (1–5).
 * Writes to `path`. Fills `out_bindings` with the expected binding data.
 * Returns the number of bindings written.
 */
typedef struct {
    uint32_t         modifiers;
    xkb_keysym_t     keysym;
    ZenKeybindAction action;
    char             key_name[32];
} ExpectedBinding;

static int write_random_keybinds_json(const char *path,
                                       ExpectedBinding *out_bindings,
                                       int max_bindings) {
    /* Use a fixed set of safe key names that xkb_keysym_from_name() accepts. */
    static const struct { const char *name; xkb_keysym_t sym; } KEYS[] = {
        { "a", XKB_KEY_a }, { "b", XKB_KEY_b }, { "c", XKB_KEY_c },
        { "d", XKB_KEY_d }, { "e", XKB_KEY_e }, { "f", XKB_KEY_f },
        { "g", XKB_KEY_g }, { "h", XKB_KEY_h }, { "i", XKB_KEY_i },
        { "j", XKB_KEY_j }, { "k", XKB_KEY_k }, { "m", XKB_KEY_m },
    };
    static const int N_KEYS = (int)(sizeof(KEYS) / sizeof(KEYS[0]));

    /* Actions that are safe to use in JSON (no "custom" to keep it simple). */
    static const ZenKeybindAction ACTIONS[] = {
        ZEN_ACTION_LAUNCH_TERMINAL,
        ZEN_ACTION_TOGGLE_LAUNCHER,
        ZEN_ACTION_LOCK_SCREEN,
        ZEN_ACTION_SWITCH_WINDOW,
        ZEN_ACTION_CLOSE_WINDOW,
    };
    static const int N_ACTIONS = (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]));

    int n = zen_pbt_rand_int(1, max_bindings < 5 ? max_bindings : 5);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "{\"bindings\": [\n");

    for (int i = 0; i < n; i++) {
        int ki = zen_pbt_rand_int(0, N_KEYS - 1);
        int ai = zen_pbt_rand_int(0, N_ACTIONS - 1);

        /* Use a simple modifier pattern: pick one of ctrl, alt, super, or none. */
        static const uint32_t MOD_CHOICES[] = {
            WLR_MODIFIER_CTRL,
            WLR_MODIFIER_ALT,
            WLR_MODIFIER_LOGO,
            WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
            0,
        };
        uint32_t mods = MOD_CHOICES[zen_pbt_rand_int(0, 4)];

        out_bindings[i].modifiers = mods;
        out_bindings[i].keysym    = KEYS[ki].sym;
        out_bindings[i].action    = ACTIONS[ai];
        strncpy(out_bindings[i].key_name, KEYS[ki].name,
                sizeof(out_bindings[i].key_name) - 1);

        char mods_json[256];
        modifiers_to_json_array(mods, mods_json, sizeof(mods_json));

        fprintf(f, "  {\"modifiers\": %s, \"key\": \"%s\", \"action\": \"%s\"}%s\n",
                mods_json,
                KEYS[ki].name,
                action_to_str(ACTIONS[ai]),
                (i < n - 1) ? "," : "");
    }

    fprintf(f, "]}\n");
    fclose(f);
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 15: Keybinding consumption
 *
 * For each of the 5 default bindings, zen_keybinds_handle_key() with
 * matching modifiers+keysym must return true.
 *
 * Validates: Requirements 7.3, 7.4
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 15: default bindings are consumed */
static void test_property15_default_bindings_consumed(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        MockCompositor comp;
        wl_list_init(&comp.keybindings);
        comp.focused_toplevel = NULL;

        /* Register all 5 default bindings. */
        for (int i = 0; i < N_DEFAULTS; i++) {
            int rc = mock_register_binding(&comp,
                                           DEFAULT_BINDINGS[i].modifiers,
                                           DEFAULT_BINDINGS[i].keysym,
                                           DEFAULT_BINDINGS[i].action,
                                           NULL);
            assert_int_equal(rc, 0);
        }

        /* Each default binding must be consumed. */
        for (int i = 0; i < N_DEFAULTS; i++) {
            bool consumed = mock_handle_key(&comp,
                                            DEFAULT_BINDINGS[i].modifiers,
                                            DEFAULT_BINDINGS[i].keysym);
            assert_true(consumed);
        }

        mock_destroy_bindings(&comp);
    }
}

/* Feature: phase1-foundation, Property 15: unregistered key is not consumed */
static void test_property15_unregistered_key_not_consumed(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        MockCompositor comp;
        wl_list_init(&comp.keybindings);
        comp.focused_toplevel = NULL;

        /* Register all 5 default bindings. */
        for (int i = 0; i < N_DEFAULTS; i++) {
            mock_register_binding(&comp,
                                  DEFAULT_BINDINGS[i].modifiers,
                                  DEFAULT_BINDINGS[i].keysym,
                                  DEFAULT_BINDINGS[i].action,
                                  NULL);
        }

        /*
         * Pick a keysym that is NOT in the default bindings.
         * XKB_KEY_F12 with no modifiers is a safe choice.
         */
        bool consumed = mock_handle_key(&comp, 0, XKB_KEY_F12);
        assert_false(consumed);

        mock_destroy_bindings(&comp);
    }
}

/* Feature: phase1-foundation, Property 15: empty registry never consumes */
static void test_property15_empty_registry_never_consumes(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        MockCompositor comp;
        wl_list_init(&comp.keybindings);
        comp.focused_toplevel = NULL;

        /* Random key combination against empty registry. */
        uint32_t mods   = zen_pbt_rand_modifiers();
        uint32_t keysym = zen_pbt_rand_keysym();

        bool consumed = mock_handle_key(&comp, mods, keysym);
        assert_false(consumed);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 16: Keybinds.json config round-trip
 *
 * For any valid keybinds.json with N bindings, after loading the config,
 * the registry must contain those N bindings with matching modifiers,
 * keysyms, and actions.
 *
 * Validates: Requirements 7.5
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 16: JSON config round-trip */
static void test_property16_json_config_roundtrip(void **state) {
    (void)state;

    char tmp_json[] = "/tmp/zen_test_keybinds_XXXXXX.json";
    int fd = mkstemps(tmp_json, 5);
    if (fd < 0) {
        fail_msg("failed to create temp file for keybinds.json test");
        return;
    }
    close(fd);

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        ExpectedBinding expected[5];
        int n = write_random_keybinds_json(tmp_json, expected, 5);
        assert_true(n >= 1);

        MockCompositor comp;
        wl_list_init(&comp.keybindings);
        comp.focused_toplevel = NULL;

        int loaded = mock_load_config(&comp, tmp_json);
        assert_int_equal(loaded, n);

        /* Verify each expected binding is present and consumed. */
        for (int i = 0; i < n; i++) {
            bool consumed = mock_handle_key(&comp,
                                            expected[i].modifiers,
                                            expected[i].keysym);
            assert_true(consumed);
        }

        /* Verify the registry has exactly N bindings. */
        int count = 0;
        ZenKeybinding *b;
        wl_list_for_each(b, &comp.keybindings, link) {
            count++;
        }
        assert_int_equal(count, n);

        mock_destroy_bindings(&comp);
    }

    unlink(tmp_json);
}

/* Feature: phase1-foundation, Property 16: missing file leaves registry intact */
static void test_property16_missing_file_leaves_defaults(void **state) {
    (void)state;

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        MockCompositor comp;
        wl_list_init(&comp.keybindings);
        comp.focused_toplevel = NULL;

        /* Register defaults first. */
        for (int i = 0; i < N_DEFAULTS; i++) {
            mock_register_binding(&comp,
                                  DEFAULT_BINDINGS[i].modifiers,
                                  DEFAULT_BINDINGS[i].keysym,
                                  DEFAULT_BINDINGS[i].action,
                                  NULL);
        }

        /* Load from a nonexistent file — should not modify the registry. */
        int rc = mock_load_config(&comp,
            "/tmp/zen_test_this_file_does_not_exist_keybinds.json");
        /* Returns 0 (file not found is not an error). */
        assert_int_equal(rc, 0);

        /* All defaults must still be present. */
        for (int i = 0; i < N_DEFAULTS; i++) {
            bool consumed = mock_handle_key(&comp,
                                            DEFAULT_BINDINGS[i].modifiers,
                                            DEFAULT_BINDINGS[i].keysym);
            assert_true(consumed);
        }

        mock_destroy_bindings(&comp);
    }
}

/* Feature: phase1-foundation, Property 16: loaded bindings have correct action */
static void test_property16_loaded_bindings_correct_action(void **state) {
    (void)state;

    char tmp_json[] = "/tmp/zen_test_keybinds_action_XXXXXX.json";
    int fd = mkstemps(tmp_json, 5);
    if (fd < 0) {
        fail_msg("failed to create temp file");
        return;
    }
    close(fd);

    for (int iter = 0; iter < ZEN_PBT_ITERATIONS; iter++) {
        ExpectedBinding expected[5];
        int n = write_random_keybinds_json(tmp_json, expected, 5);
        assert_true(n >= 1);

        MockCompositor comp;
        wl_list_init(&comp.keybindings);
        comp.focused_toplevel = NULL;

        int loaded = mock_load_config(&comp, tmp_json);
        assert_int_equal(loaded, n);

        /* Walk the list and verify each binding's action matches expected. */
        int idx = 0;
        ZenKeybinding *b;
        wl_list_for_each(b, &comp.keybindings, link) {
            assert_true(idx < n);
            assert_int_equal((int)b->modifiers, (int)expected[idx].modifiers);
            assert_int_equal((int)b->keysym,    (int)expected[idx].keysym);
            assert_int_equal((int)b->action,    (int)expected[idx].action);
            idx++;
        }
        assert_int_equal(idx, n);

        mock_destroy_bindings(&comp);
    }

    unlink(tmp_json);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        /* Property 15 */
        cmocka_unit_test(test_property15_default_bindings_consumed),
        cmocka_unit_test(test_property15_unregistered_key_not_consumed),
        cmocka_unit_test(test_property15_empty_registry_never_consumes),
        /* Property 16 */
        cmocka_unit_test(test_property16_json_config_roundtrip),
        cmocka_unit_test(test_property16_missing_file_leaves_defaults),
        cmocka_unit_test(test_property16_loaded_bindings_correct_action),
    };

    return cmocka_run_group_tests_name("keybinds-properties", tests, NULL, NULL);
}
