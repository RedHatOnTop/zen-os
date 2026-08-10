/*
 * Zen OS Shell — Desktop Entry Parser
 *
 * Implements freedesktop Desktop Entry Specification v1.5 parsing.
 * Only the [Desktop Entry] group is processed; other groups are ignored.
 * Uses pure C17 + POSIX — no external ini/json parsing libraries.
 *
 * Scan directories (in priority order):
 *   ~/.local/share/applications/
 *   /var/lib/flatpak/exports/share/applications/
 *   /usr/share/applications/
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "zen/desktop-entry.h"

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Strip leading and trailing ASCII whitespace in-place.
 * Returns the pointer (same buffer, shifted start). */
static char *trim_ws(char *s)
{
    if (!s) return s;
    /* leading */
    while (*s && isspace((unsigned char)*s)) s++;
    /* trailing */
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

/* Case-insensitive strcmp. */
static int ci_strcmp(const char *a, const char *b)
{
    while (*a && *b) {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff != 0) return diff;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Parse a boolean value per freedesktop spec.
 * "true" or "1" (case-insensitive) → true, anything else → false. */
static bool parse_bool(const char *val)
{
    if (!val) return false;
    return (ci_strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
}

/* Split a semicolon-separated list into a heap-allocated char** array.
 * Empty tokens (consecutive semicolons, trailing semicolon) are skipped.
 * Returns 0 on success, -1 on allocation failure.
 * *out_arr and *out_count are set on success. */
static int split_semicolon_list(const char *val, char ***out_arr, int *out_count)
{
    *out_arr   = NULL;
    *out_count = 0;

    if (!val || *val == '\0') return 0;

    /* Count tokens first */
    int count = 0;
    const char *p = val;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t len = semi ? (size_t)(semi - p) : strlen(p);
        if (len > 0) count++;
        p = semi ? semi + 1 : p + len;
    }

    if (count == 0) return 0;

    char **arr = calloc((size_t)count, sizeof(char *));
    if (!arr) return -1;

    int idx = 0;
    p = val;
    while (*p && idx < count) {
        const char *semi = strchr(p, ';');
        size_t len = semi ? (size_t)(semi - p) : strlen(p);
        if (len > 0) {
            arr[idx] = calloc(1, len + 1);
            if (!arr[idx]) {
                /* cleanup partial */
                for (int i = 0; i < idx; i++) free(arr[i]);
                free(arr);
                return -1;
            }
            memcpy(arr[idx], p, len);
            arr[idx][len] = '\0';
            idx++;
        }
        p = semi ? semi + 1 : p + len;
    }

    *out_arr   = arr;
    *out_count = idx;
    return 0;
}

/* Strip Exec field % field codes per freedesktop spec.
 * Codes stripped: %f %u %F %U %i %c %k (and %% → %).
 * Returns a newly allocated string; caller must free(). */
static char *strip_exec_codes(const char *exec)
{
    if (!exec) return NULL;

    size_t len = strlen(exec);
    /* Output can only be shorter than input */
    char *out = calloc(1, len + 1);
    if (!out) return NULL;

    const char *src = exec;
    char *dst = out;

    while (*src) {
        if (*src == '%' && *(src + 1)) {
            char code = *(src + 1);
            switch (code) {
            case 'f': case 'u': case 'F': case 'U':
            case 'i': case 'c': case 'k':
                /* skip both chars */
                src += 2;
                /* also eat a trailing space that was separating the code */
                if (*src == ' ') src++;
                break;
            case '%':
                /* literal % */
                *dst++ = '%';
                src += 2;
                break;
            default:
                /* unknown code — pass through as-is */
                *dst++ = *src++;
                break;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    /* Trim any trailing whitespace left after stripping codes */
    size_t olen = strlen(out);
    while (olen > 0 && isspace((unsigned char)out[olen - 1])) {
        out[--olen] = '\0';
    }

    return out;
}

/* ── parse_string implementation ─────────────────────────────────────────── */

int zen_desktop_entry_parse_string(struct ZenDesktopEntry *entry,
                                   const char *content)
{
    int ret = -1;

    if (!entry || !content) return -1;
    memset(entry, 0, sizeof(*entry));

    /* Work on a mutable copy so we can tokenise in-place */
    char *buf = strdup(content);
    if (!buf) return -1;

    bool in_desktop_entry = false;
    char *line_ctx        = NULL;
    char *line            = strtok_r(buf, "\n", &line_ctx);

    while (line) {
        /* Strip trailing \r for CRLF files */
        size_t llen = strlen(line);
        if (llen > 0 && line[llen - 1] == '\r') line[--llen] = '\0';

        char *trimmed = trim_ws(line);

        /* Skip blank lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            line = strtok_r(NULL, "\n", &line_ctx);
            continue;
        }

        /* Group header */
        if (*trimmed == '[') {
            char *close = strchr(trimmed, ']');
            if (close) {
                *close = '\0';
                const char *group = trimmed + 1;
                in_desktop_entry = (strcmp(group, "Desktop Entry") == 0);
            }
            line = strtok_r(NULL, "\n", &line_ctx);
            continue;
        }

        if (!in_desktop_entry) {
            line = strtok_r(NULL, "\n", &line_ctx);
            continue;
        }

        /* Key=Value */
        char *eq = strchr(trimmed, '=');
        if (!eq) {
            line = strtok_r(NULL, "\n", &line_ctx);
            continue;
        }

        *eq = '\0';
        char *key = trim_ws(trimmed);
        char *val = trim_ws(eq + 1);

        if (strcmp(key, "Name") == 0) {
            free(entry->name);
            entry->name = strdup(val);
            if (!entry->name) goto cleanup;

        } else if (strcmp(key, "Exec") == 0) {
            char *stripped = strip_exec_codes(val);
            if (!stripped) goto cleanup;
            free(entry->exec);
            entry->exec = stripped;

        } else if (strcmp(key, "Icon") == 0) {
            free(entry->icon);
            entry->icon = strdup(val);
            if (!entry->icon) goto cleanup;

        } else if (strcmp(key, "Type") == 0) {
            free(entry->type);
            entry->type = strdup(val);
            if (!entry->type) goto cleanup;

        } else if (strcmp(key, "Categories") == 0) {
            /* Free previous if re-encountered */
            for (int i = 0; i < entry->categories_count; i++)
                free(entry->categories[i]);
            free(entry->categories);
            entry->categories       = NULL;
            entry->categories_count = 0;
            if (split_semicolon_list(val, &entry->categories,
                                     &entry->categories_count) < 0) {
                goto cleanup;
            }

        } else if (strcmp(key, "Keywords") == 0) {
            for (int i = 0; i < entry->keywords_count; i++)
                free(entry->keywords[i]);
            free(entry->keywords);
            entry->keywords       = NULL;
            entry->keywords_count = 0;
            if (split_semicolon_list(val, &entry->keywords,
                                     &entry->keywords_count) < 0) {
                goto cleanup;
            }

        } else if (strcmp(key, "NoDisplay") == 0) {
            entry->no_display = parse_bool(val);

        } else if (strcmp(key, "Hidden") == 0) {
            entry->hidden = parse_bool(val);
        }

        line = strtok_r(NULL, "\n", &line_ctx);
    }

    /* Validate required fields */
    if (!entry->name || !entry->exec) {
        fprintf(stderr, "zen-desktop-entry: missing Name or Exec field\n");
        goto cleanup;
    }

    ret = 0;

cleanup:
    free(buf);
    if (ret < 0) zen_desktop_entry_cleanup(entry);
    return ret;
}

/* ── parse (file path) ───────────────────────────────────────────────────── */

int zen_desktop_entry_parse(struct ZenDesktopEntry *entry, const char *path)
{
    int   ret  = -1;
    char *buf  = NULL;
    FILE *fp   = NULL;

    if (!entry || !path) return -1;

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "zen-desktop-entry: cannot open '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    /* Read entire file into a buffer */
    if (fseek(fp, 0, SEEK_END) < 0) goto cleanup;
    long fsize = ftell(fp);
    if (fsize < 0) goto cleanup;
    rewind(fp);

    buf = calloc(1, (size_t)fsize + 1);
    if (!buf) goto cleanup;

    if (fread(buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
        fprintf(stderr, "zen-desktop-entry: read error on '%s'\n", path);
        goto cleanup;
    }
    buf[fsize] = '\0';

    ret = zen_desktop_entry_parse_string(entry, buf);
    if (ret < 0) {
        fprintf(stderr, "zen-desktop-entry: parse error in '%s'\n", path);
    }

cleanup:
    if (fp) fclose(fp);
    free(buf);
    return ret;
}

/* ── format ──────────────────────────────────────────────────────────────── */

char *zen_desktop_entry_format(const struct ZenDesktopEntry *entry)
{
    if (!entry) return NULL;

    /* Estimate buffer size: header + fixed fields + lists */
    size_t cap = 256;
    if (entry->name)  cap += strlen(entry->name)  + 8;
    if (entry->exec)  cap += strlen(entry->exec)  + 8;
    if (entry->icon)  cap += strlen(entry->icon)  + 8;
    if (entry->type)  cap += strlen(entry->type)  + 8;
    for (int i = 0; i < entry->categories_count; i++)
        cap += strlen(entry->categories[i]) + 2;
    for (int i = 0; i < entry->keywords_count; i++)
        cap += strlen(entry->keywords[i]) + 2;

    char *out = calloc(1, cap);
    if (!out) return NULL;

    int pos = 0;

    pos += snprintf(out + pos, cap - (size_t)pos, "[Desktop Entry]\n");

    if (entry->type)
        pos += snprintf(out + pos, cap - (size_t)pos, "Type=%s\n", entry->type);

    if (entry->name)
        pos += snprintf(out + pos, cap - (size_t)pos, "Name=%s\n", entry->name);

    if (entry->exec)
        pos += snprintf(out + pos, cap - (size_t)pos, "Exec=%s\n", entry->exec);

    if (entry->icon)
        pos += snprintf(out + pos, cap - (size_t)pos, "Icon=%s\n", entry->icon);

    if (entry->no_display)
        pos += snprintf(out + pos, cap - (size_t)pos, "NoDisplay=true\n");

    if (entry->hidden)
        pos += snprintf(out + pos, cap - (size_t)pos, "Hidden=true\n");

    if (entry->categories_count > 0) {
        pos += snprintf(out + pos, cap - (size_t)pos, "Categories=");
        for (int i = 0; i < entry->categories_count; i++) {
            pos += snprintf(out + pos, cap - (size_t)pos,
                            "%s;", entry->categories[i]);
        }
        pos += snprintf(out + pos, cap - (size_t)pos, "\n");
    }

    if (entry->keywords_count > 0) {
        pos += snprintf(out + pos, cap - (size_t)pos, "Keywords=");
        for (int i = 0; i < entry->keywords_count; i++) {
            pos += snprintf(out + pos, cap - (size_t)pos,
                            "%s;", entry->keywords[i]);
        }
        pos += snprintf(out + pos, cap - (size_t)pos, "\n");
    }

    return out;
}

/* ── is_visible ──────────────────────────────────────────────────────────── */

bool zen_desktop_entry_is_visible(const struct ZenDesktopEntry *entry)
{
    if (!entry) return false;
    if (entry->no_display) return false;
    if (entry->hidden)     return false;
    if (!entry->type)      return false;
    return (strcmp(entry->type, "Application") == 0);
}

/* ── cleanup ─────────────────────────────────────────────────────────────── */

void zen_desktop_entry_cleanup(struct ZenDesktopEntry *entry)
{
    if (!entry) return;

    free(entry->name);
    free(entry->exec);
    free(entry->icon);
    free(entry->type);

    for (int i = 0; i < entry->categories_count; i++)
        free(entry->categories[i]);
    free(entry->categories);

    for (int i = 0; i < entry->keywords_count; i++)
        free(entry->keywords[i]);
    free(entry->keywords);

    memset(entry, 0, sizeof(*entry));
}

/* ── scan ────────────────────────────────────────────────────────────────── */

int zen_desktop_entry_scan(const char **dirs,
                           struct ZenDesktopEntry **out_entries,
                           int *out_count)
{
    int                  ret      = -1;
    struct ZenDesktopEntry *arr   = NULL;
    int                  capacity = 0;
    int                  count    = 0;

    if (!dirs || !out_entries || !out_count) return -1;

    *out_entries = NULL;
    *out_count   = 0;

    for (int d = 0; dirs[d] != NULL; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) {
            /* Directory may not exist — not an error */
            continue;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            /* Only process .desktop files */
            const char *name = ent->d_name;
            size_t nlen = strlen(name);
            if (nlen < 8 || strcmp(name + nlen - 8, ".desktop") != 0)
                continue;

            /* Build full path */
            size_t dirlen  = strlen(dirs[d]);
            size_t pathlen = dirlen + 1 + nlen + 1;
            char *path = calloc(1, pathlen);
            if (!path) {
                closedir(dir);
                goto cleanup;
            }
            snprintf(path, pathlen, "%s/%s", dirs[d], name);

            /* Grow array if needed */
            if (count >= capacity) {
                int new_cap = capacity == 0 ? 32 : capacity * 2;
                struct ZenDesktopEntry *tmp = realloc(
                    arr, (size_t)new_cap * sizeof(*arr));
                if (!tmp) {
                    free(path);
                    closedir(dir);
                    goto cleanup;
                }
                arr      = tmp;
                capacity = new_cap;
            }

            /* Parse — skip on error (log already emitted by parse) */
            if (zen_desktop_entry_parse(&arr[count], path) == 0) {
                count++;
            }

            free(path);
        }

        closedir(dir);
    }

    *out_entries = arr;
    *out_count   = count;
    arr          = NULL; /* ownership transferred */
    ret          = 0;

cleanup:
    if (arr) {
        for (int i = 0; i < count; i++)
            zen_desktop_entry_cleanup(&arr[i]);
        free(arr);
    }
    return ret;
}
