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

#include <stdbool.h>
#include <wayland-server-core.h>

/* Forward declarations — we do not expose wlroots headers here. */
struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_scene;
struct wlr_scene_tree;
struct wlr_scene_output_layout;
struct wlr_output_layout;

/* Tier A forward declarations */
struct wlr_compositor;
struct wlr_subcompositor;
struct wlr_xdg_shell;
struct wlr_seat;
struct wlr_cursor;
struct wlr_xcursor_manager;

/* Tier B forward declarations */
struct wlr_layer_shell_v1;

/* Tier C forward declarations */
typedef struct sd_bus sd_bus;
typedef struct sd_bus_slot sd_bus_slot;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_v1;

/* Tier D forward declarations */
struct wlr_xwayland;

/* Module struct forward declarations */
struct ZenToplevel;

/*
 * ZenCompositor — top-level compositor state.
 *
 * Owns the Wayland display, wlroots backend, SceneFX renderer,
 * scene graph, and output layout.  All resources are allocated in
 * zen_compositor_create() and freed in zen_compositor_destroy().
 */
struct ZenCompositor {
    /* ── Existing fields (Sub-Phase 1.1) ─────────────────────────── */
    struct wl_display            *wl_display;
    struct wlr_backend           *backend;
    struct wlr_renderer          *renderer;          /* fx_renderer from SceneFX */
    struct wlr_allocator         *allocator;
    struct wlr_scene             *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout     *output_layout;
    struct wl_listener            new_output;
    float                         clear_color[4];    /* RGBA background color */

    /* ── Tier A: Core Window Management ──────────────────────────── */
    struct wlr_compositor        *wlr_comp;          /* wl_compositor global */
    struct wlr_subcompositor     *wlr_subcomp;       /* NULL — wlroots 0.17+ creates automatically */
    struct wlr_xdg_shell        *xdg_shell;
    struct wl_list                toplevels;          /* ZenToplevel.link */
    struct ZenToplevel           *focused_toplevel;

    struct wlr_seat              *seat;
    struct wlr_cursor            *cursor;
    struct wlr_xcursor_manager   *xcursor_mgr;
    struct wl_list                keyboards;          /* ZenKeyboard.link */
    struct wl_listener            new_input;
    struct wl_listener            cursor_motion;
    struct wl_listener            cursor_motion_absolute;
    struct wl_listener            cursor_button;
    struct wl_listener            cursor_axis;
    struct wl_listener            cursor_frame;
    struct wl_listener            new_xdg_toplevel;
    struct wl_listener            request_cursor;
    struct wl_listener            request_set_selection;

    /* ── Tier B: Shell Rendering Foundation ───────────────────────── */
    struct wlr_scene_tree        *shell_overlay_tree; /* for Cairo overlays */
    struct wlr_scene_tree        *wallpaper_tree;     /* bottom-most layer */
    struct wlr_layer_shell_v1    *layer_shell;
    struct wl_list                layer_surfaces;      /* ZenLayerSurface.link */
    struct wl_listener            new_layer_surface;
    struct wl_list                keybindings;         /* ZenKeybinding.link */
    bool                          dark_mode;

    /* ── Tier C: Session and Authentication ───────────────────────── */
    sd_bus                       *bus;
    sd_bus_slot                  *dbus_slot;
    struct wl_event_source       *dbus_event_source;
    struct wlr_session_lock_manager_v1 *lock_mgr;
    struct wlr_session_lock_v1   *active_lock;
    bool                          locked;

    /* ── Tier D: Extended Features ───────────────────────────────── */
    struct wlr_xwayland          *xwayland;           /* NULL if disabled */
    struct wl_listener            xwayland_ready;
    struct wl_listener            new_xwayland_surface;
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
