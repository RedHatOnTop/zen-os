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
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

#include "zen/compositor.h"
#include "zen/input.h"
#include "zen/keybinds.h"
#include "zen/lock.h"
#include "zen/xdg.h"

/* Forward declaration for lock key handler (defined in lock.c). */
bool zen_lock_handle_key(struct ZenCompositor *compositor,
                          uint32_t keycode_xkb,
                          uint32_t keysym,
                          bool pressed);

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
     * While locked, route ALL key events to the lock handler.
     * No events are forwarded to clients while the screen is locked.
     */
    if (compositor->locked) {
        bool pressed = (event->state == WL_KEYBOARD_KEY_STATE_PRESSED);
        for (int i = 0; i < nsyms; i++) {
            zen_lock_handle_key(compositor, keycode, syms[i], pressed);
        }
        /* Reset idle timer on any key activity. */
        return;
    }

    /*
     * Check keybindings first (only on key press, not release).
     * If any resolved keysym matches a binding, consume the event.
     */
    bool consumed = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_kb);
        for (int i = 0; i < nsyms; i++) {
            if (zen_keybinds_handle_key(compositor, modifiers, syms[i])) {
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

    /* Release any active interactive move grab on button release. */
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
        compositor->grabbed_toplevel) {
        wlr_log(WLR_DEBUG, "Interactive move ended: toplevel=%p",
                (void *)compositor->grabbed_toplevel);
        compositor->grabbed_toplevel = NULL;
        compositor->grab_x           = 0.0;
        compositor->grab_y           = 0.0;
        compositor->grab_node_x      = 0;
        compositor->grab_node_y      = 0;
    }

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

    /*
     * If an interactive move grab is active, reposition the grabbed toplevel
     * by the delta from the grab origin.  wlr_scene_node_set_position() uses
     * global scene coordinates, so this works seamlessly across outputs.
     */
    if (compositor->grabbed_toplevel) {
        struct ZenToplevel *tl = compositor->grabbed_toplevel;
        if (tl->scene_tree) {
            double dx = compositor->cursor->x - compositor->grab_x;
            double dy = compositor->cursor->y - compositor->grab_y;
            int new_x = compositor->grab_node_x + (int)dx;
            int new_y = compositor->grab_node_y + (int)dy;

            wlr_scene_node_set_position(&tl->scene_tree->node, new_x, new_y);

            /*
             * Detect output boundary crossing: compare toplevel center to
             * output layout regions.  In wlroots scene-graph compositors,
             * re-parenting is not required — global coordinates work across
             * outputs automatically.  We log the crossing for diagnostics.
             */
            struct wlr_xdg_toplevel *xdg_tl = tl->xdg_toplevel;
            if (xdg_tl) {
                int w = xdg_tl->base->current.geometry.width;
                int h = xdg_tl->base->current.geometry.height;
                double cx = new_x + w / 2.0;
                double cy = new_y + h / 2.0;
                struct wlr_output *dest =
                    wlr_output_layout_output_at(compositor->output_layout,
                                                cx, cy);
                if (dest) {
                    wlr_log(WLR_DEBUG,
                            "Toplevel center (%.0f,%.0f) on output: %s",
                            cx, cy, dest->name);
                }
            }
        }
        /* Do not update pointer focus while dragging. */
        return;
    }

    /*
     * Determine which output the cursor is currently on.
     *
     * wlr_cursor handles cross-output movement automatically when attached
     * to an output layout via wlr_cursor_attach_output_layout() — no special
     * boundary-crossing code is needed here.  We query the current output
     * for logging and to support future per-output cursor theming.
     */
    struct wlr_output *cursor_output = wlr_output_layout_output_at(
        compositor->output_layout,
        compositor->cursor->x,
        compositor->cursor->y);
    if (cursor_output) {
        wlr_log(WLR_DEBUG, "Cursor on output: %s (%.0f, %.0f)",
                cursor_output->name,
                compositor->cursor->x,
                compositor->cursor->y);
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

    /* Free all tracked keyboards (if list was initialized). */
    if (compositor->keyboards.next != NULL) {
        struct ZenKeyboard *kb, *tmp;
        wl_list_for_each_safe(kb, tmp, &compositor->keyboards, link) {
            wl_list_remove(&kb->key.link);
            wl_list_remove(&kb->modifiers.link);
            wl_list_remove(&kb->destroy.link);
            wl_list_remove(&kb->link);
            free(kb);
        }
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
