/*
 * Zen OS Compositor — Screen Lock Module
 *
 * Implements the lock screen using wlr_session_lock_manager_v1.
 * Renders a Cairo-based lock UI (clock, avatar placeholder, password field).
 * Handles PAM authentication for unlock.
 * Registers idle timer (300s) and logind Lock signal for auto-lock.
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

#include "zen/compositor.h"
#include "zen/lock.h"
#include "zen/cairo_buffer.h"

/* Maximum password buffer length. */
#define ZEN_LOCK_PASS_MAX 255

/* Idle timeout in milliseconds (300 seconds). */
#define ZEN_LOCK_IDLE_TIMEOUT_MS 300000

/* Lock UI dimensions. */
#define ZEN_LOCK_AVATAR_SIZE   80
#define ZEN_LOCK_PASS_W       300
#define ZEN_LOCK_PASS_H        40

/*
 * ZenLockScreen — internal lock state.
 * Tracks the active wlr_session_lock_v1 and associated Cairo buffers.
 */
struct ZenLockScreen {
    struct wlr_session_lock_v1 *wlr_lock;
    struct ZenCairoBuffer      *lock_buffer;  /* single full-screen buffer */
    int                         width;
    int                         height;
    bool                        show_error;
};

/* Module-level state (one lock screen at a time). */
static struct ZenLockScreen  s_lock;
static char                  s_password[ZEN_LOCK_PASS_MAX + 1];
static int                   s_password_len;
static struct wl_event_source *s_idle_timer;
static struct wl_listener     s_new_lock_listener;

#ifdef HAVE_SYSTEMD
/* sd-bus slot for logind Lock signal. */
static sd_bus_slot *s_lock_signal_slot;
#endif

/* ── Lock UI rendering ───────────────────────────────────────────────────── */

static void render_lock_ui(struct ZenCompositor *compositor) {
    struct ZenLockScreen *ls = &s_lock;
    if (!ls->lock_buffer || ls->width <= 0 || ls->height <= 0) {
        return;
    }

    cairo_t *cr = ls->lock_buffer->cr;
    int w = ls->width;
    int h = ls->height;

    /* Background: dark semi-transparent overlay. */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_paint(cr);

    /* ── Clock ── */
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[16];
        strftime(time_str, sizeof(time_str), "%H:%M", tm_info);

        PangoLayout *layout = zen_cairo_buffer_get_pango(ls->lock_buffer);
        PangoFontDescription *font = pango_font_description_from_string("Sans Bold 48");
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);
        pango_layout_set_text(layout, time_str, -1);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);

        int cx = (w - tw) / 2;
        int cy = h / 4 - th / 2;

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_move_to(cr, cx, cy);
        pango_cairo_show_layout(cr, layout);
    }

    /* ── Avatar placeholder ── */
    {
        int ax = (w - ZEN_LOCK_AVATAR_SIZE) / 2;
        int ay = h / 2 - ZEN_LOCK_AVATAR_SIZE - 20;

        cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 1.0);
        cairo_rectangle(cr, ax, ay, ZEN_LOCK_AVATAR_SIZE, ZEN_LOCK_AVATAR_SIZE);
        cairo_fill(cr);
    }

    /* ── Password field ── */
    {
        int px = (w - ZEN_LOCK_PASS_W) / 2;
        int py = h / 2 + 10;
        double radius = 8.0;

        /* Rounded rect background. */
        cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.9);
        cairo_new_sub_path(cr);
        cairo_arc(cr, px + ZEN_LOCK_PASS_W - radius, py + radius,
                  radius, -M_PI / 2.0, 0.0);
        cairo_arc(cr, px + ZEN_LOCK_PASS_W - radius, py + ZEN_LOCK_PASS_H - radius,
                  radius, 0.0, M_PI / 2.0);
        cairo_arc(cr, px + radius, py + ZEN_LOCK_PASS_H - radius,
                  radius, M_PI / 2.0, M_PI);
        cairo_arc(cr, px + radius, py + radius,
                  radius, M_PI, 3.0 * M_PI / 2.0);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Masked password dots. */
        if (s_password_len > 0) {
            char masked[ZEN_LOCK_PASS_MAX + 1];
            memset(masked, '*', (size_t)s_password_len);
            masked[s_password_len] = '\0';

            PangoLayout *layout = zen_cairo_buffer_get_pango(ls->lock_buffer);
            PangoFontDescription *font =
                pango_font_description_from_string("Monospace 16");
            pango_layout_set_font_description(layout, font);
            pango_font_description_free(font);
            pango_layout_set_text(layout, masked, -1);

            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);

            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            cairo_move_to(cr, px + (ZEN_LOCK_PASS_W - tw) / 2,
                          py + (ZEN_LOCK_PASS_H - th) / 2);
            pango_cairo_show_layout(cr, layout);
        }
    }

    /* ── Error message ── */
    if (ls->show_error) {
        PangoLayout *layout = zen_cairo_buffer_get_pango(ls->lock_buffer);
        PangoFontDescription *font =
            pango_font_description_from_string("Sans 14");
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);
        pango_layout_set_text(layout, "Incorrect password", -1);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);

        int ex = (w - tw) / 2;
        int ey = h / 2 + ZEN_LOCK_PASS_H + 20;

        cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 1.0);
        cairo_move_to(cr, ex, ey);
        pango_cairo_show_layout(cr, layout);
    }

    zen_cairo_buffer_submit(ls->lock_buffer);
    (void)compositor;
}

