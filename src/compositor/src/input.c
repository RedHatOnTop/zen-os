/*
 * Zen OS Compositor — Input Routing Module
 *
 * Manages the wlr_seat, keyboard, pointer, and touch input devices.
 * Routes events to the correct focused surface and handles cursor
 * rendering via wlr_cursor + wlr_xcursor_manager.
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "zen/compositor.h"
#include "zen/input.h"
#include "zen/xdg.h"

/* Default xcursor theme size. */
#define ZEN_XCURSOR_SIZE 24

/* Keyboard repeat: 25 keys/sec after 600 ms delay (standard desktop values). */
#define ZEN_KB_REPEAT_RATE  25
#define ZEN_KB_REPEAT_DELAY 600

/* ── Helper: find toplevel at scene coordinates ──────────────────────────── */

/*
 * Walk up the scene graph from a node to find the ZenToplevel that owns it.
 * Returns NULL if the node is not part of a toplevel's scene tree.
 * Used by cursor motion and click handlers (Sub-Phase 1.4.5/1.4.6).
 */
static struct ZenToplevel *toplevel_at_scene_node(struct wlr_scene_node *node) {
    while (node && !node->data) {
        node = &node->parent->node;
    }
    return node ? node->data : NULL;
}

/* ── Keyboard listeners ──────────────────────────────────────────────────── */

/*
 * Stub for zen_keybinds_handle_key() — always returns false.
 * Replaced by the real implementation in Sub-Phase 1.8 (keybinds.c).
 */
static bool zen_keybinds_handle_key_stub(struct ZenCompositor *compositor,
                                         uint32_t modifiers,
                                         xkb_keysym_t keysym) {
    (void)compositor;
    (void)modifiers;
    (void)keysym;
    return false;
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
    struct ZenKeyboard *keyboard =
        wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;
    struct ZenCompositor *compositor = keyboard->compositor;
    struct wlr_keyboard *wlr_kb = keyboard->wlr_keyboard;

    /* Translate libinput keycode to xkbcommon keysym. */
    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(wlr_kb->xkb_state, keycode, &syms);

    /*
     * Check keybindings first (only on key press, not release).
     * If any resolved keysym matches a binding, consume the event.
     */
    bool consumed = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_kb);
        for (int i = 0; i < nsyms; i++) {
            if (zen_keybinds_handle_key_stub(compositor, modifiers, syms[i])) {
                consumed = true;
                break;
            }
        }
    }

    if (!consumed) {
        wlr_seat_keyboard_notify_key(compositor->seat,
                                     event->time_msec,
                                     event->keycode,
                                     event->state);
    }
}

static void handle_keyboard_modifiers(struct wl_listener *listener,
                                      void *data) {
    (void)data;
    struct ZenKeyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);

    /* Forward modifier state to the seat so clients see Shift/Ctrl/etc. */
    wlr_seat_set_keyboard(keyboard->compositor->seat,
                          keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->compositor->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct ZenKeyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);

    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

/* ── Cursor motion listeners ──────────────────────────────────────────── */

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(compositor->cursor, &event->pointer->base,
                     event->delta_x, event->delta_y);
    zen_input_process_cursor_motion(compositor, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
                                          void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_cursor_warp_absolute(compositor->cursor, &event->pointer->base,
                              event->x, event->y);
    zen_input_process_cursor_motion(compositor, event->time_msec);
}

/* ── Cursor button listener ──────────────────────────────────────────── */

static void handle_cursor_button(struct wl_listener *listener, void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, cursor_button);
    struct wlr_pointer_button_event *event = data;

    /* On press, focus the toplevel under the cursor. */
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        double sx, sy;
        struct wlr_scene_node *node = wlr_scene_node_at(
            &compositor->scene->tree.node,
            compositor->cursor->x, compositor->cursor->y,
            &sx, &sy);

        struct ZenToplevel *toplevel = NULL;
        if (node) {
            toplevel = toplevel_at_scene_node(node);
        }

        if (toplevel) {
            zen_xdg_focus_toplevel(compositor, toplevel);
        }
    }

    /* Forward the button event to the focused client. */
    wlr_seat_pointer_notify_button(compositor->seat,
                                   event->time_msec,
                                   event->button,
                                   event->state);
}

