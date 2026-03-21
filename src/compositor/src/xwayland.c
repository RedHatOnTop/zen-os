/*
 * Zen OS Compositor — XWayland Bridge (Optional)
 *
 * Provides XWayland integration for running legacy X11 applications.
 * Only compiled when -Denable_xwayland=true is passed to meson.
 *
 * Architecture:
 *   - zen_xwayland_init()    — creates wlr_xwayland, registers listeners
 *   - zen_xwayland_destroy() — tears down XWayland state
 *   - xwayland_ready         — sets DISPLAY env var, configures seat
 *   - new_xwayland_surface   — allocates ZenXWaylandSurface, wires listeners
 *   - map/unmap/destroy      — mirrors XDG toplevel lifecycle
 *   - set_title              — free/strdup title into surface state
 *   - focus routing          — wlr_xwayland_surface_activate + seat notify
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <wlr/xwayland/xwayland.h>

#include "zen/compositor.h"
#include "zen/xwayland.h"

/* ── ZenXWaylandSurface — per-X11-window state ───────────────────────────── */

/*
 * Local struct mirroring the key fields of ZenToplevel but for X11 windows.
 * XWayland surfaces use wlr_xwayland_surface (not wlr_xdg_toplevel), so we
 * keep a separate struct rather than shoehorning into ZenToplevel.
 */
struct ZenXWaylandSurface {
    struct wlr_xwayland_surface *xwayland_surface;
    struct wlr_scene_surface    *scene_surface;   /* scene node for this X11 window */
    struct ZenCompositor        *compositor;

    char *title;

    /* Listeners on the wlr_xwayland_surface */
    struct wl_listener associate;   /* wlr_surface becomes valid */
    struct wl_listener dissociate;  /* wlr_surface becomes invalid */
    struct wl_listener destroy;
    struct wl_listener set_title;

    /* Listeners on the inner wlr_surface (registered after associate) */
    struct wl_listener map;
    struct wl_listener unmap;
};

/* ── Forward declarations ────────────────────────────────────────────────── */

static void handle_xwayland_ready(struct wl_listener *listener, void *data);
static void handle_new_xwayland_surface(struct wl_listener *listener,
                                        void *data);

/* ── Per-surface listener callbacks ─────────────────────────────────────── */

static void handle_xws_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenXWaylandSurface *xws =
        wl_container_of(listener, xws, map);

    if (xws->scene_surface) {
        wlr_scene_node_set_enabled(&xws->scene_surface->buffer->node, true);
    }

    /* Focus-on-map: activate the X11 surface and send keyboard enter. */
    wlr_xwayland_surface_activate(xws->xwayland_surface, true);

    struct wlr_seat *seat = xws->compositor->seat;
    if (seat && xws->xwayland_surface->surface) {
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(seat,
                xws->xwayland_surface->surface,
                keyboard->keycodes,
                keyboard->num_keycodes,
                &keyboard->modifiers);
        }
    }

    wlr_log(WLR_INFO, "XWayland surface mapped: %p (title=%s)",
            (void *)xws, xws->title ? xws->title : "(null)");
}

static void handle_xws_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenXWaylandSurface *xws =
        wl_container_of(listener, xws, unmap);

    if (xws->scene_surface) {
        wlr_scene_node_set_enabled(&xws->scene_surface->buffer->node, false);
    }

    /* Clear keyboard focus if this surface had it. */
    struct wlr_seat *seat = xws->compositor->seat;
    if (seat) {
        wlr_seat_keyboard_notify_clear_focus(seat);
    }

    wlr_log(WLR_INFO, "XWayland surface unmapped: %p", (void *)xws);
}

static void handle_xws_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenXWaylandSurface *xws =
        wl_container_of(listener, xws, associate);

    /*
     * The inner wlr_surface is now valid. Create the scene node and
     * register map/unmap listeners on the wlr_surface.
     */
    struct wlr_xwayland_surface *xsurface = xws->xwayland_surface;

    xws->scene_surface = wlr_scene_surface_create(
        &xws->compositor->scene->tree,
        xsurface->surface);

    if (!xws->scene_surface) {
        wlr_log(WLR_ERROR, "%s",
                "XWayland: failed to create scene surface node");
        return;
    }

    /* Store back-pointer for hit-testing. */
    xws->scene_surface->buffer->node.data = xws;

    xws->map.notify = handle_xws_map;
    wl_signal_add(&xsurface->surface->events.map, &xws->map);

    xws->unmap.notify = handle_xws_unmap;
    wl_signal_add(&xsurface->surface->events.unmap, &xws->unmap);

    wlr_log(WLR_DEBUG, "XWayland surface associated: %p", (void *)xws);
}

static void handle_xws_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenXWaylandSurface *xws =
        wl_container_of(listener, xws, dissociate);

    /* Remove map/unmap listeners — the wlr_surface is going away. */
    if (xws->map.link.next) {
        wl_list_remove(&xws->map.link);
        xws->map.link.next = NULL;
    }
    if (xws->unmap.link.next) {
        wl_list_remove(&xws->unmap.link);
        xws->unmap.link.next = NULL;
    }

    /* Destroy the scene node. */
    if (xws->scene_surface) {
        wlr_scene_node_destroy(&xws->scene_surface->buffer->node);
        xws->scene_surface = NULL;
    }

    wlr_log(WLR_DEBUG, "XWayland surface dissociated: %p", (void *)xws);
}