/* ── PAM authentication ──────────────────────────────────────────────────── */

#ifdef HAVE_PAM

static const char *s_pam_password;

static int pam_conv_fn(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata_ptr) {
    (void)appdata_ptr;

    *resp = calloc((size_t)num_msg, sizeof(struct pam_response));
    if (!*resp) {
        return PAM_BUF_ERR;
    }

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            (*resp)[i].resp = strdup(s_pam_password ? s_pam_password : "");
        }
    }

    return PAM_SUCCESS;
}

static bool pam_authenticate_password(const char *password) {
    bool success = false;
    pam_handle_t *pamh = NULL;

    const char *username = getenv("USER");
    if (!username || username[0] == '\0') {
        username = getlogin();
    }
    if (!username || username[0] == '\0') {
        username = "root";
    }

    s_pam_password = password;

    struct pam_conv conv = {
        .conv        = pam_conv_fn,
        .appdata_ptr = NULL,
    };

    int ret = pam_start("login", username, &conv, &pamh);
    if (ret != PAM_SUCCESS) {
        wlr_log(WLR_ERROR, "pam_start failed: %s", pam_strerror(pamh, ret));
        goto cleanup;
    }

    ret = pam_authenticate(pamh, 0);
    if (ret == PAM_SUCCESS) {
        success = true;
    } else {
        wlr_log(WLR_INFO, "PAM authentication failed: %s",
                pam_strerror(pamh, ret));
    }

cleanup:
    if (pamh) {
        pam_end(pamh, ret);
    }
    s_pam_password = NULL;
    return success;
}

#else /* !HAVE_PAM */

static bool pam_authenticate_password(const char *password) {
    /* Without PAM, never unlock (safe default). */
    (void)password;
    wlr_log(WLR_INFO, "%s", "PAM not available — unlock disabled");
    return false;
}

#endif /* HAVE_PAM */

/* ── Deactivate (internal) ───────────────────────────────────────────────── */

static void zen_lock_deactivate(struct ZenCompositor *compositor) {
    if (!compositor->locked) {
        return;
    }

    /* Destroy lock buffer. */
    if (s_lock.lock_buffer) {
        zen_cairo_buffer_destroy(s_lock.lock_buffer);
        free(s_lock.lock_buffer);
        s_lock.lock_buffer = NULL;
    }

    /* Unlock the wlr session lock. */
    if (s_lock.wlr_lock) {
        wlr_session_lock_v1_destroy(s_lock.wlr_lock);
        s_lock.wlr_lock = NULL;
    }

    compositor->locked      = false;
    compositor->active_lock = NULL;

    memset(&s_lock, 0, sizeof(s_lock));
    memset(s_password, 0, sizeof(s_password));
    s_password_len = 0;

    /* Reset idle timer. */
    if (s_idle_timer) {
        wl_event_source_timer_update(s_idle_timer, ZEN_LOCK_IDLE_TIMEOUT_MS);
    }

    wlr_log(WLR_INFO, "%s", "Screen unlocked");
}

/* ── Keyboard input while locked ─────────────────────────────────────────── */