/* ── Request cursor listener ─────────────────────────────────────────── */

static void handle_request_cursor(struct wl_listener *listener, void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    /*
     * Only honor the cursor request if it comes from the client that
     * currently has pointer focus.  This prevents unfocused clients
     * from hijacking the cursor image.
     */
    struct wlr_seat_client *focused =
        compositor->seat->pointer_state.focused_client;
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(compositor->cursor,
                               event->surface,
                               event->hotspot_x,
                               event->hotspot_y);
    }
}

/* ── new_input listener ──────────────────────────────────────────────────── */

static void handle_new_input(struct wl_listener *listener, void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct ZenKeyboard *keyboard = calloc(1, sizeof(*keyboard));
        if (!keyboard) {
            wlr_log(WLR_ERROR, "%s", "Failed to allocate ZenKeyboard");
            return;
        }

        keyboard->wlr_keyboard = wlr_keyboard_from_input_device(device);
        keyboard->compositor   = compositor;

        /* Set up xkb keymap with system defaults. */
        struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap  *keymap = NULL;

        if (ctx) {
            keymap = xkb_keymap_new_from_names(ctx, NULL,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
        }

        if (keymap) {
            wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
            xkb_keymap_unref(keymap);
        } else {
            wlr_log(WLR_ERROR, "%s", "Failed to create xkb keymap");
        }

        if (ctx) {
            xkb_context_unref(ctx);
        }

        /* Set repeat info: 25 keys/sec, 600 ms initial delay. */
        wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard,
                                     ZEN_KB_REPEAT_RATE,
                                     ZEN_KB_REPEAT_DELAY);

        /* Register listeners. */
        keyboard->key.notify       = handle_keyboard_key;
        keyboard->modifiers.notify = handle_keyboard_modifiers;
        keyboard->destroy.notify   = handle_keyboard_destroy;

        wl_signal_add(&keyboard->wlr_keyboard->events.key,
                      &keyboard->key);
        wl_signal_add(&keyboard->wlr_keyboard->events.modifiers,
                      &keyboard->modifiers);
        wl_signal_add(&device->events.destroy,
                      &keyboard->destroy);

        /* Make this the active keyboard on the seat. */
        wlr_seat_set_keyboard(compositor->seat, keyboard->wlr_keyboard);

        wl_list_insert(&compositor->keyboards, &keyboard->link);

        wlr_log(WLR_INFO, "%s", "Keyboard device attached");
        break;
    }

    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(compositor->cursor, device);
        wlr_log(WLR_INFO, "%s", "Pointer device attached");
        break;

    case WLR_INPUT_DEVICE_TOUCH:
        /* Touch events are routed via the seat; attach to cursor for
         * position tracking on single-touch displays. */
        wlr_cursor_attach_input_device(compositor->cursor, device);
        wlr_log(WLR_INFO, "%s", "Touch device attached");
        break;

    default:
        wlr_log(WLR_DEBUG, "Unhandled input device type: %d", device->type);
        break;
    }

    /* Update seat capabilities based on what devices are now attached. */
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&compositor->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(compositor->seat, caps);
}

/* ── zen_input_process_cursor_motion ─────────────────────────────────────── */

