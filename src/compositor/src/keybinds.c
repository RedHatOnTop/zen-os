/*
 * Zen OS Compositor — Global Keybinding System
 *
 * Maintains a registry of key combinations mapped to compositor actions.
 * Default bindings are registered at init; custom overrides are loaded
 * from ~/.config/zenos/keybinds.json using pure C17 string parsing.
 *
 * Error handling: goto cleanup pattern per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "zen/compositor.h"
#include "zen/keybinds.h"
#include "zen/xdg.h"

/* Forward declaration to avoid circular dependency with lock.h (Tier C).
 * A weak stub is provided here so the compositor links without lock.c.
 * The real implementation in lock.c (Sub-Phase 1.12) will override it. */
__attribute__((weak))
void zen_lock_activate(struct ZenCompositor *compositor) {
    (void)compositor;
    wlr_log(WLR_INFO, "%s", "zen_lock_activate: lock module not yet initialized");
}

/* Forward declaration for wlr_xdg_toplevel_send_close (from wlr/types/wlr_xdg_shell.h).
 * We forward-declare to avoid pulling in the full xdg_shell header here. */
struct wlr_xdg_toplevel;
void wlr_xdg_toplevel_send_close(struct wlr_xdg_toplevel *toplevel);

/* ── Action names for logging ────────────────────────────────────────────── */

static const char *action_name(ZenKeybindAction action) {
    switch (action) {
    case ZEN_ACTION_LAUNCH_TERMINAL:  return "launch_terminal";
    case ZEN_ACTION_TOGGLE_LAUNCHER:  return "toggle_launcher";
    case ZEN_ACTION_LOCK_SCREEN:      return "lock_screen";
    case ZEN_ACTION_SWITCH_WINDOW:    return "switch_window";
    case ZEN_ACTION_CLOSE_WINDOW:     return "close_window";
    case ZEN_ACTION_CUSTOM:           return "custom";
    default:                          return "unknown";
    }
}

/* ── Action execution ────────────────────────────────────────────────────── */

static void execute_action(struct ZenCompositor *compositor,
                           ZenKeybindAction action,
                           const char *custom_cmd) {
    switch (action) {
    case ZEN_ACTION_LAUNCH_TERMINAL: {
        wlr_log(WLR_INFO, "%s", "Keybind: launching terminal");
        pid_t pid = fork();
        if (pid == 0) {
            /* Child: exec terminal. */
            char *const argv[] = { "foot", NULL };
            execvp(argv[0], argv);
            /* If foot not found, try xterm as fallback. */
            char *const fallback[] = { "xterm", NULL };
            execvp(fallback[0], fallback);
            _exit(127);
        } else if (pid < 0) {
            wlr_log(WLR_ERROR, "%s", "Keybind: fork() failed for terminal launch");
        }
        break;
    }

    case ZEN_ACTION_LOCK_SCREEN:
        wlr_log(WLR_INFO, "%s", "Keybind: activating lock screen");
        zen_lock_activate(compositor);
        break;

    case ZEN_ACTION_CLOSE_WINDOW: {
        wlr_log(WLR_INFO, "%s", "Keybind: closing focused window");
        struct ZenToplevel *focused = compositor->focused_toplevel;
        if (focused && focused->xdg_toplevel) {
            wlr_xdg_toplevel_send_close(focused->xdg_toplevel);
        }
        break;
    }

    case ZEN_ACTION_CUSTOM:
        if (custom_cmd && *custom_cmd) {
            wlr_log(WLR_INFO, "Keybind: executing custom command: %s", custom_cmd);
            pid_t pid = fork();
            if (pid == 0) {
                char *const argv[] = { "sh", "-c", (char *)custom_cmd, NULL };
                execvp("sh", argv);
                _exit(127);
            } else if (pid < 0) {
                wlr_log(WLR_ERROR, "%s", "Keybind: fork() failed for custom command");
            }
        }
        break;

    default:
        wlr_log(WLR_INFO, "Keybind: action '%s'", action_name(action));
        break;
    }
}