/*
 * Called from input.c handle_keyboard_key() when compositor->locked is true.
 * Returns true to consume the event (never forward to clients while locked).
 */
bool zen_lock_handle_key(struct ZenCompositor *compositor,
                          uint32_t keycode_xkb,
                          uint32_t keysym,
                          bool pressed) {
    if (!compositor->locked || !pressed) {
        return compositor->locked;
    }

    /* XKB_KEY_BackSpace = 0xff08 */
    if (keysym == 0xff08) {
        if (s_password_len > 0) {
            s_password[--s_password_len] = '\0';
            s_lock.show_error = false;
            render_lock_ui(compositor);
        }
        return true;
    }

    /* XKB_KEY_Return = 0xff0d, XKB_KEY_KP_Enter = 0xff8d */
    if (keysym == 0xff0d || keysym == 0xff8d) {
        if (pam_authenticate_password(s_password)) {
            zen_lock_deactivate(compositor);
        } else {
            s_lock.show_error = true;
            memset(s_password, 0, sizeof(s_password));
            s_password_len = 0;
            render_lock_ui(compositor);
        }
        return true;
    }

    /* XKB_KEY_Escape = 0xff1b */
    if (keysym == 0xff1b) {
        memset(s_password, 0, sizeof(s_password));
        s_password_len = 0;
        s_lock.show_error = false;
        render_lock_ui(compositor);
        return true;
    }

    /* Accumulate printable characters (keysym in Latin-1 printable range). */
    if (keysym >= 0x0020 && keysym <= 0x007e) {
        if (s_password_len < ZEN_LOCK_PASS_MAX) {
            s_password[s_password_len++] = (char)(keysym & 0xff);
            s_password[s_password_len]   = '\0';
            s_lock.show_error = false;
            render_lock_ui(compositor);
        }
        return true;
    }

    (void)keycode_xkb;
    /* Consume all other keys while locked. */
    return true;
}

/* ── Idle timer callback ─────────────────────────────────────────────────── */

static int idle_timeout_cb(void *data) {
    struct ZenCompositor *compositor = data;
    wlr_log(WLR_INFO, "%s", "Idle timeout — activating lock screen");
    zen_lock_activate(compositor);
    return 0;
}

/* ── sd-bus logind Lock signal ───────────────────────────────────────────── */

#ifdef HAVE_SYSTEMD
#include <systemd/sd-bus.h>

static int lock_signal_cb(sd_bus_message *msg, void *userdata,
                           sd_bus_error *error) {
    (void)msg;
    (void)error;
    struct ZenCompositor *compositor = userdata;
    wlr_log(WLR_INFO, "%s", "logind Lock signal received — activating lock");
    zen_lock_activate(compositor);
    return 0;
}
#endif

/* ── new_lock listener ───────────────────────────────────────────────────── */

