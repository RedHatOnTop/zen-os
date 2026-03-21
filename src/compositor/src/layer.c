/*
 * Zen OS Compositor — Layer Shell Protocol Implementation
 *
 * Implements wlr-layer-shell-v1 support: panels, wallpapers, overlays,
 * and other shell surfaces that anchor to screen edges with exclusive zones.
 *
 * Architecture:
 *   - zen_layer_init()  creates the wlr_layer_shell_v1 global and registers
 *     the new_surface listener.
 *   - new_layer_surface listener allocates a ZenLayerSurface, creates a
 *     wlr_scene_layer_surface_v1 node, and registers map/unmap/destroy
 *     listeners.
 *   - wlr_scene_layer_surface_v1 handles exclusive zone geometry
 *     automatically via wlr_scene_layer_surface_v1_configure().
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "zen/compositor.h"
#include "zen/layer.h"

/* ── Forward declarations ────────────────────────────────────────────────── */

static void handle_new_layer_surface(struct wl_listener *listener, void *data);
static void handle_layer_surface_map(struct wl_listener *listener, void *data);
static void handle_layer_surface_unmap(struct wl_listener *listener, void *data);
static void handle_layer_surface_destroy(struct wl_listener *listener, void *data);

/* ── zen_layer_init ──────────────────────────────────────────────────────── */

int zen_layer_init(struct ZenCompositor *compositor) {
    int ret = -1;

    /* Initialize the layer surfaces list. */
    wl_list_init(&compositor->layer_surfaces);

    /* Create the wlr_layer_shell_v1 global (version 4). */
    compositor->layer_shell =
        wlr_layer_shell_v1_create(compositor->wl_display, 4);
    if (!compositor->layer_shell) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_layer_shell_v1");
        goto cleanup;
    }

    /* Register listener for new layer shell surfaces. */
    compositor->new_layer_surface.notify = handle_new_layer_surface;
    wl_signal_add(&compositor->layer_shell->events.new_surface,
                  &compositor->new_layer_surface);

    wlr_log(WLR_INFO, "%s", "Layer shell initialized (wlr-layer-shell-v1 v4)");
    ret = 0;

cleanup:
    return ret;
}

/* ── zen_layer_destroy ───────────────────────────────────────────────────── */

void zen_layer_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Guard against uninitialized list (list head next is NULL if never
     * initialized, but wl_list_init sets both pointers to self). */
    if (!compositor->layer_surfaces.next) {
        return;
    }

    /* Safely iterate and free all remaining ZenLayerSurface entries. */
    struct ZenLayerSurface *surface, *tmp;
    wl_list_for_each_safe(surface, tmp, &compositor->layer_surfaces, link) {
        wl_list_remove(&surface->map.link);
        wl_list_remove(&surface->unmap.link);
        wl_list_remove(&surface->destroy.link);
        wl_list_remove(&surface->link);
        free(surface);
    }

    /* The layer_shell global is destroyed when the wl_display is destroyed. */
    compositor->layer_shell = NULL;

    wlr_log(WLR_INFO, "%s", "Layer shell destroyed");
}

