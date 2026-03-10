/*
 * Zen OS Compositor — XDG Shell + Toplevel Lifecycle
 *
 * Manages xdg_shell protocol support: toplevel creation, focus,
 * resize, maximize, fullscreen, and destruction.
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "zen/compositor.h"
#include "zen/xdg.h"

/* ── Forward declarations for listener callbacks ─────────────────────────── */

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data);

/* ── Focus Management ────────────────────────────────────────────────────── */

struct ZenToplevel *zen_xdg_get_focused(struct ZenCompositor *compositor) {
    if (!compositor) {
        return NULL;
    }
    return compositor->focused_toplevel;
}

void zen_xdg_focus_toplevel(struct ZenCompositor *compositor,
                            struct ZenToplevel *toplevel) {
    if (!compositor) {
        return;
    }

    struct ZenToplevel *prev = compositor->focused_toplevel;
    if (prev == toplevel) {
        return;
    }

    /* Clear focus on the previously focused toplevel. */
    if (prev && prev->xdg_toplevel) {
        wlr_xdg_toplevel_set_activated(prev->xdg_toplevel, false);
    }

    compositor->focused_toplevel = toplevel;

    if (!toplevel) {
        /* No toplevel to focus — clear keyboard focus. */
        struct wlr_seat *seat = compositor->seat;
        if (seat) {
            wlr_seat_keyboard_notify_clear_focus(seat);
        }
        return;
    }

    /* Move to front of MRU list. */
    wl_list_remove(&toplevel->link);
    wl_list_insert(&compositor->toplevels, &toplevel->link);

    /* Activate the toplevel and send keyboard enter. */
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    struct wlr_seat *seat = compositor->seat;
    if (seat) {
        struct wlr_surface *surface =
            toplevel->xdg_toplevel->base->surface;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(seat, surface,
                keyboard->keycodes, keyboard->num_keycodes,
                &keyboard->modifiers);
        }
    }

    /* Raise the scene node to the top. */
    if (toplevel->scene_tree) {
        wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    }
}

/* ── XDG Shell Init / Destroy ────────────────────────────────────────────── */

int zen_xdg_init(struct ZenCompositor *compositor) {
    int ret = -1;

    if (!compositor || !compositor->wl_display) {
        wlr_log(WLR_ERROR, "%s", "zen_xdg_init: invalid compositor");
        goto cleanup;
    }

    wl_list_init(&compositor->toplevels);
    compositor->focused_toplevel = NULL;

    compositor->xdg_shell = wlr_xdg_shell_create(compositor->wl_display, 3);
    if (!compositor->xdg_shell) {
        wlr_log(WLR_ERROR, "%s", "Failed to create xdg_shell");
        goto cleanup;
    }

    compositor->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&compositor->xdg_shell->events.new_toplevel,
                  &compositor->new_xdg_toplevel);

    wlr_log(WLR_INFO, "%s", "XDG shell initialized");
    ret = 0;

cleanup:
    return ret;
}

void zen_xdg_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Clean up any remaining toplevels. */
    struct ZenToplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &compositor->toplevels, link) {
        wl_list_remove(&toplevel->link);
        free(toplevel->title);
        free(toplevel->app_id);
        free(toplevel);
    }

    compositor->focused_toplevel = NULL;

    /* Remove the new_xdg_toplevel listener. */
    wl_list_remove(&compositor->new_xdg_toplevel.link);

    wlr_log(WLR_INFO, "%s", "XDG shell destroyed");
}

/* ── Per-Toplevel Listener Stubs (tasks 1.3.4–1.3.6) ────────────────────── */

static void handle_toplevel_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, map);

    /* Show the scene node. */
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);

    /* Focus-on-map: newly mapped toplevel gets focus (Property 6). */
    zen_xdg_focus_toplevel(toplevel->compositor, toplevel);

    wlr_log(WLR_INFO, "Toplevel mapped: %p (title=%s)",
            (void *)toplevel, toplevel->title ? toplevel->title : "(null)");
}

