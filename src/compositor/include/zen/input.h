/*
 * Zen OS Compositor — Input Routing Module
 *
 * Manages the wlr_seat, keyboard, pointer, and touch input devices.
 * Routes events to the correct focused surface and handles cursor
 * rendering via wlr_cursor + wlr_xcursor_manager.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_INPUT_H
#define ZEN_COMPOSITOR_INPUT_H

#include <stdint.h>
#include <wayland-server-core.h>

struct ZenCompositor;

/*
 * ZenKeyboard — per-keyboard state.
 *
 * Allocated when a new keyboard device is attached to the seat.
 * Freed in the keyboard destroy listener.
 */
struct ZenKeyboard {
    struct wlr_keyboard      *wlr_keyboard;
    struct ZenCompositor     *compositor;

    struct wl_listener        key;
    struct wl_listener        modifiers;
    struct wl_listener        destroy;

    struct wl_list            link;  /* ZenCompositor.keyboards */
};

/* Initialize seat, cursor, and xcursor manager. Returns 0 on success. */
int zen_input_init(struct ZenCompositor *compositor);

/* Destroy input state. */
void zen_input_destroy(struct ZenCompositor *compositor);

/* Process pointer motion at the given layout coordinates.
 * Updates focus and delivers motion events. */
void zen_input_process_cursor_motion(struct ZenCompositor *compositor,
                                     uint32_t time_msec);

#endif /* ZEN_COMPOSITOR_INPUT_H */
