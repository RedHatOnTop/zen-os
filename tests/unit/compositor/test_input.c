/*
 * Zen OS — Property-Based Tests for Input Routing
 *
 * Tests Properties 8–10 from the Phase 1 design document.
 * Uses CMocka + zen_pbt.h for randomized input generation.
 *
 * These tests exercise the pure data-structure logic of input routing
 * (focus tracking, key delivery decisions, device attachment bookkeeping)
 * using lightweight mock structs that mirror the real wlroots types
 * without requiring a running Wayland display.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "zen_pbt.h"

/* ── Mock Wayland / wlroots types ────────────────────────────────────────── */

struct wl_list {
    struct wl_list *prev;
    struct wl_list *next;
};

static inline void wl_list_init(struct wl_list *list) {
    list->prev = list;
    list->next = list;
}

static inline void wl_list_insert(struct wl_list *list, struct wl_list *elm) {
    elm->prev = list;
    elm->next = list->next;
    list->next = elm;
    elm->next->prev = elm;
}

static inline void wl_list_remove(struct wl_list *elm) {
    elm->prev->next = elm->next;
    elm->next->prev = elm->prev;
    elm->prev = elm;
    elm->next = elm;
}

static inline int wl_list_empty(const struct wl_list *list) {
    return list->next == list;
}

static inline int wl_list_length(const struct wl_list *list) {
    int count = 0;
    const struct wl_list *e = list->next;
    while (e != list) {
        count++;
        e = e->next;
    }
    return count;
}

#define wl_container_of(ptr, sample, member)                               \
    (__typeof__(sample))((char *)(ptr) -                                   \
        __builtin_offsetof(__typeof__(*sample), member))

#define wl_list_for_each(pos, head, member)                                \
    for (pos = wl_container_of((head)->next, pos, member);                 \
         &pos->member != (head);                                           \
         pos = wl_container_of(pos->member.next, pos, member))

#define wl_list_for_each_safe(pos, tmp, head, member)                      \
    for (pos = wl_container_of((head)->next, pos, member),                 \
         tmp = wl_container_of(pos->member.next, tmp, member);             \
         &pos->member != (head);                                           \
         pos = tmp,                                                        \
         tmp = wl_container_of(pos->member.next, tmp, member))

/* ── Mock input device types ─────────────────────────────────────────────── */

enum MockInputDeviceType {
    MOCK_INPUT_KEYBOARD = 0,
    MOCK_INPUT_POINTER  = 1,
    MOCK_INPUT_TOUCH    = 2,
};

struct MockInputDevice {
    enum MockInputDeviceType type;
    bool attached;
    struct wl_list link;  /* MockCompositor.devices */
};

/* ── Mock scene graph node ───────────────────────────────────────────────── */

struct MockSceneNode {
    bool enabled;
    void *data;
    int x, y, width, height;  /* geometry in layout coordinates */
};

/* ── Mock toplevel ───────────────────────────────────────────────────────── */

struct MockToplevel {
    struct MockSceneNode scene_node;
    char *title;
    bool mapped;
    struct wl_list link;  /* MockCompositor.toplevels */

    /* Track events delivered to this toplevel for verification. */
    int keys_delivered;
    int pointer_enters;
    int pointer_motions;
    bool has_pointer_focus;
    bool has_keyboard_focus;
};

/* ── Mock cursor ─────────────────────────────────────────────────────────── */

struct MockCursor {
    double x, y;
    bool custom_image_set;
    const char *current_image;  /* "default" or "client-custom" */
};

/* ── Mock seat ───────────────────────────────────────────────────────────── */

struct MockSeat {
    struct MockToplevel *pointer_focused;
    struct MockToplevel *keyboard_focused;
    uint32_t capabilities;
};

/* ── Mock compositor ─────────────────────────────────────────────────────── */

struct MockCompositor {
    struct wl_list toplevels;
    struct wl_list devices;
    struct MockToplevel *focused_toplevel;
    struct MockSeat seat;
    struct MockCursor cursor;
    int keyboard_count;
    int pointer_count;
    int touch_count;
};

/* ── Helper: create a mock toplevel with geometry ────────────────────────── */

