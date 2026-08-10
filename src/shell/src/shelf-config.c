/*
 * Zen OS Shell — Shelf Configuration Persistence
 *
 * JSON serialization/deserialization for ~/.config/zenos/shelf.json.
 * Hand-rolled minimal JSON parser — no external library.
 * Unknown fields are preserved via a static buffer for round-trip fidelity.
 *
 * JSON format:
 *   {"position": "bottom", "auto_hide": false, "icon_size": 32,
 *    "pinned_apps": ["app1", "app2"]}
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

#include "zen/models.h"
#include "zen/shelf-config.h"

/* ── Position string table ───────────────────────────────────────────────── */

static const char *const POSITION_STRINGS[] = {
    [ZEN_SHELF_BOTTOM] = "bottom",
    [ZEN_SHELF_LEFT]   = "left",
    [ZEN_SHELF_RIGHT]  = "right",
    [ZEN_SHELF_TOP]    = "top",
};

#define POSITION_COUNT ((int)(sizeof(POSITION_STRINGS) / sizeof(POSITION_STRINGS[0])))

/* ── Unknown-field preservation (single-instance config) ─────────────────── */

/* Stores any key-value pairs from the JSON that are not known fields.
 * Format: comma-prefixed fragment ready to splice into the output object,
 * e.g. ', "custom_key": "value", "flag": true'.
 * Empty string when there are no unknown fields. */
static char *g_extra_json = NULL;

/* ── Internal JSON helpers ───────────────────────────────────────────────── */

/* Skip whitespace. Returns pointer past whitespace. */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* Match a literal string at *p.  Returns pointer past the match, or NULL. */
static const char *match_lit(const char *p, const char *lit)
{
    size_t n = strlen(lit);
    if (strncmp(p, lit, n) == 0) return p + n;
    return NULL;
}

/* Parse a JSON string starting at the opening '"'.
 * Returns a heap-allocated copy of the string content (caller frees),
 * or NULL on error.  *end_out is set to the character after the closing '"'. */