/* ── new_layer_surface listener ─────────────────────────────────────────── */

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    /* Allocate per-surface state. */
    struct ZenLayerSurface *zen_layer_surface =
        calloc(1, sizeof(*zen_layer_surface));
    if (!zen_layer_surface) {
        wlr_log(WLR_ERROR, "%s", "Failed to allocate ZenLayerSurface");
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    zen_layer_surface->layer_surface = layer_surface;
    zen_layer_surface->compositor    = compositor;

    /*
     * Determine the scene tree based on the requested layer.
     * For simplicity, all layers use shell_overlay_tree — the scene graph
     * ordering handles visual priority via wlr_scene_layer_surface_v1.
     * A production compositor would use separate trees per layer.
     */
    struct wlr_scene_tree *scene_tree = compositor->shell_overlay_tree;
    if (!scene_tree) {
        /* Fallback to root scene tree if overlay tree is not yet created. */
        scene_tree = &compositor->scene->tree;
    }

    /* Create the scene layer surface node. */
    zen_layer_surface->scene_layer =
        wlr_scene_layer_surface_v1_create(scene_tree, layer_surface);
    if (!zen_layer_surface->scene_layer) {
        wlr_log(WLR_ERROR, "%s",
                "Failed to create wlr_scene_layer_surface_v1");
        free(zen_layer_surface);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    /* Register event listeners. */
    zen_layer_surface->map.notify = handle_layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map,
                  &zen_layer_surface->map);

    zen_layer_surface->unmap.notify = handle_layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap,
                  &zen_layer_surface->unmap);

    zen_layer_surface->destroy.notify = handle_layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy,
                  &zen_layer_surface->destroy);

    /* Prepend to the compositor's layer surfaces list. */
    wl_list_insert(&compositor->layer_surfaces, &zen_layer_surface->link);

    /*
     * Initial configure: if an output is associated, send initial dimensions
     * so the client can commit its first buffer.
     * wlr_scene_layer_surface_v1 handles exclusive zone geometry automatically
     * via wlr_scene_layer_surface_v1_configure().
     */
    if (layer_surface->output) {
        struct wlr_box output_box;
        wlr_output_layout_get_box(compositor->output_layout,
                                   layer_surface->output, &output_box);
        if (output_box.width > 0 && output_box.height > 0) {
            wlr_layer_surface_v1_configure(layer_surface,
                                            (uint32_t)output_box.width,
                                            (uint32_t)output_box.height);
        }
    }

    wlr_log(WLR_INFO, "New layer surface: namespace='%s' layer=%d",
            layer_surface->namespace ? layer_surface->namespace : "(null)",
            (int)layer_surface->pending.layer);
}

/* ── map listener ────────────────────────────────────────────────────────── */

static void handle_layer_surface_map(struct wl_listener *listener,
                                      void *data __attribute__((unused))) {
    struct ZenLayerSurface *zen_layer_surface =
        wl_container_of(listener, zen_layer_surface, map);

    wlr_scene_node_set_enabled(
        &zen_layer_surface->scene_layer->tree->node, true);

    wlr_log(WLR_DEBUG, "Layer surface mapped: namespace='%s'",
            zen_layer_surface->layer_surface->namespace
                ? zen_layer_surface->layer_surface->namespace
                : "(null)");
}

/* ── unmap listener ──────────────────────────────────────────────────────── */

static void handle_layer_surface_unmap(struct wl_listener *listener,
                                        void *data __attribute__((unused))) {
    struct ZenLayerSurface *zen_layer_surface =
        wl_container_of(listener, zen_layer_surface, unmap);

    wlr_scene_node_set_enabled(
        &zen_layer_surface->scene_layer->tree->node, false);

    wlr_log(WLR_DEBUG, "Layer surface unmapped: namespace='%s'",
            zen_layer_surface->layer_surface->namespace
                ? zen_layer_surface->layer_surface->namespace
                : "(null)");
}

/* ── destroy listener ────────────────────────────────────────────────────── */

static void handle_layer_surface_destroy(struct wl_listener *listener,
                                          void *data __attribute__((unused))) {
    struct ZenLayerSurface *zen_layer_surface =
        wl_container_of(listener, zen_layer_surface, destroy);

    wlr_log(WLR_DEBUG, "Layer surface destroyed: namespace='%s'",
            zen_layer_surface->layer_surface->namespace
                ? zen_layer_surface->layer_surface->namespace
                : "(null)");

    wl_list_remove(&zen_layer_surface->map.link);
    wl_list_remove(&zen_layer_surface->unmap.link);
    wl_list_remove(&zen_layer_surface->destroy.link);
    wl_list_remove(&zen_layer_surface->link);

    free(zen_layer_surface);
}
