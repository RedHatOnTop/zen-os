/*
 * Zen OS Shell — Theme Configuration
 *
 * JSON serialization/deserialization for ~/.config/zenos/theme.json.
 * Hand-rolled minimal JSON parser — no external library.
 * Unknown fields are preserved via a static buffer for round-trip fidelity.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "zen/theme-config.h"

/* ── Default palettes ────────────────────────────────────────────────────── */

static const struct ZenColorPalette DEFAULT_DARK = {
    .background    = { 0.102f, 0.102f, 0.180f, 0.95f },
    .foreground    = { 0.878f, 0.878f, 0.878f, 1.0f  },
    .accent        = { 0.290f, 0.620f, 1.0f,   1.0f  },
    .border        = { 0.200f, 0.200f, 0.333f, 0.8f  },
    .shadow        = { 0.0f,   0.0f,   0.0f,   0.5f  },
    .surface       = { 0.145f, 0.145f, 0.271f, 0.95f },
    .text_secondary= { 0.533f, 0.533f, 0.533f, 1.0f  },
};

static const struct ZenColorPalette DEFAULT_LIGHT = {
    .background    = { 0.941f, 0.941f, 0.961f, 0.95f },
    .foreground    = { 0.102f, 0.102f, 0.102f, 1.0f  },
    .accent        = { 0.102f, 0.451f, 0.910f, 1.0f  },
    .border        = { 0.816f, 0.816f, 0.847f, 0.8f  },
    .shadow        = { 0.0f,   0.0f,   0.0f,   0.2f  },
    .surface       = { 1.0f,   1.0f,   1.0f,   0.95f },
    .text_secondary= { 0.400f, 0.400f, 0.400f, 1.0f  },
};

/* ── Unknown-field preservation (single-instance config) ─────────────────── */

/* Stores any key-value pairs from the JSON that are not "dark_mode" or
 * "wallpaper_path".  Format: comma-prefixed fragment ready to splice into
 * the output object, e.g. ', "custom_key": "value", "flag": true'.
 * Empty string when there are no unknown fields. */
static char *g_extra_json = NULL;

/* ── Internal JSON helpers ───────────────────────────────────────────────── */

/* Skip whitespace. Returns pointer past whitespace. */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* Match a literal string at *p.  Returns pointer past the match, or NULL. */
static const char *match_lit(const char *p, const char *lit) {
    size_t n = strlen(lit);
    if (strncmp(p, lit, n) == 0) return p + n;
    return NULL;
}

/* Parse a JSON string starting at the opening '"'.
 * Returns a heap-allocated copy of the string content (caller frees),
 * or NULL on error.  *end_out is set to the character after the closing '"'. */
static char *parse_json_string(const char *p, const char **end_out) {
    if (*p != '"') return NULL;
    p++; /* skip opening quote */

    const char *start = p;
    /* Find closing quote (no escape handling needed for our simple paths) */
    while (*p && *p != '"') {
        if (*p == '\\') p++; /* skip escaped char */
        if (*p) p++;
    }
    if (*p != '"') return NULL;

    size_t len = (size_t)(p - start);
    char *out = calloc(1, len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';

    if (end_out) *end_out = p + 1; /* past closing quote */
    return out;
}

/* Skip a single JSON value (string, number, bool, null, object, array).
 * Returns pointer past the value, or NULL on error. */
static const char *skip_json_value(const char *p) {
    p = skip_ws(p);
    if (!*p) return NULL;

    if (*p == '"') {
        /* string */
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') p++;
            if (*p) p++;
        }
        if (*p == '"') return p + 1;
        return NULL;
    }
    if (*p == '{') {
        int depth = 1; p++;
        while (*p && depth > 0) {
            if (*p == '"') { /* skip string to avoid counting braces inside */
                p++;
                while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
                if (*p == '"') p++;
            } else if (*p == '{') { depth++; p++; }
            else if (*p == '}') { depth--; p++; }
            else p++;
        }
        return (depth == 0) ? p : NULL;
    }
    if (*p == '[') {
        int depth = 1; p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
                if (*p == '"') p++;
            } else if (*p == '[') { depth++; p++; }
            else if (*p == ']') { depth--; p++; }
            else p++;
        }
        return (depth == 0) ? p : NULL;
    }
    /* number, bool, null — scan until delimiter */
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        p++;
    }
    return p;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void zen_theme_config_init(struct ZenThemeConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->dark_mode    = true;
    cfg->dark_palette = DEFAULT_DARK;
    cfg->light_palette= DEFAULT_LIGHT;
    /* wallpaper_path left NULL — no default wallpaper */
}

void zen_theme_config_cleanup(struct ZenThemeConfig *cfg) {
    free(cfg->wallpaper_path);
    memset(cfg, 0, sizeof(*cfg));
}

/* ── from_json ───────────────────────────────────────────────────────────── */

