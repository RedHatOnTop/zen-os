/*
 * Zen OS Compositor — Layer Shell Protocol (wlr-layer-shell-v1)
 *
 * Provides support for layer shell clients (panels, wallpapers, overlays)
 * that anchor surfaces to screen edges with exclusive zones.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_LAYER_H
#define ZEN_COMPOSITOR_LAYER_H

#include <wayland-server-core.h>

struct ZenCompositor;

struct ZenLayerSurface {
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer;
    struct ZenCompositor *compositor;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener new_popup;

    struct wl_list link;  /* ZenCompositor.layer_surfaces */
};

/* Initialize layer shell protocol. Returns 0 on success. */
int zen_layer_init(struct ZenCompositor *compositor);

/* Destroy layer shell state. */
void zen_layer_destroy(struct ZenCompositor *compositor);

#endif /* ZEN_COMPOSITOR_LAYER_H */