static struct MockToplevel *mock_toplevel_create(struct MockCompositor *comp,
                                                 int x, int y,
                                                 int w, int h) {
    struct MockToplevel *tl = calloc(1, sizeof(*tl));
    if (!tl) {
        return NULL;
    }
    tl->scene_node.enabled = true;
    tl->scene_node.data = tl;
    tl->scene_node.x = x;
    tl->scene_node.y = y;
    tl->scene_node.width = w;
    tl->scene_node.height = h;
    tl->mapped = true;
    wl_list_insert(&comp->toplevels, &tl->link);
    return tl;
}

static void mock_toplevel_destroy(struct MockCompositor *comp,
                                  struct MockToplevel *tl) {
    (void)comp;
    wl_list_remove(&tl->link);
    free(tl->title);
    free(tl);
}

/* ── Helper: focus a toplevel (mirrors zen_xdg_focus_toplevel) ───────────── */

static void mock_focus_toplevel(struct MockCompositor *comp,
                                struct MockToplevel *toplevel) {
    if (comp->focused_toplevel) {
        comp->focused_toplevel->has_keyboard_focus = false;
    }
    comp->focused_toplevel = toplevel;
    comp->seat.keyboard_focused = toplevel;
    if (toplevel) {
        toplevel->has_keyboard_focus = true;
        /* Move to front of MRU list. */
        wl_list_remove(&toplevel->link);
        wl_list_insert(&comp->toplevels, &toplevel->link);
    }
}

/* ── Helper: find toplevel at coordinates (mirrors wlr_scene_node_at) ──── */

static struct MockToplevel *mock_toplevel_at(struct MockCompositor *comp,
                                             double cx, double cy) {
    struct MockToplevel *tl;
    wl_list_for_each(tl, &comp->toplevels, link) {
        if (!tl->mapped || !tl->scene_node.enabled) {
            continue;
        }
        if (cx >= tl->scene_node.x &&
            cx < tl->scene_node.x + tl->scene_node.width &&
            cy >= tl->scene_node.y &&
            cy < tl->scene_node.y + tl->scene_node.height) {
            return tl;
        }
    }
    return NULL;
}

/*
 * Helper: process cursor motion (mirrors zen_input_process_cursor_motion).
 *
 * Finds the toplevel under the cursor and updates pointer focus.
 * If no toplevel is found, clears pointer focus and sets default cursor.
 */
static void mock_process_cursor_motion(struct MockCompositor *comp,
                                       uint32_t time_msec) {
    (void)time_msec;

    struct MockToplevel *tl = mock_toplevel_at(comp, comp->cursor.x,
                                               comp->cursor.y);

    /* Clear old pointer focus. */
    if (comp->seat.pointer_focused && comp->seat.pointer_focused != tl) {
        comp->seat.pointer_focused->has_pointer_focus = false;
    }

    if (!tl) {
        comp->seat.pointer_focused = NULL;
        comp->cursor.current_image = "default";
        comp->cursor.custom_image_set = false;
        return;
    }

    comp->seat.pointer_focused = tl;
    tl->has_pointer_focus = true;
    tl->pointer_enters++;
    tl->pointer_motions++;
}

/*
 * Helper: process cursor button (mirrors handle_cursor_button).
 *
 * On press, focuses the toplevel under the cursor.
 */
static void mock_process_cursor_button(struct MockCompositor *comp,
                                       bool pressed) {
    if (!pressed) {
        return;
    }

    struct MockToplevel *tl = mock_toplevel_at(comp, comp->cursor.x,
                                               comp->cursor.y);
    if (tl) {
        mock_focus_toplevel(comp, tl);
    }
}

/*
 * Helper: deliver key to focused surface (mirrors handle_keyboard_key).
 *
 * Returns true if the key was delivered to a focused surface.
 */
static bool mock_deliver_key(struct MockCompositor *comp,
                             uint32_t keycode, bool pressed) {
    (void)keycode;
    (void)pressed;

    struct MockToplevel *focused = comp->seat.keyboard_focused;
    if (!focused) {
        return false;
    }
    focused->keys_delivered++;
    return true;
}

/*
 * Helper: attach input device (mirrors handle_new_input).
 *
 * Attaches the device to the seat and updates capabilities.
 */
