/*
 * Zen OS Compositor — Core Data Structures & Lifecycle API
 *
 * The compositor is a wlroots-based Wayland compositor using SceneFX
 * for GPU-accelerated rendering effects (blur, shadows, rounded corners).
 * The desktop shell is integrated in-process (Option A architecture).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_COMPOSITOR_H
#define ZEN_COMPOSITOR_COMPOSITOR_H

#include <wayland-server-core.h>

/* Forward declarations — we do not expose wlroots headers here. */
struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_scene;
struct wlr_scene_output_layout;
struct wlr_output_layout;

/*
 * ZenCompositor — top-level compositor state.
 *
 * Owns the Wayland display, wlroots backend, SceneFX renderer,
 * scene graph, and output layout.  All resources are allocated in
 * zen_compositor_create() and freed in zen_compositor_destroy().
 */
struct ZenCompositor {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;          /* fx_renderer from SceneFX */
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout *output_layout;

    struct wl_listener new_output;
    struct wl_listener new_input;

    float clear_color[4];                   /* RGBA background color */
};

/*
 * ZenOutput — per-output state, allocated when a new output is detected.
 */
struct ZenOutput {
    struct wlr_output *wlr_output;
    struct ZenCompositor *compositor;
    struct wlr_scene_output *scene_output;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/*
 * Create and initialize the compositor.
 * Returns 0 on success, -1 on failure.
 * On failure, all partially-allocated resources are cleaned up.
 */
int zen_compositor_create(struct ZenCompositor *compositor);

/*
 * Run the compositor event loop.  Blocks until the display is terminated.
 */
void zen_compositor_run(struct ZenCompositor *compositor);

/*
 * Destroy the compositor, releasing all resources.
 * Safe to call on a partially-initialized struct (goto cleanup pattern).
 */
void zen_compositor_destroy(struct ZenCompositor *compositor);

#endif /* ZEN_COMPOSITOR_COMPOSITOR_H */