/* ── zen_keybinds_handle_key ─────────────────────────────────────────────── */

bool zen_keybinds_handle_key(struct ZenCompositor *compositor,
                              uint32_t modifiers,
                              xkb_keysym_t keysym) {
    if (!compositor) {
        return false;
    }

    ZenKeybinding *binding;
    wl_list_for_each(binding, &compositor->keybindings, link) {
        if (binding->modifiers == modifiers && binding->keysym == keysym) {
            execute_action(compositor, binding->action, binding->custom_cmd);
            return true;
        }
    }

    return false;
}

/* ── zen_keybinds_destroy ────────────────────────────────────────────────── */

void zen_keybinds_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Guard against uninitialized list (partial init). */
    if (compositor->keybindings.next == NULL) {
        return;
    }

    ZenKeybinding *binding, *tmp;
    wl_list_for_each_safe(binding, tmp, &compositor->keybindings, link) {
        wl_list_remove(&binding->link);
        free(binding->custom_cmd);
        free(binding);
    }

    wlr_log(WLR_INFO, "%s", "Keybinding registry destroyed");
}

/* ── Helper: register a single default binding ───────────────────────────── */

static int register_binding(struct ZenCompositor *compositor,
                             uint32_t modifiers,
                             xkb_keysym_t keysym,
                             ZenKeybindAction action,
                             const char *custom_cmd) {
    ZenKeybinding *b = calloc(1, sizeof(*b));
    if (!b) {
        return -1;
    }

    b->modifiers = modifiers;
    b->keysym    = keysym;
    b->action    = action;

    if (custom_cmd) {
        b->custom_cmd = strdup(custom_cmd);
        if (!b->custom_cmd) {
            free(b);
            return -1;
        }
    }

    wl_list_insert(compositor->keybindings.prev, &b->link);
    return 0;
}

/* ── JSON config parser ──────────────────────────────────────────────────── */

/*
 * Parse a modifier string token ("ctrl", "alt", "super", "shift") into
 * the corresponding WLR_MODIFIER_* bit.  Returns 0 for unknown tokens.
 */
static uint32_t parse_modifier_token(const char *tok, size_t len) {
    if (len == 4 && strncasecmp(tok, "ctrl", 4) == 0) {
        return WLR_MODIFIER_CTRL;
    }
    if (len == 3 && strncasecmp(tok, "alt", 3) == 0) {
        return WLR_MODIFIER_ALT;
    }
    if (len == 5 && strncasecmp(tok, "super", 5) == 0) {
        return WLR_MODIFIER_LOGO;
    }
    if (len == 5 && strncasecmp(tok, "shift", 5) == 0) {
        return WLR_MODIFIER_SHIFT;
    }
    return 0;
}

/*
 * Parse the "modifiers" JSON array value (the part after the colon).
 * Expects: ["ctrl","alt"] or ["super"] etc.
 * Returns the combined WLR_MODIFIER_* bitmask.
 */
static uint32_t parse_modifiers_array(const char *p) {
    uint32_t mods = 0;

    /* Skip to '[' */
    while (*p && *p != '[') p++;
    if (*p != '[') return 0;
    p++;

    while (*p && *p != ']') {
        /* Skip whitespace and commas. */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == ']') break;
        if (*p != '"') { p++; continue; }
        p++; /* skip opening quote */

        const char *start = p;
        while (*p && *p != '"') p++;
        size_t len = (size_t)(p - start);
        mods |= parse_modifier_token(start, len);

        if (*p == '"') p++; /* skip closing quote */
    }

    return mods;
}

/*
 * Parse an action string into ZenKeybindAction.
 * Returns ZEN_ACTION_COUNT on unknown action.
 */
