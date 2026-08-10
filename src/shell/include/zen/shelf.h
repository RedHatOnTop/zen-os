/*
 * Zen OS Shell — Shelf (Taskbar)
 *
 * Persistent bottom bar on the primary display containing pinned
 * application icons, running app indicators, and the system tray
 * (battery, Bluetooth, network, volume, storage).
 *
 * Rendered via Cairo + Pango; composited with SceneFX effects.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_SHELF_H
#define ZEN_SHELL_SHELF_H

#include <stdbool.h>

#include "zen/models.h"

/* Forward declarations */
struct ZenShell;
struct ZenCairoBuffer;
struct wl_event_source;
struct sd_bus_slot;
struct wlr_scene_shadow;

/* ── ZenTrayIndicator ────────────────────────────────────────────────────── */

/*
 * Tray indicator state for a single D-Bus service (battery, network, etc.).
 */
struct ZenTrayIndicator {
    bool  available;    /* D-Bus service reachable */
    char *label;        /* short text label, e.g. "85%" */
    int   icon_index;   /* index into tray icon atlas */
};

/* ── ZenAppEntry ─────────────────────────────────────────────────────────── */

/*
 * Per-pinned-app entry stored in the shelf's pinned_apps array.
 */
struct ZenAppEntry {
    char *app_id;
    char *name;
    char *icon_path;
    char *exec_cmd;
    bool  is_running;
};

/* ── ZenShelf ────────────────────────────────────────────────────────────── */

/*
 * Full shelf state. Owns all Cairo surfaces and tray indicator data.
 */
struct ZenShelf {
    struct ZenShell       *shell;
    struct ZenCairoBuffer *bar_buffer;          /* main shelf bar */
    struct ZenCairoBuffer *context_menu_buf;    /* popup, NULL when hidden */
    struct wlr_scene_shadow *shadow;            /* SceneFX drop shadow node */
    ZenShelfConfig        *config;

    struct ZenAppEntry    *pinned_apps;         /* heap-allocated array */
    int                    pinned_count;

    struct ZenTrayIndicator battery;
    struct ZenTrayIndicator bluetooth;
    struct ZenTrayIndicator network;
    struct ZenTrayIndicator volume;
    struct ZenTrayIndicator storage;

    char                   clock_text[6];       /* "HH:MM\0" */
    struct wl_event_source *clock_timer;
    struct wl_event_source *auto_hide_timer;    /* 3s idle timer for auto-hide */

    /* D-Bus subscription slots for system tray indicators */
    struct sd_bus_slot     *upower_slot;        /* UPower PropertiesChanged */
    struct sd_bus_slot     *bluez_slot;         /* BlueZ PropertiesChanged */
    struct sd_bus_slot     *network_slot;       /* NetworkManager StateChanged */
    struct sd_bus_slot     *udisks_slot;        /* UDisks2 InterfacesAdded/Removed */
    struct sd_bus_slot     *name_owner_slot;    /* NameOwnerChanged for retry */
    struct wl_event_source *volume_timer;       /* 1s poll for PipeWire volume */

    int  x, y, width, height;
    int  context_menu_x, context_menu_y; /* screen coords of context menu top-left */
    bool auto_hide_active;
    bool context_menu_visible;

    /* Drag-to-pin state */
    bool  drag_active;   /* true while a drag is in progress */
    char *drag_app_id;   /* app_id being dragged, NULL if none */
};

/* ── API ─────────────────────────────────────────────────────────────────── */

/*
 * Initialize the shelf, binding it to the parent shell.
 * Returns 0 on success, -1 on failure.
 */
int zen_shelf_init(struct ZenShelf *shelf, struct ZenShell *shell);

/*
 * Destroy the shelf, releasing all Cairo surfaces and resources.
 */
void zen_shelf_destroy(struct ZenShelf *shelf);

/*
 * Trigger a full repaint of the shelf bar buffer.
 */
void zen_shelf_repaint(struct ZenShelf *shelf);

/*
 * Pin an application to the shelf by app_id.
 * Returns 0 on success, -1 if app_id is invalid or already pinned.
 */
int zen_shelf_pin_app(struct ZenShelf *shelf, const char *app_id);

/*
 * Unpin an application from the shelf.
 * Returns 0 on success, -1 if app_id is not pinned.
 */
int zen_shelf_unpin_app(struct ZenShelf *shelf, const char *app_id);

/*
 * Apply configuration: position string ("bottom", "top", "left", "right"),
 * auto-hide flag, and icon size in pixels.
 * Returns 0 on success, -1 on invalid arguments.
 */
int zen_shelf_apply_config(struct ZenShelf *shelf, const char *position,
                           bool auto_hide, int icon_size);

/*
 * Update the is_running flag for a pinned app entry.
 */
void zen_shelf_update_running(struct ZenShelf *shelf, const char *app_id,
                              bool running);

/*
 * Handle a primary (left) click at shelf-relative coordinates.
 */
void zen_shelf_handle_click(struct ZenShelf *shelf, int x, int y);

/*
 * Handle a secondary (right) click at shelf-relative coordinates.
 */
void zen_shelf_handle_right_click(struct ZenShelf *shelf, int x, int y);

/*
 * Notify the shelf of pointer motion at absolute screen coordinates (x, y).
 * When auto-hide is enabled:
 *   - If the pointer is within 2px of the shelf edge, cancel any pending
 *     hide timer and slide the shelf back on-screen.
 *   - Otherwise, arm a 3-second timer to slide the shelf off-screen.
 * Has no effect when auto-hide is disabled.
 */
void zen_shelf_handle_pointer_motion(struct ZenShelf *shelf, int x, int y);

/*
 * Begin a drag operation with the given app_id.
 * Stores the app_id so zen_shelf_drag_end() can pin it if dropped on the shelf.
 */
void zen_shelf_drag_begin(struct ZenShelf *shelf, const char *app_id);

/*
 * End a drag operation at absolute screen coordinates (x, y).
 * If (x, y) falls within the shelf bounds, pins drag_app_id via
 * zen_shelf_pin_app() and persists to shelf.json.
 * Clears drag state regardless of whether the drop landed on the shelf.
 */
void zen_shelf_drag_end(struct ZenShelf *shelf, int x, int y);

/*
 * Cancel an in-progress drag without pinning.
 * Clears drag state.
 */
void zen_shelf_drag_cancel(struct ZenShelf *shelf);

/*
 * Show the context menu popup.
 */
void zen_shelf_show_context_menu(struct ZenShelf *shelf);

/*
 * Hide the context menu popup and free its buffer.
 */
void zen_shelf_hide_context_menu(struct ZenShelf *shelf);

#endif /* ZEN_SHELL_SHELF_H */
