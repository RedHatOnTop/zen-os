/*
 * Zen OS Compositor — XDG Shell + Toplevel Lifecycle
 *
 * Manages xdg_shell protocol support: toplevel creation, focus,
 * resize, maximize, fullscreen, and destruction.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_XDG_H
#define ZEN_COMPOSITOR_XDG_H

#include <wayland-server-core.h>

struct ZenCompositor;
struct wlr_xdg_toplevel;
struct wlr_scene_tree;

struct ZenToplevel {
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree   *scene_tree;
    struct ZenCompositor    *compositor;

    char *title;
    char *app_id;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener set_title;
    struct wl_listener set_app_id;

    struct wl_list link;  /* ZenCompositor.toplevels */
};

/* Initialize XDG shell protocol support. Returns 0 on success. */
int zen_xdg_init(struct ZenCompositor *compositor);

/* Destroy XDG shell state. */
void zen_xdg_destroy(struct ZenCompositor *compositor);

/* Get the currently focused toplevel, or NULL. */
struct ZenToplevel *zen_xdg_get_focused(struct ZenCompositor *compositor);

/* Focus a specific toplevel. Sends keyboard enter/leave as needed. */
void zen_xdg_focus_toplevel(struct ZenCompositor *compositor,
                            struct ZenToplevel *toplevel);

#endif /* ZEN_COMPOSITOR_XDG_H */