static char *parse_json_string(const char *p, const char **end_out)
{
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
static const char *skip_json_value(const char *p)
{
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
            if (*p == '"') {
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

/* ── Validation ──────────────────────────────────────────────────────────── */

int zen_shelf_config_validate(ZenShelfConfig *cfg)
{
    int clamped = 0;

    if ((int)cfg->position < 0 || (int)cfg->position >= POSITION_COUNT) {
        cfg->position = ZEN_SHELF_BOTTOM;
        clamped++;
    }

    if (cfg->icon_size < 16) {
        cfg->icon_size = 16;
        clamped++;
    } else if (cfg->icon_size > 64) {
        cfg->icon_size = 64;
        clamped++;
    }

    return clamped;
}

/* ── from_json ───────────────────────────────────────────────────────────── */

int zen_shelf_config_from_json(ZenShelfConfig *cfg, const char *json)
{
    int    ret          = -1;
    char  *extra        = NULL;
    char  *extra_tmp    = NULL;
    char  *key          = NULL;
    char  *val_str      = NULL;
    char **new_apps     = NULL;
    int    new_apps_cnt = 0;

    /* Parsed field values (committed on success) */
    ZenShelfPosition parsed_position = ZEN_SHELF_BOTTOM;
    bool             parsed_auto_hide = false;
    int              parsed_icon_size = 32;
    bool             got_position  = false;
    bool             got_auto_hide = false;
    bool             got_icon_size = false;

    if (!cfg || !json) goto cleanup;

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

        if (strcmp(key, "position") == 0) {
            const char *val_end = NULL;
            val_str = parse_json_string(p, &val_end);
            if (!val_str) goto cleanup;

            /* Map string to enum; unknown strings clamp to bottom */
            parsed_position = ZEN_SHELF_BOTTOM;
            for (int i = 0; i < POSITION_COUNT; i++) {
                if (strcmp(val_str, POSITION_STRINGS[i]) == 0) {
                    parsed_position = (ZenShelfPosition)i;
                    break;
                }
            }
            free(val_str);
            val_str = NULL;
            got_position = true;
            p = val_end;

        } else if (strcmp(key, "auto_hide") == 0) {
            const char *after = match_lit(p, "true");
            if (after) {
                parsed_auto_hide = true;
                p = after;
            } else {
                after = match_lit(p, "false");
                if (!after) goto cleanup;
                parsed_auto_hide = false;
                p = after;
            }
            got_auto_hide = true;

        } else if (strcmp(key, "icon_size") == 0) {
            char *endptr = NULL;
            long v = strtol(p, &endptr, 10);
            if (endptr == p) goto cleanup;
            parsed_icon_size = (int)v;
            got_icon_size = true;
            p = endptr;

        } else if (strcmp(key, "pinned_apps") == 0) {
            /* Parse JSON array of strings */
            if (*p != '[') goto cleanup;
            p++;

            /* Free any previously parsed apps from a duplicate key */
            for (int i = 0; i < new_apps_cnt; i++) free(new_apps[i]);
            free(new_apps);
            new_apps     = NULL;
            new_apps_cnt = 0;

            while (1) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; p = skip_ws(p); }
                if (*p == ']') { p++; break; }
                if (!*p) goto cleanup;

                const char *item_end = NULL;
                char *item = parse_json_string(p, &item_end);
                if (!item) goto cleanup;

                char **tmp = realloc(new_apps,
                                     (size_t)(new_apps_cnt + 1) * sizeof(char *));
                if (!tmp) {
                    free(item);
                    goto cleanup;
                }
                new_apps = tmp;
                new_apps[new_apps_cnt++] = item;
                p = item_end;
            }

        } else {
            /* Unknown field — record raw key+value for preservation */
            const char *val_start = p;
            const char *val_end   = skip_json_value(p);
            if (!val_end) goto cleanup;

            size_t frag_len = 6 + strlen(key) + (size_t)(val_end - val_start);
            size_t cur_len  = strlen(extra);
            if (cur_len + frag_len + 1 > extra_cap) {
                extra_cap = cur_len + frag_len + 64;
                extra_tmp = realloc(extra, extra_cap);
                if (!extra_tmp) goto cleanup;
                extra     = extra_tmp;
                extra_tmp = NULL;
            }
            int written = snprintf(extra + cur_len, extra_cap - cur_len,
                                   ", \"%s\": %.*s",
                                   key, (int)(val_end - val_start), val_start);
            if (written < 0) goto cleanup;
            p = val_end;
        }

        free(key);
        key = NULL;
    }

    /* Commit: initialize cfg with defaults, then apply parsed values */
    zen_shelf_config_init(cfg);
    cfg->icon_size = 32; /* spec default overrides models.c default of 48 */

    if (got_position)  cfg->position  = parsed_position;
    if (got_auto_hide) cfg->auto_hide = parsed_auto_hide;
    if (got_icon_size) cfg->icon_size = parsed_icon_size;

    cfg->pinned_apps       = new_apps;
    cfg->pinned_apps_count = new_apps_cnt;
    new_apps     = NULL;
    new_apps_cnt = 0;

    /* Validate and clamp */
    zen_shelf_config_validate(cfg);

    /* Store extra fields globally */
    free(g_extra_json);
    g_extra_json = extra;
    extra        = NULL;

    ret = 0;

cleanup:
    free(key);
    free(val_str);
    free(extra);
    for (int i = 0; i < new_apps_cnt; i++) free(new_apps[i]);
    free(new_apps);
    return ret;
}

/* ── to_json ─────────────────────────────────────────────────────────────── */