static ZenKeybindAction parse_action_string(const char *s, size_t len) {
    /* Use explicit length comparisons for clarity. */
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
        if (len == tlen && strncmp(s, table[i].name, len) == 0) {
            return table[i].action;
        }
    }

    return ZEN_ACTION_COUNT; /* unknown */
}

/*
 * Extract a JSON string value for a given key within a binding object.
 * `obj_start` points to the '{' of the binding object.
 * `obj_end` points past the '}' of the binding object.
 * Returns a heap-allocated string on success, NULL if key not found.
 * Caller must free().
 */
static char *extract_string_field(const char *obj_start, const char *obj_end,
                                   const char *key) {
    /* Build search pattern: "key" */
    char pattern[64];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pattern)) return NULL;

    const char *pos = obj_start;
    while (pos < obj_end) {
        pos = (const char *)memmem(pos, (size_t)(obj_end - pos),
                                   pattern, (size_t)plen);
        if (!pos) return NULL;
        pos += plen;

        /* Skip whitespace and colon. */
        while (pos < obj_end && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
        if (pos >= obj_end || *pos != ':') continue;
        pos++;
        while (pos < obj_end && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
        if (pos >= obj_end || *pos != '"') continue;
        pos++; /* skip opening quote */

        const char *start = pos;
        while (pos < obj_end && *pos != '"') {
            if (*pos == '\\') pos++; /* skip escaped char */
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

/*
 * Extract the "modifiers" array field from a binding object.
 * Returns the combined WLR_MODIFIER_* bitmask.
 */
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

/* ── zen_keybinds_load_config ────────────────────────────────────────────── */

int zen_keybinds_load_config(struct ZenCompositor *compositor,
                              const char *path) {
    int   ret  = -1;
    char *buf  = NULL;
    FILE *f    = NULL;

    if (!compositor || !path) {
        goto cleanup;
    }

    f = fopen(path, "r");
    if (!f) {
        /* File missing is not an error — defaults remain. */
        wlr_log(WLR_DEBUG, "keybinds: config not found at %s (using defaults)", path);
        ret = 0;
        goto cleanup;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        wlr_log(WLR_INFO, "keybinds: fseek failed for %s", path);
        goto cleanup;
    }
    long fsize = ftell(f);
    rewind(f);

    if (fsize <= 0 || fsize > 65536) {
        wlr_log(WLR_INFO, "keybinds: config file too large or empty: %s", path);
        goto cleanup;
    }

    buf = malloc((size_t)fsize + 1);
    if (!buf) {
        wlr_log(WLR_ERROR, "keybinds: out of memory reading %s", path);
        goto cleanup;
    }

    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        wlr_log(WLR_INFO, "keybinds: read error for %s", path);
        goto cleanup;
    }
    buf[fsize] = '\0';

    /* Find "bindings" array. */
    const char *bindings_key = "\"bindings\"";
    char *bindings_pos = strstr(buf, bindings_key);
    if (!bindings_pos) {
        wlr_log(WLR_INFO, "keybinds: no 'bindings' key in %s", path);
        goto cleanup;
    }
    bindings_pos += strlen(bindings_key);

    /* Skip to '['. */
    while (*bindings_pos && *bindings_pos != '[') bindings_pos++;
    if (!*bindings_pos) {
        wlr_log(WLR_INFO, "keybinds: malformed bindings array in %s", path);
        goto cleanup;
    }
    bindings_pos++; /* skip '[' */

    /* Iterate binding objects. */
    int loaded = 0;
    const char *p = bindings_pos;
    while (*p) {
        /* Skip whitespace and commas. */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        if (*p != '{') {
            wlr_log(WLR_INFO, "%s", "keybinds: expected '{' in bindings array");
            goto cleanup;
        }

        /* Find matching '}'. */
        const char *obj_start = p;
        int depth = 0;
        const char *q = p;
        while (*q) {
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) { q++; break; } }
            q++;
        }
        const char *obj_end = q;

        /* Extract fields. */
        uint32_t modifiers = extract_modifiers_field(obj_start, obj_end);

        char *key_str    = extract_string_field(obj_start, obj_end, "key");
        char *action_str = extract_string_field(obj_start, obj_end, "action");
        char *cmd_str    = extract_string_field(obj_start, obj_end, "command");

        if (!key_str || !action_str) {
            wlr_log(WLR_INFO, "%s", "keybinds: binding missing 'key' or 'action' field");
            free(key_str);
            free(action_str);
            free(cmd_str);
            goto cleanup;
        }

        xkb_keysym_t keysym = xkb_keysym_from_name(key_str,
                                                     XKB_KEYSYM_CASE_INSENSITIVE);
        if (keysym == XKB_KEY_NoSymbol) {
            wlr_log(WLR_INFO, "keybinds: unknown key '%s' in config", key_str);
            free(key_str);
            free(action_str);
            free(cmd_str);
            goto cleanup;
        }

        ZenKeybindAction action = parse_action_string(action_str,
                                                       strlen(action_str));
        if (action == ZEN_ACTION_COUNT) {
            wlr_log(WLR_INFO, "keybinds: unknown action '%s' in config", action_str);
            free(key_str);
            free(action_str);
            free(cmd_str);
            goto cleanup;
        }

        int rc = register_binding(compositor, modifiers, keysym, action, cmd_str);
        free(key_str);
        free(action_str);
        free(cmd_str);

        if (rc != 0) {
            wlr_log(WLR_ERROR, "%s", "keybinds: out of memory registering binding");
            goto cleanup;
        }

        loaded++;
        p = obj_end;
    }

    wlr_log(WLR_INFO, "keybinds: loaded %d custom binding(s) from %s",
            loaded, path);
    ret = 0;

cleanup:
    free(buf);
    if (f) fclose(f);
    return ret;
}

/* ── zen_keybinds_init ───────────────────────────────────────────────────── */

int zen_keybinds_init(struct ZenCompositor *compositor) {
    if (!compositor) {
        return -1;
    }

    wl_list_init(&compositor->keybindings);

    /* Register 5 default bindings. */
    struct {
        uint32_t         modifiers;
        xkb_keysym_t     keysym;
        ZenKeybindAction action;
    } defaults[] = {
        /* Ctrl+Alt+T → launch terminal */
        { WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_t,       ZEN_ACTION_LAUNCH_TERMINAL },
        /* Super (lone) → toggle launcher */
        { 0,                                    XKB_KEY_Super_L,  ZEN_ACTION_TOGGLE_LAUNCHER },
        /* Super+L → lock screen */
        { WLR_MODIFIER_LOGO,                    XKB_KEY_l,        ZEN_ACTION_LOCK_SCREEN     },
        /* Alt+Tab → switch window */
        { WLR_MODIFIER_ALT,                     XKB_KEY_Tab,      ZEN_ACTION_SWITCH_WINDOW   },
        /* Super+Q → close window */
        { WLR_MODIFIER_LOGO,                    XKB_KEY_q,        ZEN_ACTION_CLOSE_WINDOW    },
    };

    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        if (register_binding(compositor,
                             defaults[i].modifiers,
                             defaults[i].keysym,
                             defaults[i].action,
                             NULL) != 0) {
            wlr_log(WLR_ERROR, "keybinds: failed to register default binding %zu", i);
            zen_keybinds_destroy(compositor);
            return -1;
        }
    }

    /* Load custom overrides from ~/.config/zenos/keybinds.json. */
    const char *home = getenv("HOME");
    if (home) {
        char config_path[512];
        snprintf(config_path, sizeof(config_path),
                 "%s/.config/zenos/keybinds.json", home);
        zen_keybinds_load_config(compositor, config_path);
    }

    wlr_log(WLR_INFO, "Keybinding registry initialized with %d default binding(s)",
            (int)(sizeof(defaults) / sizeof(defaults[0])));
    return 0;
}