static void mock_attach_input_device(struct MockCompositor *comp,
                                     struct MockInputDevice *dev) {
    dev->attached = true;
    wl_list_insert(&comp->devices, &dev->link);

    switch (dev->type) {
    case MOCK_INPUT_KEYBOARD:
        comp->keyboard_count++;
        break;
    case MOCK_INPUT_POINTER:
        comp->pointer_count++;
        break;
    case MOCK_INPUT_TOUCH:
        comp->touch_count++;
        break;
    }

    /* Update seat capabilities. */
    uint32_t caps = 0;
    if (comp->pointer_count > 0 || comp->touch_count > 0) {
        caps |= 0x01;  /* WL_SEAT_CAPABILITY_POINTER */
    }
    if (comp->keyboard_count > 0) {
        caps |= 0x02;  /* WL_SEAT_CAPABILITY_KEYBOARD */
    }
    if (comp->touch_count > 0) {
        caps |= 0x04;  /* WL_SEAT_CAPABILITY_TOUCH */
    }
    comp->seat.capabilities = caps;
}

/*
 * Helper: simulate client setting a custom cursor image.
 * Mirrors handle_request_cursor — only honored if the requesting
 * client has pointer focus.
 */
static void mock_request_set_cursor(struct MockCompositor *comp,
                                    struct MockToplevel *requester,
                                    const char *image_name) {
    if (comp->seat.pointer_focused == requester) {
        comp->cursor.custom_image_set = true;
        comp->cursor.current_image = image_name;
    }
}

/* ── Test setup / teardown ───────────────────────────────────────────────── */

static int setup_compositor(void **state) {
    struct MockCompositor *comp = calloc(1, sizeof(*comp));
    if (!comp) {
        return -1;
    }
    wl_list_init(&comp->toplevels);
    wl_list_init(&comp->devices);
    comp->focused_toplevel = NULL;
    comp->seat.pointer_focused = NULL;
    comp->seat.keyboard_focused = NULL;
    comp->cursor.x = 0;
    comp->cursor.y = 0;
    comp->cursor.current_image = "default";
    comp->cursor.custom_image_set = false;
    *state = comp;
    return 0;
}