static void handle_toplevel_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, unmap);

    /* Hide the scene node. */
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);

    /* If this toplevel was focused, transfer focus to next MRU (Property 7). */
    if (toplevel->compositor->focused_toplevel == toplevel) {
        struct ZenToplevel *next = NULL;
        struct ZenToplevel *iter;
        wl_list_for_each(iter, &toplevel->compositor->toplevels, link) {
            if (iter != toplevel && iter->xdg_toplevel &&
                iter->xdg_toplevel->base->surface->mapped) {
                next = iter;
                break;
            }
        }
        zen_xdg_focus_toplevel(toplevel->compositor, next);
    }

    wlr_log(WLR_INFO, "Toplevel unmapped: %p", (void *)toplevel);
}

static void handle_toplevel_commit(struct wl_listener *listener, void *data) {
    (void)listener;
    (void)data;
    /* Surface commits are handled automatically by the wlroots scene graph.
     * No additional work needed here for now. */
}

static void handle_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, destroy);
    struct ZenCompositor *compositor = toplevel->compositor;

    wlr_log(WLR_INFO, "Toplevel destroyed: %p (title=%s)",
            (void *)toplevel, toplevel->title ? toplevel->title : "(null)");

    /* If this toplevel was focused, transfer focus to next MRU (Property 7). */
    if (compositor->focused_toplevel == toplevel) {
        struct ZenToplevel *next = NULL;
        struct ZenToplevel *iter;
        wl_list_for_each(iter, &compositor->toplevels, link) {
            if (iter != toplevel) {
                next = iter;
                break;
            }
        }
        zen_xdg_focus_toplevel(compositor, next);
    }

    /* Remove all per-toplevel listeners. */
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);
    wl_list_remove(&toplevel->set_title.link);
    wl_list_remove(&toplevel->set_app_id.link);

    /* Remove from toplevels list (Property 1: membership invariant). */
    wl_list_remove(&toplevel->link);

    /* Free owned strings and the toplevel itself. */
    free(toplevel->title);
    free(toplevel->app_id);
    free(toplevel);
}

static void handle_toplevel_request_move(struct wl_listener *listener,
                                         void *data) {
    (void)listener;
    (void)data;
    /* TODO(1.3.5): interactive move */
}

static void handle_toplevel_request_resize(struct wl_listener *listener,
                                           void *data) {
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, request_resize);
    struct wlr_xdg_toplevel_resize_event *event = data;

    /*
     * Interactive resize requires cursor grab logic (Sub-Phase 1.4).
     * For now, acknowledge the resize request by setting the toplevel
     * size to the event's requested dimensions if provided, or leave
     * unchanged. The actual interactive drag-resize will be wired once
     * input routing is in place.
     */
    (void)event;
    (void)toplevel;
    /* TODO: begin interactive resize via cursor grab in 1.4 */
}

static void handle_toplevel_request_maximize(struct wl_listener *listener,
                                             void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, request_maximize);
    struct wlr_xdg_toplevel *xdg_tl = toplevel->xdg_toplevel;
    struct ZenCompositor *compositor = toplevel->compositor;

    if (!xdg_tl || !compositor->output_layout) {
        return;
    }

    bool maximized = !xdg_tl->current.maximized;

    if (maximized) {
        /* Get the output this toplevel is on (or the first output).
         * wlroots 0.19 removed wlr_surface.output; determine output from
         * the scene node position via the output layout instead. */
        struct wlr_output *output = NULL;
        struct wlr_box box;

        int sx = toplevel->scene_tree->node.x;
        int sy = toplevel->scene_tree->node.y;
        output = wlr_output_layout_output_at(compositor->output_layout,
                                             sx, sy);

        if (!output) {
            /* Fall back to first output in layout. */
            struct wlr_output_layout_output *lo;
            wl_list_for_each(lo, &compositor->output_layout->outputs, link) {
                output = lo->output;
                break;
            }
        }

        if (!output) {
            return;
        }

        wlr_output_layout_get_box(compositor->output_layout, output, &box);

        /* TODO: subtract exclusive zones once layer shell (1.9) is in place. */
        wlr_xdg_toplevel_set_size(xdg_tl, box.width, box.height);
        wlr_xdg_toplevel_set_maximized(xdg_tl, true);

        wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                    box.x, box.y);
    } else {
        /* Unmaximize: let the client choose its own size. */
        wlr_xdg_toplevel_set_size(xdg_tl, 0, 0);
        wlr_xdg_toplevel_set_maximized(xdg_tl, false);
    }
}

