/*
 * Zen OS Compositor — Test State Snapshot Hooks
 *
 * Serializes observable compositor state to a JSON file that zen-test can
 * query via guest-side helper commands.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "zen/compositor-testctl.h"
#include "zen/compositor.h"
#include "zen/test-hooks.h"
#include "zen/xdg.h"

#ifdef ZEN_ENABLE_TEST_HOOKS

static int count_outputs(struct ZenCompositor *compositor)
{
    int count = 0;
    struct wlr_output_layout_output *layout_output = NULL;

    if (!compositor || !compositor->output_layout) {
        return 0;
    }

    wl_list_for_each(layout_output, &compositor->output_layout->outputs, link) {
        count++;
    }

    return count;
}

static int count_toplevels(struct ZenCompositor *compositor)
{
    int count = 0;
    struct ZenToplevel *toplevel = NULL;

    if (!compositor || compositor->toplevels.next == NULL
        || compositor->toplevels.prev == NULL) {
        return 0;
    }

    wl_list_for_each(toplevel, &compositor->toplevels, link) {
        count++;
    }

    return count;
}

static void write_toplevel(FILE *stream, struct ZenToplevel *toplevel)
{
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool mapped = false;
    struct wlr_scene_node *node = NULL;

    if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
        width = toplevel->xdg_toplevel->base->current.geometry.width;
        height = toplevel->xdg_toplevel->base->current.geometry.height;
        mapped = toplevel->xdg_toplevel->base->surface->mapped;
    }
    if (toplevel->scene_tree) {
        node = (struct wlr_scene_node *)toplevel->scene_tree;
        x = node->x;
        y = node->y;
    }

    fputs("{\"app_id\":", stream);
    zen_test_hooks_write_json_string(stream, toplevel->app_id);
    fputs(",\"title\":", stream);
    zen_test_hooks_write_json_string(stream, toplevel->title);
    fprintf(stream,
            ",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"mapped\":%s}",
            x,
            y,
            width,
            height,
            mapped ? "true" : "false");
}

static void write_scene_tree_state(FILE *stream, struct wlr_scene_tree *tree)
{
    int x = 0;
    int y = 0;
    bool enabled = false;
    struct wlr_scene_node *node = NULL;

    if (!tree) {
        fputs("{\"available\":false}", stream);
        return;
    }

    node = (struct wlr_scene_node *)tree;
    enabled = node->enabled;
    x = node->x;
    y = node->y;

    fprintf(stream,
            "{\"available\":true,\"enabled\":%s,\"x\":%d,\"y\":%d}",
            enabled ? "true" : "false",
            x,
            y);
}

void zen_compositor_testctl_sync(struct ZenCompositor *compositor)
{
    char *json = NULL;
    size_t size = 0;
    FILE *stream = NULL;
    struct wlr_output_layout_output *layout_output = NULL;
    struct ZenToplevel *toplevel = NULL;
    bool first = true;

    if (!compositor) {
        return;
    }

    stream = open_memstream(&json, &size);
    if (!stream) {
        return;
    }

    fprintf(stream,
            "{\"dark_mode\":%s,\"shell_present\":%s,"
            "\"output_count\":%d,\"outputs\":[",
            compositor->dark_mode ? "true" : "false",
            compositor->shell ? "true" : "false",
            count_outputs(compositor));

    if (compositor->output_layout) {
        wl_list_for_each(layout_output, &compositor->output_layout->outputs, link) {
            struct wlr_box box = {0};

            if (!first) {
                fputc(',', stream);
            }
            first = false;

            wlr_output_layout_get_box(compositor->output_layout,
                                      layout_output->output, &box);
            fputs("{\"name\":", stream);
            zen_test_hooks_write_json_string(stream, layout_output->output->name);
            fprintf(stream,
                    ",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
                    box.x, box.y, box.width, box.height);
        }
    }

    fprintf(stream, "],\"toplevel_count\":%d,", count_toplevels(compositor));

    if (compositor->focused_toplevel) {
        fputs("\"focused_toplevel\":", stream);
        write_toplevel(stream, compositor->focused_toplevel);
    } else {
        fputs("\"focused_toplevel\":null", stream);
    }

    fputs(",\"shell_nodes\":{", stream);
    fputs("\"shelf\":", stream);
    write_scene_tree_state(stream, compositor->shelf_tree);
    fputs(",\"launcher\":", stream);
    write_scene_tree_state(stream, compositor->launcher_tree);
    fputs(",\"quick_settings\":", stream);
    write_scene_tree_state(stream, compositor->quick_settings_tree);
    fputs(",\"context_menu\":", stream);
    write_scene_tree_state(stream, compositor->context_menu_tree);
    fputs(",\"notifications\":", stream);
    write_scene_tree_state(stream, compositor->notification_tree);
    fputs("},\"toplevels\":[", stream);
    first = true;
    if (compositor->toplevels.next != NULL && compositor->toplevels.prev != NULL) {
        wl_list_for_each(toplevel, &compositor->toplevels, link) {
            if (!first) {
                fputc(',', stream);
            }
            first = false;
            write_toplevel(stream, toplevel);
        }
    }
    fputs("]}", stream);

    if (fclose(stream) != 0) {
        free(json);
        return;
    }

    if (zen_test_hooks_write_atomic(ZEN_COMPOSITOR_STATE_PATH, json) != 0) {
        wlr_log(WLR_INFO, "%s",
                "compositor-testctl: failed to write compositor state");
    }

    free(json);
}

void zen_compositor_testctl_clear(void)
{
    if (zen_test_hooks_remove_file(ZEN_COMPOSITOR_STATE_PATH) != 0) {
        wlr_log(WLR_DEBUG, "%s",
                "compositor-testctl: failed to remove compositor state");
    }
}

#else

void zen_compositor_testctl_sync(struct ZenCompositor *compositor)
{
    (void)compositor;
}

void zen_compositor_testctl_clear(void)
{
}

#endif