int zen_theme_config_from_json(struct ZenThemeConfig *cfg, const char *json) {
    int   ret        = -1;
    char *extra      = NULL;  /* accumulated unknown fields */
    char *extra_tmp  = NULL;
    char *key        = NULL;
    char *val_str    = NULL;
    char *new_wp     = NULL;

    if (!cfg || !json) goto cleanup;

    /* Reset to defaults first */
    zen_theme_config_init(cfg);

    /* extra buffer: worst case same size as input + some overhead */
    size_t extra_cap = strlen(json) + 4;
    extra = calloc(1, extra_cap);
    if (!extra) goto cleanup;
    extra[0] = '\0';

    const char *p = skip_ws(json);
    if (*p != '{') goto cleanup;
    p++;

    while (1) {
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; p = skip_ws(p); }
        if (*p == '}') { p++; break; }
        if (!*p) goto cleanup;

        /* Parse key */
        const char *key_end = NULL;
        key = parse_json_string(p, &key_end);
        if (!key) goto cleanup;
        p = skip_ws(key_end);

        if (*p != ':') goto cleanup;
        p++;
        p = skip_ws(p);

        /* Dispatch on known keys */
        if (strcmp(key, "dark_mode") == 0) {
            const char *after = match_lit(p, "true");
            if (after) {
                cfg->dark_mode = true;
                p = after;
            } else {
                after = match_lit(p, "false");
                if (!after) goto cleanup;
                cfg->dark_mode = false;
                p = after;
            }
        } else if (strcmp(key, "wallpaper_path") == 0) {
            const char *val_end = NULL;
            val_str = parse_json_string(p, &val_end);
            if (!val_str) goto cleanup;
            new_wp = val_str;
            val_str = NULL;
            p = val_end;
        } else {
            /* Unknown field — record raw key+value for preservation */
            const char *val_start = p;
            const char *val_end   = skip_json_value(p);
            if (!val_end) goto cleanup;

            /* Build fragment: , "key": value */
            size_t frag_len = 6 + strlen(key) + (size_t)(val_end - val_start);
            size_t cur_len  = strlen(extra);
            if (cur_len + frag_len + 1 > extra_cap) {
                extra_cap = cur_len + frag_len + 64;
                extra_tmp = realloc(extra, extra_cap);
                if (!extra_tmp) goto cleanup;
                extra = extra_tmp;
                extra_tmp = NULL;
            }
            /* Append: , "key": <raw_value> */
            int written = snprintf(extra + cur_len, extra_cap - cur_len,
                                   ", \"%s\": %.*s",
                                   key, (int)(val_end - val_start), val_start);
            if (written < 0) goto cleanup;
            p = val_end;
        }

        free(key);
        key = NULL;
    }

    /* Commit wallpaper path */
    free(cfg->wallpaper_path);
    cfg->wallpaper_path = new_wp;
    new_wp = NULL;

    /* Store extra fields globally */
    free(g_extra_json);
    g_extra_json = extra;
    extra = NULL;

    ret = 0;

cleanup:
    free(key);
    free(val_str);
    free(new_wp);
    free(extra);
    return ret;
}

/* ── to_json ─────────────────────────────────────────────────────────────── */

char *zen_theme_config_to_json(const struct ZenThemeConfig *cfg) {
    if (!cfg) return NULL;

    const char *dark_str = cfg->dark_mode ? "true" : "false";
    const char *extra    = g_extra_json ? g_extra_json : "";

    /* Calculate buffer size */
    size_t wp_len = cfg->wallpaper_path ? strlen(cfg->wallpaper_path) : 0;
    /* Base: {"dark_mode": true/false, "wallpaper_path": "..."<extra>}\n\0 */
    size_t cap = 64 + wp_len + strlen(extra) + 4;
    char *out = calloc(1, cap);
    if (!out) return NULL;

    int n;
    if (cfg->wallpaper_path) {
        n = snprintf(out, cap,
                     "{\"dark_mode\": %s, \"wallpaper_path\": \"%s\"%s}",
                     dark_str, cfg->wallpaper_path, extra);
    } else {
        n = snprintf(out, cap,
                     "{\"dark_mode\": %s%s}",
                     dark_str, extra);
    }

    if (n < 0 || (size_t)n >= cap) {
        free(out);
        return NULL;
    }
    return out;
}

/* ── load / save ─────────────────────────────────────────────────────────── */

int zen_theme_config_load(struct ZenThemeConfig *cfg, const char *path) {
    int   ret  = -1;
    FILE *fp   = NULL;
    char *buf  = NULL;

    zen_theme_config_init(cfg);
    if (!path) goto cleanup;

    fp = fopen(path, "r");
    if (!fp) {
        /* Missing file is not an error — defaults already applied */
        if (errno == ENOENT) {
            ret = 0;
        }
        goto cleanup;
    }

    /* Determine file size */
    if (fseek(fp, 0, SEEK_END) != 0) goto cleanup;
    long sz = ftell(fp);
    if (sz < 0) goto cleanup;
    rewind(fp);

    buf = calloc(1, (size_t)sz + 1);
    if (!buf) goto cleanup;

    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) goto cleanup;
    buf[sz] = '\0';

    if (zen_theme_config_from_json(cfg, buf) != 0) {
        /* Parse error — re-apply defaults */
        zen_theme_config_init(cfg);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (fp) fclose(fp);
    free(buf);
    return ret;
}

int zen_theme_config_save(const struct ZenThemeConfig *cfg, const char *path) {
    int   ret  = -1;
    FILE *fp   = NULL;
    char *json = NULL;

    if (!cfg || !path) goto cleanup;

    json = zen_theme_config_to_json(cfg);
    if (!json) goto cleanup;

    fp = fopen(path, "w");
    if (!fp) goto cleanup;

    if (fputs(json, fp) == EOF) goto cleanup;
    if (fputc('\n', fp) == EOF) goto cleanup;

    ret = 0;

cleanup:
    if (fp) fclose(fp);
    free(json);
    return ret;
}

/* ── get_palette ─────────────────────────────────────────────────────────── */

const struct ZenColorPalette *zen_theme_config_get_palette(
    const struct ZenThemeConfig *cfg)
{
    if (!cfg) return &DEFAULT_DARK;
    return cfg->dark_mode ? &cfg->dark_palette : &cfg->light_palette;
}