static void handle_new_lock(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_session_lock_v1 *wlr_lock = data;

    wlr_log(WLR_INFO, "%s", "Session lock requested via protocol");
    wlr_session_lock_v1_send_locked(wlr_lock);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void zen_lock_activate(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Guard: no-op if already locked. */
    if (compositor->locked) {
        return;
    }

    compositor->locked = true;

    memset(&s_lock, 0, sizeof(s_lock));
    memset(s_password, 0, sizeof(s_password));
    s_password_len = 0;

    /* Determine primary output dimensions. */
    int out_w = 1920;
    int out_h = 1080;

    if (compositor->output_layout) {
        struct wlr_output_layout_output *lo;
        wl_list_for_each(lo, &compositor->output_layout->outputs, link) {
            if (lo->output && lo->output->width > 0) {
                out_w = lo->output->width;
                out_h = lo->output->height;
                break;
            }
        }
    }

    s_lock.width  = out_w;
    s_lock.height = out_h;

    /* Create full-screen Cairo buffer for lock UI. */
    s_lock.lock_buffer = calloc(1, sizeof(*s_lock.lock_buffer));
    if (!s_lock.lock_buffer) {
        wlr_log(WLR_ERROR, "%s", "zen_lock_activate: failed to alloc lock buffer");
        compositor->locked = false;
        return;
    }

    struct wlr_scene_tree *parent = compositor->shell_overlay_tree
                                  ? compositor->shell_overlay_tree
                                  : &compositor->scene->tree;

    if (zen_cairo_buffer_create(s_lock.lock_buffer,
                                compositor->renderer,
                                parent,
                                out_w, out_h) != 0) {
        wlr_log(WLR_ERROR, "%s", "zen_lock_activate: failed to create cairo buffer");
        free(s_lock.lock_buffer);
        s_lock.lock_buffer = NULL;
        compositor->locked = false;
        return;
    }

    /* Position at origin. */
    wlr_scene_node_set_position(&s_lock.lock_buffer->scene_buffer->node, 0, 0);

    render_lock_ui(compositor);

    wlr_log(WLR_INFO, "Lock screen activated (%dx%d)", out_w, out_h);
}

bool zen_lock_is_active(struct ZenCompositor *compositor) {
    if (!compositor) {
        return false;
    }
    return compositor->locked;
}

int zen_lock_init(struct ZenCompositor *compositor) {
    int ret = -1;

    if (!compositor || !compositor->wl_display) {
        wlr_log(WLR_ERROR, "%s", "zen_lock_init: invalid compositor");
        goto cleanup;
    }

    memset(&s_lock, 0, sizeof(s_lock));
    memset(s_password, 0, sizeof(s_password));
    s_password_len = 0;
    s_idle_timer   = NULL;

    /* Create wlr_session_lock_manager_v1. */
    compositor->lock_mgr =
        wlr_session_lock_manager_v1_create(compositor->wl_display);
    if (!compositor->lock_mgr) {
        wlr_log(WLR_ERROR, "%s",
                "zen_lock_init: failed to create session lock manager");
        goto cleanup;
    }

    /* Register new_lock listener. */
    s_new_lock_listener.notify = handle_new_lock;
    wl_signal_add(&compositor->lock_mgr->events.new_lock,
                  &s_new_lock_listener);

    /* Register idle timer (300 seconds). */
    struct wl_event_loop *loop =
        wl_display_get_event_loop(compositor->wl_display);

    s_idle_timer = wl_event_loop_add_timer(loop, idle_timeout_cb, compositor);
    if (s_idle_timer) {
        wl_event_source_timer_update(s_idle_timer, ZEN_LOCK_IDLE_TIMEOUT_MS);
    } else {
        wlr_log(WLR_ERROR, "%s", "zen_lock_init: failed to create idle timer");
        /* Non-fatal — continue without idle lock. */
    }

    /* Register logind Lock signal via sd-bus if bus is available. */
#ifdef HAVE_SYSTEMD
    if (compositor->bus) {
        int r = sd_bus_match_signal(
            compositor->bus,
            &s_lock_signal_slot,
            "org.freedesktop.login1",
            NULL,
            "org.freedesktop.login1.Session",
            "Lock",
            lock_signal_cb,
            compositor);
        if (r < 0) {
            wlr_log(WLR_INFO,
                    "zen_lock_init: sd_bus_match_signal failed (%d) — "
                    "lid-close lock disabled", r);
            /* Non-fatal. */
        }
    }
#endif

    wlr_log(WLR_INFO, "%s", "Screen lock module initialized");
    ret = 0;

cleanup:
    if (ret != 0) {
        zen_lock_destroy(compositor);
    }
    return ret;
}

void zen_lock_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Remove idle timer. */
    if (s_idle_timer) {
        wl_event_source_remove(s_idle_timer);
        s_idle_timer = NULL;
    }

#ifdef HAVE_SYSTEMD
    /* Remove sd-bus signal match. */
    if (s_lock_signal_slot) {
        sd_bus_slot_unref(s_lock_signal_slot);
        s_lock_signal_slot = NULL;
    }
#endif

    /* Free lock surface Cairo buffer. */
    if (s_lock.lock_buffer) {
        zen_cairo_buffer_destroy(s_lock.lock_buffer);
        free(s_lock.lock_buffer);
        s_lock.lock_buffer = NULL;
    }

    compositor->locked      = false;
    compositor->active_lock = NULL;
    compositor->lock_mgr    = NULL;

    memset(&s_lock, 0, sizeof(s_lock));
    memset(s_password, 0, sizeof(s_password));
    s_password_len = 0;

    wlr_log(WLR_INFO, "%s", "Screen lock module destroyed");
}
