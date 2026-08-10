/*
 * Zen OS Shell — Test State Snapshot Hooks
 *
 * Serializes observable shell state to a JSON file that zen-test can query
 * via guest-side helper commands.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>

#include <wlr/util/log.h>

#include "zen/app-launcher.h"
#include "zen/models.h"
#include "zen/shelf.h"
#include "zen/shell.h"
#include "zen/shell-testctl.h"
#include "zen/test-hooks.h"

#ifdef ZEN_ENABLE_TEST_HOOKS

static const char *shelf_position_name(ZenShelfPosition position)
{
    switch (position) {
    case ZEN_SHELF_TOP:
        return "top";
    case ZEN_SHELF_LEFT:
        return "left";
    case ZEN_SHELF_RIGHT:
        return "right";
    case ZEN_SHELF_BOTTOM:
    default:
        return "bottom";
    }
}

static void write_shelf_state(FILE *stream, const struct ZenShelf *shelf)
{
    const char *position = "bottom";
    bool auto_hide = false;
    int icon_size = 0;

    if (shelf->config) {
        position = shelf_position_name(shelf->config->position);
        auto_hide = shelf->config->auto_hide;
        icon_size = shelf->config->icon_size;
    }

    fputs("\"shelf\":{", stream);
    fprintf(stream,
            "\"visible\":%s,"
            "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
            "\"position\":",
            shelf->bar_buffer ? "true" : "false",
            shelf->x, shelf->y, shelf->width, shelf->height);
    zen_test_hooks_write_json_string(stream, position);
    fprintf(stream,
            ",\"auto_hide\":%s,\"auto_hide_active\":%s,"
            "\"context_menu_visible\":%s,\"icon_size\":%d,"
            "\"pinned_count\":%d,\"clock_text\":",
            auto_hide ? "true" : "false",
            shelf->auto_hide_active ? "true" : "false",
            shelf->context_menu_visible ? "true" : "false",
            icon_size,
            shelf->pinned_count);
    zen_test_hooks_write_json_string(stream, shelf->clock_text);
    fputs(",\"pinned_apps\":[", stream);

    for (int i = 0; i < shelf->pinned_count; i++) {
        const struct ZenAppEntry *entry = &shelf->pinned_apps[i];
        if (i > 0) {
            fputc(',', stream);
        }
        fputs("{\"app_id\":", stream);
        zen_test_hooks_write_json_string(stream, entry->app_id);
        fputs(",\"name\":", stream);
        zen_test_hooks_write_json_string(stream, entry->name);
        fprintf(stream, ",\"is_running\":%s}", entry->is_running ? "true" : "false");
    }

    fputs("]}", stream);
}

static void write_launcher_state(FILE *stream, const struct ZenAppLauncher *launcher)
{
    fputs("\"launcher\":{", stream);
    fprintf(stream,
            "\"available\":true,\"visible\":%s,"
            "\"filtered_count\":%d,\"selected_index\":%d,"
            "\"grid_cols\":%d,\"grid_rows\":%d,\"search_text\":",
            launcher->visible ? "true" : "false",
            launcher->filtered_count,
            launcher->selected_index,
            launcher->grid_cols,
            launcher->grid_rows);
    zen_test_hooks_write_json_string(stream, launcher->search_text);
    fputc('}', stream);
}

static void write_tray_indicator(FILE *stream,
                                 const struct ZenTrayIndicator *indicator)
{
    fprintf(stream, "{\"available\":%s,\"label\":",
            indicator && indicator->available ? "true" : "false");
    zen_test_hooks_write_json_string(stream, indicator ? indicator->label : NULL);
    fprintf(stream, ",\"icon_index\":%d}", indicator ? indicator->icon_index : -1);
}

static void write_tray_state(FILE *stream, const struct ZenShelf *shelf)
{
    fputs("\"tray\":{", stream);
    fputs("\"battery\":", stream);
    write_tray_indicator(stream, shelf ? &shelf->battery : NULL);
    fputs(",\"bluetooth\":", stream);
    write_tray_indicator(stream, shelf ? &shelf->bluetooth : NULL);
    fputs(",\"network\":", stream);
    write_tray_indicator(stream, shelf ? &shelf->network : NULL);
    fputs(",\"volume\":", stream);
    write_tray_indicator(stream, shelf ? &shelf->volume : NULL);
    fputs(",\"storage\":", stream);
    write_tray_indicator(stream, shelf ? &shelf->storage : NULL);
    fputc('}', stream);
}

static void write_stub_section(FILE *stream, const char *name, bool available)
{
    fprintf(stream, "\"%s\":{\"available\":%s}", name, available ? "true" : "false");
}

void zen_shell_testctl_sync(struct ZenShell *shell)
{
    char *json = NULL;
    size_t size = 0;
    FILE *stream = NULL;

    if (!shell) {
        return;
    }

    stream = open_memstream(&json, &size);
    if (!stream) {
        return;
    }

    fprintf(stream, "{\"dark_mode\":%s,", shell->dark_mode ? "true" : "false");

    if (shell->theme) {
        fputs("\"theme\":{\"palette\":", stream);
        zen_test_hooks_write_json_string(stream, shell->dark_mode ? "dark" : "light");
        fputs("},", stream);
    } else {
        fputs("\"theme\":null,", stream);
    }

    if (shell->shelf) {
        write_shelf_state(stream, shell->shelf);
    } else {
        fputs("\"shelf\":null", stream);
    }

    fputc(',', stream);
    if (shell->launcher) {
        write_launcher_state(stream, shell->launcher);
    } else {
        fputs("\"launcher\":{\"available\":false}", stream);
    }

    fputc(',', stream);
    write_tray_state(stream, shell->shelf);

    fputc(',', stream);
    write_stub_section(stream, "quick_settings", shell->quick_settings != NULL);

    fputc(',', stream);
    write_stub_section(stream, "notifications", shell->notifications != NULL);

    fputc('}', stream);

    if (fclose(stream) != 0) {
        free(json);
        return;
    }

    if (zen_test_hooks_write_atomic(ZEN_SHELL_STATE_PATH, json) != 0) {
        wlr_log(WLR_INFO, "%s", "shell-testctl: failed to write shell state");
    }

    free(json);
}

void zen_shell_testctl_clear(void)
{
    if (zen_test_hooks_remove_file(ZEN_SHELL_STATE_PATH) != 0) {
        wlr_log(WLR_DEBUG, "%s", "shell-testctl: failed to remove shell state");
    }
}

#else

void zen_shell_testctl_sync(struct ZenShell *shell)
{
    (void)shell;
}

void zen_shell_testctl_clear(void)
{
}

#endif