void zen_input_process_cursor_motion(struct ZenCompositor *compositor,
                                     uint32_t time_msec) {
    if (!compositor || !compositor->cursor || !compositor->seat) {
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = NULL;

    struct wlr_scene_node *node = wlr_scene_node_at(
        &compositor->scene->tree.node,
        compositor->cursor->x, compositor->cursor->y,
        &sx, &sy);

    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer =
            wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface =
            wlr_scene_surface_try_from_buffer(scene_buffer);
        if (scene_surface) {
            surface = scene_surface->surface;
        }
    }

    if (!surface) {
        /* No surface under cursor — set default cursor image. */
        wlr_cursor_set_xcursor(compositor->cursor,
                               compositor->xcursor_mgr, "default");
        wlr_seat_pointer_notify_clear_focus(compositor->seat);
        return;
    }

    /* Notify the seat of pointer motion over this surface. */
    wlr_seat_pointer_notify_enter(compositor->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(compositor->seat, time_msec, sx, sy);
}

/* ── Init / Destroy ──────────────────────────────────────────────────────── */

int zen_input_init(struct ZenCompositor *compositor) {
    int ret = -1;

    if (!compositor || !compositor->wl_display) {
        wlr_log(WLR_ERROR, "%s", "zen_input_init: invalid compositor");
        goto cleanup;
    }

    /* Initialize keyboards list. */
    wl_list_init(&compositor->keyboards);

    /* 1. Create seat */
    compositor->seat = wlr_seat_create(compositor->wl_display, "seat0");
    if (!compositor->seat) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_seat");
        goto cleanup;
    }

    /* 2. Create cursor */
    compositor->cursor = wlr_cursor_create();
    if (!compositor->cursor) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_cursor");
        goto cleanup;
    }

    wlr_cursor_attach_output_layout(compositor->cursor,
                                    compositor->output_layout);

    /* 3. Create xcursor manager and load default theme */
    compositor->xcursor_mgr = wlr_xcursor_manager_create(NULL,
                                                          ZEN_XCURSOR_SIZE);
    if (!compositor->xcursor_mgr) {
        wlr_log(WLR_ERROR, "%s", "Failed to create xcursor manager");
        goto cleanup;
    }

    /* 4. Register cursor motion listeners. */
    compositor->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&compositor->cursor->events.motion,
                  &compositor->cursor_motion);

    compositor->cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&compositor->cursor->events.motion_absolute,
                  &compositor->cursor_motion_absolute);

    compositor->cursor_button.notify = handle_cursor_button;
    wl_signal_add(&compositor->cursor->events.button,
                  &compositor->cursor_button);

    /* 5. Register request_cursor listener (client custom cursor images). */
    compositor->request_cursor.notify = handle_request_cursor;
    wl_signal_add(&compositor->seat->events.request_set_cursor,
                  &compositor->request_cursor);

    /* 6. Register new_input listener to handle device hotplug. */
    compositor->new_input.notify = handle_new_input;
    wl_signal_add(&compositor->backend->events.new_input,
                  &compositor->new_input);

    wlr_log(WLR_INFO, "%s", "Input module initialized (seat, cursor, xcursor)");
    ret = 0;

cleanup:
    if (ret != 0) {
        zen_input_destroy(compositor);
    }
    return ret;
}

void zen_input_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Free all tracked keyboards. */
    struct ZenKeyboard *kb, *tmp;
    wl_list_for_each_safe(kb, tmp, &compositor->keyboards, link) {
        wl_list_remove(&kb->key.link);
        wl_list_remove(&kb->modifiers.link);
        wl_list_remove(&kb->destroy.link);
        wl_list_remove(&kb->link);
        free(kb);
    }

    if (compositor->xcursor_mgr) {
        wlr_xcursor_manager_destroy(compositor->xcursor_mgr);
        compositor->xcursor_mgr = NULL;
    }

    if (compositor->cursor) {
        wlr_cursor_destroy(compositor->cursor);
        compositor->cursor = NULL;
    }

    /* wlr_seat is destroyed automatically when wl_display is destroyed,
     * but we NULL the pointer to prevent double-free in partial cleanup. */
    compositor->seat = NULL;

    wlr_log(WLR_INFO, "%s", "Input module destroyed");
}