static void handle_toplevel_request_fullscreen(struct wl_listener *listener,
                                               void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, request_fullscreen);
    struct wlr_xdg_toplevel *xdg_tl = toplevel->xdg_toplevel;
    struct ZenCompositor *compositor = toplevel->compositor;

    if (!xdg_tl || !compositor->output_layout) {
        return;
    }

    bool fullscreen = !xdg_tl->current.fullscreen;

    if (fullscreen) {
        /* Determine target output. Prefer the requested output, then the
         * output under the toplevel's scene node, then the first output. */
        struct wlr_output *output = xdg_tl->requested.fullscreen_output;
        struct wlr_box box;

        if (!output) {
            int sx = toplevel->scene_tree->node.x;
            int sy = toplevel->scene_tree->node.y;
            output = wlr_output_layout_output_at(compositor->output_layout,
                                                 sx, sy);
        }
        if (!output) {
            struct wlr_output_layout_output *lo;
            wl_list_for_each(lo, &compositor->output_layout->outputs, link) {
                output = lo->output;
                break;
            }
        }

        if (!output) {
            return;
        }

        wlr_output_layout_get_box(compositor->output_layout, output, &box);

        wlr_xdg_toplevel_set_size(xdg_tl, box.width, box.height);
        wlr_xdg_toplevel_set_fullscreen(xdg_tl, true);

        wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                    box.x, box.y);
    } else {
        /* Exit fullscreen: let the client choose its own size. */
        wlr_xdg_toplevel_set_size(xdg_tl, 0, 0);
        wlr_xdg_toplevel_set_fullscreen(xdg_tl, false);
    }
}

static void handle_toplevel_set_title(struct wl_listener *listener,
                                      void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, set_title);
    const char *new_title = toplevel->xdg_toplevel->title;

    free(toplevel->title);
    toplevel->title = new_title ? strdup(new_title) : NULL;
}

static void handle_toplevel_set_app_id(struct wl_listener *listener,
                                       void *data) {
    (void)data;
    struct ZenToplevel *toplevel =
        wl_container_of(listener, toplevel, set_app_id);
    const char *new_app_id = toplevel->xdg_toplevel->app_id;

    free(toplevel->app_id);
    toplevel->app_id = new_app_id ? strdup(new_app_id) : NULL;
}

/* ── New XDG Toplevel Handler ────────────────────────────────────────────── */

static void handle_new_xdg_toplevel(struct wl_listener *listener,
                                    void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    struct ZenToplevel *toplevel = NULL;

    toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        wlr_log(WLR_ERROR, "%s", "Failed to allocate ZenToplevel");
        goto cleanup;
    }

    toplevel->compositor = compositor;
    toplevel->xdg_toplevel = xdg_toplevel;

    /* Create a scene tree node for this toplevel's XDG surface. */
    toplevel->scene_tree = wlr_scene_xdg_surface_create(
        &compositor->scene->tree, xdg_toplevel->base);
    if (!toplevel->scene_tree) {
        wlr_log(WLR_ERROR, "%s", "Failed to create scene xdg surface");
        goto cleanup;
    }

    /* Store a back-pointer so we can retrieve the ZenToplevel from
     * scene graph hit-testing (used by input routing in 1.4). */
    toplevel->scene_tree->node.data = toplevel;

    /* Register per-toplevel listeners (stubs until tasks 1.3.4–1.3.6). */
    toplevel->map.notify = handle_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

    toplevel->unmap.notify = handle_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap,
                  &toplevel->unmap);

    toplevel->commit.notify = handle_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit,
                  &toplevel->commit);

    toplevel->destroy.notify = handle_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    toplevel->request_move.notify = handle_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move,
                  &toplevel->request_move);

    toplevel->request_resize.notify = handle_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize,
                  &toplevel->request_resize);

    toplevel->request_maximize.notify = handle_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
                  &toplevel->request_maximize);

    toplevel->request_fullscreen.notify = handle_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
                  &toplevel->request_fullscreen);

    toplevel->set_title.notify = handle_toplevel_set_title;
    wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);

    toplevel->set_app_id.notify = handle_toplevel_set_app_id;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);

    /* Prepend to MRU toplevels list. */
    wl_list_insert(&compositor->toplevels, &toplevel->link);

    wlr_log(WLR_INFO, "New XDG toplevel created: %p", (void *)toplevel);

    /* Focus the new toplevel. */
    zen_xdg_focus_toplevel(compositor, toplevel);
    return;

cleanup:
    free(toplevel);
}