static void handle_xws_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenXWaylandSurface *xws =
        wl_container_of(listener, xws, set_title);
    const char *new_title = xws->xwayland_surface->title;

    free(xws->title);
    xws->title = new_title ? strdup(new_title) : NULL;

    wlr_log(WLR_DEBUG, "XWayland surface title set: %s",
            xws->title ? xws->title : "(null)");
}

static void handle_xws_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenXWaylandSurface *xws =
        wl_container_of(listener, xws, destroy);

    wlr_log(WLR_INFO, "XWayland surface destroyed: %p (title=%s)",
            (void *)xws, xws->title ? xws->title : "(null)");

    /* Remove all listeners. */
    wl_list_remove(&xws->associate.link);
    wl_list_remove(&xws->dissociate.link);
    wl_list_remove(&xws->destroy.link);
    wl_list_remove(&xws->set_title.link);

    /* map/unmap listeners are on the wlr_surface; only remove if still live. */
    if (xws->map.link.next) {
        wl_list_remove(&xws->map.link);
    }
    if (xws->unmap.link.next) {
        wl_list_remove(&xws->unmap.link);
    }

    /* Destroy scene node if still present (dissociate may have done it). */
    if (xws->scene_surface) {
        wlr_scene_node_destroy(&xws->scene_surface->buffer->node);
        xws->scene_surface = NULL;
    }

    free(xws->title);
    free(xws);
}

/* ── XWayland-level listener callbacks ──────────────────────────────────── */

static void handle_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, xwayland_ready);

    struct wlr_xwayland *xwayland = compositor->xwayland;
    if (!xwayland) {
        return;
    }

    /* Set the DISPLAY environment variable so X11 clients can connect. */
    if (xwayland->display_name) {
        setenv("DISPLAY", xwayland->display_name, 1);
        wlr_log(WLR_INFO, "XWayland ready: DISPLAY=%s",
                xwayland->display_name);
    }

    /* Configure the seat so XWayland can route input. */
    if (compositor->seat) {
        wlr_xwayland_set_seat(xwayland, compositor->seat);
    }
}

static void handle_new_xwayland_surface(struct wl_listener *listener,
                                        void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;
    struct ZenXWaylandSurface *xws = NULL;

    xws = calloc(1, sizeof(*xws));
    if (!xws) {
        wlr_log(WLR_ERROR, "%s",
                "XWayland: failed to allocate ZenXWaylandSurface");
        goto cleanup;
    }

    xws->compositor       = compositor;
    xws->xwayland_surface = xsurface;
    xws->scene_surface    = NULL;
    xws->title            = NULL;

    /* Initialise map/unmap link sentinels to NULL so destroy handler can
     * safely check whether they were ever registered. */
    xws->map.link.next   = NULL;
    xws->unmap.link.next = NULL;

    /* Copy initial title if already set. */
    if (xsurface->title) {
        xws->title = strdup(xsurface->title);
    }

    /* Register listeners on the wlr_xwayland_surface. */
    xws->associate.notify = handle_xws_associate;
    wl_signal_add(&xsurface->events.associate, &xws->associate);

    xws->dissociate.notify = handle_xws_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &xws->dissociate);

    xws->destroy.notify = handle_xws_destroy;
    wl_signal_add(&xsurface->events.destroy, &xws->destroy);

    xws->set_title.notify = handle_xws_set_title;
    wl_signal_add(&xsurface->events.set_title, &xws->set_title);

    wlr_log(WLR_INFO, "New XWayland surface: %p (title=%s)",
            (void *)xws, xws->title ? xws->title : "(null)");
    return;

cleanup:
    free(xws);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int zen_xwayland_init(struct ZenCompositor *compositor) {
    int ret = -1;

    if (!compositor || !compositor->wl_display || !compositor->wlr_comp) {
        wlr_log(WLR_ERROR, "%s", "zen_xwayland_init: invalid compositor");
        goto cleanup;
    }

    /*
     * Create the XWayland instance.
     * lazy=true: Xwayland process is only started when a client connects,
     * reducing startup overhead when no X11 apps are running.
     */
    compositor->xwayland = wlr_xwayland_create(
        compositor->wl_display,
        compositor->wlr_comp,
        true /* lazy */);

    if (!compositor->xwayland) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_xwayland");
        goto cleanup;
    }

    /* Register the ready listener (fires when Xwayland process is up). */
    compositor->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&compositor->xwayland->events.ready,
                  &compositor->xwayland_ready);

    /* Register the new_surface listener (fires for each new X11 window). */
    compositor->new_xwayland_surface.notify = handle_new_xwayland_surface;
    wl_signal_add(&compositor->xwayland->events.new_surface,
                  &compositor->new_xwayland_surface);

    wlr_log(WLR_INFO, "%s", "XWayland bridge initialized (lazy mode)");
    ret = 0;

cleanup:
    if (ret != 0) {
        zen_xwayland_destroy(compositor);
    }
    return ret;
}

void zen_xwayland_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    if (compositor->xwayland) {
        /* Remove our listeners before destroying to avoid dangling callbacks. */
        if (compositor->xwayland_ready.link.next) {
            wl_list_remove(&compositor->xwayland_ready.link);
            compositor->xwayland_ready.link.next = NULL;
        }
        if (compositor->new_xwayland_surface.link.next) {
            wl_list_remove(&compositor->new_xwayland_surface.link);
            compositor->new_xwayland_surface.link.next = NULL;
        }

        wlr_xwayland_destroy(compositor->xwayland);
        compositor->xwayland = NULL;
    }

    wlr_log(WLR_INFO, "%s", "XWayland bridge destroyed");
}
