/*
 * Zen OS Shell — Desktop Entry Parser API
 *
 * Parses freedesktop Desktop Entry Specification v1.5 (.desktop files).
 * Only the [Desktop Entry] group is processed; other groups are ignored.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_DESKTOP_ENTRY_H
#define ZEN_SHELL_DESKTOP_ENTRY_H

#include <stdbool.h>

/* Parsed desktop entry. */
struct ZenDesktopEntry {
    char   *name;             /* Name= */
    char   *exec;             /* Exec= (% codes stripped) */
    char   *icon;             /* Icon= */
    char   *type;             /* Type= (must be "Application") */
    char  **categories;       /* Categories= (semicolon-separated) */
    int     categories_count;
    char  **keywords;         /* Keywords= (semicolon-separated) */
    int     keywords_count;
    bool    no_display;       /* NoDisplay= */
    bool    hidden;           /* Hidden= */
};

/* Parse a .desktop file at the given path.
 * Returns 0 on success, -1 on parse error. */
int zen_desktop_entry_parse(struct ZenDesktopEntry *entry, const char *path);

/* Parse a .desktop file from a string buffer.
 * Returns 0 on success, -1 on parse error. */
int zen_desktop_entry_parse_string(struct ZenDesktopEntry *entry,
                                   const char *content);

/* Format a desktop entry back to .desktop file format string.
 * Caller must free(). Returns NULL on failure. */
char *zen_desktop_entry_format(const struct ZenDesktopEntry *entry);

/* Returns true if the entry should be displayed in the launcher
 * (Type=Application, NoDisplay!=true, Hidden!=true). */
bool zen_desktop_entry_is_visible(const struct ZenDesktopEntry *entry);

/* Free all heap-allocated fields in the entry. */
void zen_desktop_entry_cleanup(struct ZenDesktopEntry *entry);

/* Scan directories for .desktop files and build an array.
 * dirs: NULL-terminated array of directory paths to scan.
 * out_entries: receives allocated array (caller frees with cleanup + free).
 * out_count: receives number of entries.
 * Returns 0 on success, -1 on failure. */
int zen_desktop_entry_scan(const char **dirs,
                           struct ZenDesktopEntry **out_entries,
                           int *out_count);

#endif /* ZEN_SHELL_DESKTOP_ENTRY_H */