char *zen_shelf_config_to_json(const ZenShelfConfig *cfg)
{
    if (!cfg) return NULL;

    /* Clamp position to valid range for the string table lookup */
    int pos_idx = (int)cfg->position;
    if (pos_idx < 0 || pos_idx >= POSITION_COUNT) pos_idx = ZEN_SHELF_BOTTOM;
    const char *pos_str   = POSITION_STRINGS[pos_idx];
    const char *hide_str  = cfg->auto_hide ? "true" : "false";
    const char *extra     = g_extra_json ? g_extra_json : "";

    /* Build pinned_apps array fragment */
    size_t apps_len = 2; /* "[]" */
    for (int i = 0; i < cfg->pinned_apps_count; i++) {
        apps_len += 4 + strlen(cfg->pinned_apps[i]); /* '"app",' */
    }

    char *apps_buf = calloc(1, apps_len + 1);
    if (!apps_buf) return NULL;

    apps_buf[0] = '[';
    size_t off = 1;
    for (int i = 0; i < cfg->pinned_apps_count; i++) {
        int n = snprintf(apps_buf + off, apps_len + 1 - off,
                         "%s\"%s\"",
                         (i > 0) ? ", " : "",
                         cfg->pinned_apps[i]);
        if (n < 0) { free(apps_buf); return NULL; }
        off += (size_t)n;
    }
    apps_buf[off++] = ']';
    apps_buf[off]   = '\0';

    /* Calculate total buffer size and build output */
    /* {"position": "bottom", "auto_hide": false, "icon_size": 32,
     *  "pinned_apps": [...]<extra>} */
    size_t cap = 80 + strlen(pos_str) + strlen(apps_buf) + strlen(extra) + 4;
    char *out = calloc(1, cap);
    if (!out) { free(apps_buf); return NULL; }

    int n = snprintf(out, cap,
                     "{\"position\": \"%s\", \"auto_hide\": %s, "
                     "\"icon_size\": %d, \"pinned_apps\": %s%s}",
                     pos_str, hide_str, cfg->icon_size, apps_buf, extra);

    free(apps_buf);

    if (n < 0 || (size_t)n >= cap) {
        free(out);
        return NULL;
    }
    return out;
}

/* ── load / save ─────────────────────────────────────────────────────────── */

int zen_shelf_config_load(ZenShelfConfig *cfg, const char *path)
{
    int   ret = -1;
    FILE *fp  = NULL;
    char *buf = NULL;

    zen_shelf_config_init(cfg);
    cfg->icon_size = 32; /* spec default */
    if (!path) goto cleanup;

    fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) {
            /* First run — write defaults to disk so the file exists next time */
            ret = zen_shelf_config_save(cfg, path);
            if (ret != 0) {
                /* Save failed (e.g. parent dir missing) — still return success
                 * with defaults; the caller can operate without persistence. */
                ret = 0;
            }
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

    if (zen_shelf_config_from_json(cfg, buf) != 0) {
        /* Parse error — re-apply defaults and warn */
        fprintf(stderr, "shelf-config: shelf.json contains invalid JSON, "
                "using defaults\n");
        zen_shelf_config_cleanup(cfg);
        zen_shelf_config_init(cfg);
        cfg->icon_size = 32;
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (fp) fclose(fp);
    free(buf);
    return ret;
}

int zen_shelf_config_save(const ZenShelfConfig *cfg, const char *path)
{
    int   ret  = -1;
    FILE *fp   = NULL;
    char *json = NULL;
    char *dir  = NULL;

    if (!cfg || !path) goto cleanup;

    json = zen_shelf_config_to_json(cfg);
    if (!json) goto cleanup;

    /* Ensure parent directory exists */
    {
        size_t path_len = strlen(path);
        dir = calloc(1, path_len + 1);
        if (!dir) goto cleanup;
        memcpy(dir, path, path_len);

        /* Walk backwards to find the last '/' */
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = '\0';
            /* Create each component of the path */
            for (char *p = dir + 1; *p; p++) {
                if (*p == '/') {
                    *p = '\0';
                    mkdir(dir, 0755);
                    *p = '/';
                }
            }
            mkdir(dir, 0755);
        }
    }

    fp = fopen(path, "w");
    if (!fp) goto cleanup;

    if (fputs(json, fp) == EOF) goto cleanup;
    if (fputc('\n', fp) == EOF) goto cleanup;

    ret = 0;

cleanup:
    if (fp) fclose(fp);
    free(json);
    free(dir);
    return ret;
}