static int teardown_compositor(void **state) {
    struct MockCompositor *comp = *state;
    struct MockToplevel *tl, *tmp_tl;
    wl_list_for_each_safe(tl, tmp_tl, &comp->toplevels, link) {
        wl_list_remove(&tl->link);
        free(tl->title);
        free(tl);
    }
    struct MockInputDevice *dev, *tmp_dev;
    wl_list_for_each_safe(dev, tmp_dev, &comp->devices, link) {
        wl_list_remove(&dev->link);
        free(dev);
    }
    free(comp);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 8: Input focus follows input events
 *
 * For any mapped toplevel and any pointer-motion, click, or touch event
 * whose coordinates fall within that toplevel's geometry, the appropriate
 * focus (pointer focus for motion, keyboard focus for click) must be set
 * to that toplevel.
 *
 * Validates: Requirements 3.3, 3.4, 3.6
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 8: input focus follows input events */
static void test_property8_input_focus_follows_events(void **state) {
    struct MockCompositor *comp = *state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        /* Create 2–6 non-overlapping toplevels tiled horizontally. */
        int n = zen_pbt_rand_int(2, 6);
        struct MockToplevel **tls = calloc((size_t)n, sizeof(*tls));
        assert_non_null(tls);

        int tile_w = zen_pbt_rand_int(100, 400);
        int tile_h = zen_pbt_rand_int(100, 400);

        for (int j = 0; j < n; j++) {
            tls[j] = mock_toplevel_create(comp, j * tile_w, 0,
                                          tile_w, tile_h);
            assert_non_null(tls[j]);
        }

        /* Pick a random toplevel to target. */
        int target_idx = zen_pbt_rand_int(0, n - 1);
        struct MockToplevel *target = tls[target_idx];

        /* Move cursor into the target's geometry. */
        comp->cursor.x = target->scene_node.x +
                          zen_pbt_rand_int(0, tile_w - 1);
        comp->cursor.y = zen_pbt_rand_int(0, tile_h - 1);

        /* Process cursor motion — pointer focus must follow. */
        mock_process_cursor_motion(comp, 1000 + (uint32_t)i);
        assert_ptr_equal(comp->seat.pointer_focused, target);
        assert_true(target->has_pointer_focus);

        /* Click — keyboard focus must follow. */
        mock_process_cursor_button(comp, true);
        assert_ptr_equal(comp->seat.keyboard_focused, target);
        assert_ptr_equal(comp->focused_toplevel, target);
        assert_true(target->has_keyboard_focus);

        /* Move cursor to a different toplevel. */
        int other_idx = (target_idx + 1) % n;
        struct MockToplevel *other = tls[other_idx];
        comp->cursor.x = other->scene_node.x +
                          zen_pbt_rand_int(0, tile_w - 1);
        comp->cursor.y = zen_pbt_rand_int(0, tile_h - 1);

        mock_process_cursor_motion(comp, 2000 + (uint32_t)i);
        assert_ptr_equal(comp->seat.pointer_focused, other);
        assert_true(other->has_pointer_focus);
        /* Keyboard focus should NOT change on motion alone. */
        assert_ptr_equal(comp->seat.keyboard_focused, target);

        /* Click on the other toplevel — keyboard focus transfers. */
        mock_process_cursor_button(comp, true);
        assert_ptr_equal(comp->seat.keyboard_focused, other);
        assert_ptr_equal(comp->focused_toplevel, other);

        /* Move cursor to empty space (beyond all toplevels). */
        comp->cursor.x = n * tile_w + 50;
        comp->cursor.y = tile_h + 50;
        mock_process_cursor_motion(comp, 3000 + (uint32_t)i);
        assert_null(comp->seat.pointer_focused);
        assert_string_equal(comp->cursor.current_image, "default");

        /* Clean up. */
        for (int j = 0; j < n; j++) {
            mock_toplevel_destroy(comp, tls[j]);
        }
        free(tls);
        comp->focused_toplevel = NULL;
        comp->seat.keyboard_focused = NULL;
        comp->seat.pointer_focused = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 9: Key delivery to focused surface
 *
 * For any key event and any focused surface, the key event must be
 * delivered to the focused surface. If no surface is focused, the key
 * event must not be delivered.
 *
 * Validates: Requirements 3.5, 13.3
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 9: key delivery to focused surface */
static void test_property9_key_delivery_to_focused(void **state) {
    struct MockCompositor *comp = *state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int n = zen_pbt_rand_int(1, 6);
        struct MockToplevel **tls = calloc((size_t)n, sizeof(*tls));
        assert_non_null(tls);

        int tile_w = 200;
        int tile_h = 200;

        for (int j = 0; j < n; j++) {
            tls[j] = mock_toplevel_create(comp, j * tile_w, 0,
                                          tile_w, tile_h);
            assert_non_null(tls[j]);
        }

        /* Focus a random toplevel. */
        int focus_idx = zen_pbt_rand_int(0, n - 1);
        mock_focus_toplevel(comp, tls[focus_idx]);

        /* Generate a random key event. */
        uint32_t keycode = zen_pbt_rand_keysym();
        bool pressed = zen_pbt_rand_bool();

        /* Deliver key — must reach the focused surface. */
        int prev_count = tls[focus_idx]->keys_delivered;
        bool delivered = mock_deliver_key(comp, keycode, pressed);
        assert_true(delivered);
        assert_int_equal(tls[focus_idx]->keys_delivered, prev_count + 1);

        /* Verify no other toplevel received the key. */
        for (int j = 0; j < n; j++) {
            if (j == focus_idx) {
                continue;
            }
            assert_int_equal(tls[j]->keys_delivered, 0);
        }

        /* Unfocus all — key must not be delivered. */
        mock_focus_toplevel(comp, NULL);
        delivered = mock_deliver_key(comp, keycode, pressed);
        assert_false(delivered);

        /* Clean up. */
        for (int j = 0; j < n; j++) {
            mock_toplevel_destroy(comp, tls[j]);
        }
        free(tls);
        comp->focused_toplevel = NULL;
        comp->seat.keyboard_focused = NULL;
        comp->seat.pointer_focused = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Property 10: Input device attachment
 *
 * For any new input device detected by the backend, the device must be
 * attached to the compositor's seat and seat capabilities updated.
 *
 * Validates: Requirements 3.2
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Property 10: input device attachment */
static void test_property10_input_device_attachment(void **state) {
    struct MockCompositor *comp = *state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        /* Generate a random set of input devices. */
        int n_keyboards = zen_pbt_rand_int(0, 3);
        int n_pointers  = zen_pbt_rand_int(0, 3);
        int n_touch     = zen_pbt_rand_int(0, 3);
        int total = n_keyboards + n_pointers + n_touch;

        /* Ensure at least one device. */
        if (total == 0) {
            n_keyboards = 1;
            total = 1;
        }

        struct MockInputDevice **devs = calloc((size_t)total, sizeof(*devs));
        assert_non_null(devs);

        int idx = 0;
        for (int j = 0; j < n_keyboards; j++) {
            devs[idx] = calloc(1, sizeof(struct MockInputDevice));
            assert_non_null(devs[idx]);
            devs[idx]->type = MOCK_INPUT_KEYBOARD;
            mock_attach_input_device(comp, devs[idx]);
            idx++;
        }
        for (int j = 0; j < n_pointers; j++) {
            devs[idx] = calloc(1, sizeof(struct MockInputDevice));
            assert_non_null(devs[idx]);
            devs[idx]->type = MOCK_INPUT_POINTER;
            mock_attach_input_device(comp, devs[idx]);
            idx++;
        }
        for (int j = 0; j < n_touch; j++) {
            devs[idx] = calloc(1, sizeof(struct MockInputDevice));
            assert_non_null(devs[idx]);
            devs[idx]->type = MOCK_INPUT_TOUCH;
            mock_attach_input_device(comp, devs[idx]);
            idx++;
        }

        /* Verify: all devices are attached. */
        assert_int_equal(wl_list_length(&comp->devices), total);
        for (int j = 0; j < total; j++) {
            assert_true(devs[j]->attached);
        }

        /* Verify: device counts match. */
        assert_int_equal(comp->keyboard_count, n_keyboards);
        assert_int_equal(comp->pointer_count, n_pointers);
        assert_int_equal(comp->touch_count, n_touch);

        /* Verify: seat capabilities reflect attached devices. */
        if (n_pointers > 0 || n_touch > 0) {
            assert_true(comp->seat.capabilities & 0x01);  /* POINTER */
        }
        if (n_keyboards > 0) {
            assert_true(comp->seat.capabilities & 0x02);  /* KEYBOARD */
        }
        if (n_touch > 0) {
            assert_true(comp->seat.capabilities & 0x04);  /* TOUCH */
        }

        /* Clean up devices for next iteration. */
        struct MockInputDevice *dev, *tmp_dev;
        wl_list_for_each_safe(dev, tmp_dev, &comp->devices, link) {
            wl_list_remove(&dev->link);
            free(dev);
        }
        free(devs);
        comp->keyboard_count = 0;
        comp->pointer_count = 0;
        comp->touch_count = 0;
        comp->seat.capabilities = 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Additional: Cursor tracks position across outputs (Req 3.7)
 *
 * For any set of outputs arranged contiguously, the cursor position must
 * be trackable across the full layout area and correctly identify which
 * toplevel (if any) is under the cursor at each position.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Req 3.7: cursor tracks across outputs */
static void test_cursor_tracks_across_outputs(void **state) {
    struct MockCompositor *comp = *state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        /* Simulate 2 outputs side by side. */
        int out_w = zen_pbt_rand_int(800, 1920);
        int out_h = zen_pbt_rand_int(600, 1080);

        /* Place one toplevel on each "output". */
        int tl_w = zen_pbt_rand_int(100, out_w / 2);
        int tl_h = zen_pbt_rand_int(100, out_h / 2);

        struct MockToplevel *tl1 = mock_toplevel_create(
            comp, zen_pbt_rand_int(0, out_w - tl_w - 1),
            zen_pbt_rand_int(0, out_h - tl_h - 1), tl_w, tl_h);
        assert_non_null(tl1);

        struct MockToplevel *tl2 = mock_toplevel_create(
            comp, out_w + zen_pbt_rand_int(0, out_w - tl_w - 1),
            zen_pbt_rand_int(0, out_h - tl_h - 1), tl_w, tl_h);
        assert_non_null(tl2);

        /* Move cursor to first output's toplevel. */
        comp->cursor.x = tl1->scene_node.x + tl_w / 2;
        comp->cursor.y = tl1->scene_node.y + tl_h / 2;
        mock_process_cursor_motion(comp, 1000);
        assert_ptr_equal(comp->seat.pointer_focused, tl1);

        /* Move cursor across to second output's toplevel. */
        comp->cursor.x = tl2->scene_node.x + tl_w / 2;
        comp->cursor.y = tl2->scene_node.y + tl_h / 2;
        mock_process_cursor_motion(comp, 2000);
        assert_ptr_equal(comp->seat.pointer_focused, tl2);

        /* Cursor position must reflect the second output coordinates. */
        assert_true(comp->cursor.x >= out_w);

        /* Clean up. */
        mock_toplevel_destroy(comp, tl1);
        mock_toplevel_destroy(comp, tl2);
        comp->seat.pointer_focused = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Additional: Client custom cursor image (Req 3.8)
 *
 * When the pointer enters a toplevel that sets a custom cursor image,
 * the compositor must display the client-requested cursor. Only the
 * client with pointer focus may set the cursor.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Feature: phase1-foundation, Req 3.8: client custom cursor image */
static void test_client_custom_cursor(void **state) {
    struct MockCompositor *comp = *state;

    for (int i = 0; i < ZEN_PBT_ITERATIONS; i++) {
        int tile_w = 200;
        int tile_h = 200;

        struct MockToplevel *tl1 = mock_toplevel_create(comp, 0, 0,
                                                        tile_w, tile_h);
        struct MockToplevel *tl2 = mock_toplevel_create(comp, tile_w, 0,
                                                        tile_w, tile_h);
        assert_non_null(tl1);
        assert_non_null(tl2);

        /* Move cursor over tl1 and give it pointer focus. */
        comp->cursor.x = tile_w / 2;
        comp->cursor.y = tile_h / 2;
        mock_process_cursor_motion(comp, 1000);
        assert_ptr_equal(comp->seat.pointer_focused, tl1);

        /* tl1 requests a custom cursor — should be honored. */
        mock_request_set_cursor(comp, tl1, "client-custom");
        assert_true(comp->cursor.custom_image_set);
        assert_string_equal(comp->cursor.current_image, "client-custom");

        /* tl2 (not focused) requests a cursor — must be rejected. */
        mock_request_set_cursor(comp, tl2, "evil-cursor");
        /* Cursor should still show tl1's custom image. */
        assert_string_equal(comp->cursor.current_image, "client-custom");

        /* Move cursor to empty space — default cursor restored. */
        comp->cursor.x = tile_w * 3;
        comp->cursor.y = tile_h * 3;
        mock_process_cursor_motion(comp, 2000);
        assert_null(comp->seat.pointer_focused);
        assert_string_equal(comp->cursor.current_image, "default");
        assert_false(comp->cursor.custom_image_set);

        /* Move cursor over tl2 — tl2 can now set cursor. */
        comp->cursor.x = tile_w + tile_w / 2;
        comp->cursor.y = tile_h / 2;
        mock_process_cursor_motion(comp, 3000);
        assert_ptr_equal(comp->seat.pointer_focused, tl2);

        mock_request_set_cursor(comp, tl2, "tl2-cursor");
        assert_true(comp->cursor.custom_image_set);
        assert_string_equal(comp->cursor.current_image, "tl2-cursor");

        /* Clean up. */
        mock_toplevel_destroy(comp, tl1);
        mock_toplevel_destroy(comp, tl2);
        comp->seat.pointer_focused = NULL;
        comp->seat.keyboard_focused = NULL;
        comp->focused_toplevel = NULL;
        comp->cursor.custom_image_set = false;
        comp->cursor.current_image = "default";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — register and run all property tests
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    zen_pbt_seed();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_property8_input_focus_follows_events,
            setup_compositor, teardown_compositor),
        cmocka_unit_test_setup_teardown(
            test_property9_key_delivery_to_focused,
            setup_compositor, teardown_compositor),
        cmocka_unit_test_setup_teardown(
            test_property10_input_device_attachment,
            setup_compositor, teardown_compositor),
        cmocka_unit_test_setup_teardown(
            test_cursor_tracks_across_outputs,
            setup_compositor, teardown_compositor),
        cmocka_unit_test_setup_teardown(
            test_client_custom_cursor,
            setup_compositor, teardown_compositor),
    };

    return cmocka_run_group_tests_name("input-routing-properties",
                                       tests, NULL, NULL);
}
