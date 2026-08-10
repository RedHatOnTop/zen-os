/*
 * Zen OS Shell — Shelf (Taskbar)
 *
 * Renders the persistent bottom bar with pinned app icons, running indicators,
 * and clock in the right-side tray area.
 * Cairo + Pango for content, SceneFX for rounded corners and drop shadows.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <cairo.h>
#include <fcntl.h>
#include <pango/pangocairo.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <systemd/sd-bus.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>

#include "zen/cairo_buffer.h"
#include "zen/compositor.h"
#include "zen/app-launcher.h"
#include "zen/models.h"
#include "zen/shelf-config.h"
#include "zen/shelf.h"
#include "zen/shell.h"
#include "zen/shell-testctl.h"
#include "zen/theme-config.h"
#include "zen/xdg.h"

/* Height of the shelf bar in pixels. */
#define SHELF_HEIGHT 48

/* Default icon size in pixels (matches shelf-config default). */
#define DEFAULT_ICON_SIZE 32

/* Horizontal padding between pinned app icon slots. */
#define ICON_SLOT_PADDING 8

/* Left margin before the first icon. */
#define ICON_LEFT_MARGIN 8

/* Running indicator dot radius in pixels. */
#define RUNNING_DOT_RADIUS 4

/* Running indicator dot vertical offset below icon bottom edge. */
#define RUNNING_DOT_OFFSET 2

/* Clock font description. */
#define CLOCK_FONT "Sans Bold 13"

/* Clock right-side padding from the shelf edge. */
#define CLOCK_PADDING_RIGHT 16

/* Placeholder icon font description (first letter of app name). */
#define ICON_FONT "Sans Bold 14"

/* ── Auto-hide ───────────────────────────────────────────────────────────── */

/*
 * Compute the output dimensions from the compositor's output layout.
 * Falls back to 1920×1080 if the layout box is empty.
 */
static void shelf_get_output_dims(struct ZenShelf *shelf, int *out_w, int *out_h)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    struct wlr_box layout_box;

    *out_w = 1920;
    *out_h = 1080;

    wlr_output_layout_get_box(compositor->output_layout, NULL, &layout_box);
    if (layout_box.width  > 0) *out_w = layout_box.width;
    if (layout_box.height > 0) *out_h = layout_box.height;
}

/*
 * Slide the shelf off-screen by repositioning its scene node to the hidden
 * position for the current shelf edge.  Sets auto_hide_active = true.
 */
static void shelf_slide_off(struct ZenShelf *shelf)
{
    if (!shelf->bar_buffer || !shelf->bar_buffer->scene_buffer) return;

    int out_w = 0, out_h = 0;
    shelf_get_output_dims(shelf, &out_w, &out_h);

    int hide_x = shelf->x;
    int hide_y = shelf->y;

    ZenShelfPosition pos = ZEN_SHELF_BOTTOM;
    if (shelf->config) pos = shelf->config->position;

    switch (pos) {
    case ZEN_SHELF_TOP:
        hide_y = -shelf->height;
        break;
    case ZEN_SHELF_LEFT:
        hide_x = -shelf->width;
        break;
    case ZEN_SHELF_RIGHT:
        hide_x = out_w;
        break;
    case ZEN_SHELF_BOTTOM:
    default:
        hide_y = out_h;
        break;
    }

    wlr_scene_node_set_position(&shelf->bar_buffer->scene_buffer->node,
                                hide_x, hide_y);
    if (shelf->shadow) {
        wlr_scene_node_set_position(&shelf->shadow->node, hide_x, hide_y);
    }
    shelf->auto_hide_active = true;
    zen_shell_testctl_sync(shelf->shell);
    wlr_log(WLR_DEBUG, "shelf: auto-hide: slid off-screen to (%d, %d)",
            hide_x, hide_y);
}

/*
 * Slide the shelf back on-screen by restoring its scene node to shelf->x/y.
 * Sets auto_hide_active = false.
 */
static void shelf_slide_on(struct ZenShelf *shelf)
{
    if (!shelf->bar_buffer || !shelf->bar_buffer->scene_buffer) return;

    wlr_scene_node_set_position(&shelf->bar_buffer->scene_buffer->node,
                                shelf->x, shelf->y);
    if (shelf->shadow) {
        wlr_scene_node_set_position(&shelf->shadow->node,
                                    shelf->x, shelf->y);
    }
    shelf->auto_hide_active = false;
    zen_shell_testctl_sync(shelf->shell);
    wlr_log(WLR_DEBUG, "shelf: auto-hide: slid on-screen to (%d, %d)",
            shelf->x, shelf->y);
}

/*
 * Timer callback: fires 3 seconds after the pointer left the shelf proximity
 * zone.  Slides the shelf off-screen.
 */
static int auto_hide_timer_cb(void *data)
{
    struct ZenShelf *shelf = data;

    shelf_slide_off(shelf);
    return 0;
}

void zen_shelf_handle_pointer_motion(struct ZenShelf *shelf, int x, int y)
{
    if (!shelf) return;

    /* No-op when auto-hide is disabled. */
    bool auto_hide = shelf->config ? shelf->config->auto_hide : false;
    if (!auto_hide) return;

    int out_w = 0, out_h = 0;
    shelf_get_output_dims(shelf, &out_w, &out_h);

    ZenShelfPosition pos = ZEN_SHELF_BOTTOM;
    if (shelf->config) pos = shelf->config->position;

    /*
     * Determine whether the pointer is within 2px of the shelf edge.
     * "Shelf edge" is the screen edge that the shelf is attached to.
     */
    bool near_edge = false;
    switch (pos) {
    case ZEN_SHELF_TOP:
        near_edge = (y <= 2);
        break;
    case ZEN_SHELF_LEFT:
        near_edge = (x <= 2);
        break;
    case ZEN_SHELF_RIGHT:
        near_edge = (x >= out_w - 2);
        break;
    case ZEN_SHELF_BOTTOM:
    default:
        near_edge = (y >= out_h - 2);
        break;
    }

    if (near_edge) {
        /* Cancel any pending hide timer. */
        if (shelf->auto_hide_timer) {
            wl_event_source_timer_update(shelf->auto_hide_timer, 0);
        }

        /* Slide back on-screen if currently hidden. */
        if (shelf->auto_hide_active) {
            shelf_slide_on(shelf);
        }
    } else {
        /* Pointer is away from the shelf edge — arm the 3-second hide timer. */
        if (!shelf->auto_hide_active) {
            if (!shelf->auto_hide_timer) {
                struct ZenCompositor *compositor = shelf->shell->compositor;
                struct wl_event_loop *event_loop =
                    wl_display_get_event_loop(compositor->wl_display);

                shelf->auto_hide_timer = wl_event_loop_add_timer(
                    event_loop, auto_hide_timer_cb, shelf);
                if (!shelf->auto_hide_timer) {
                    wlr_log(WLR_ERROR, "%s",
                            "shelf: failed to create auto-hide timer");
                    return;
                }
            }
            /* (Re-)arm for 3 seconds; harmless if already armed. */
            wl_event_source_timer_update(shelf->auto_hide_timer, 3000);
        }
    }
}

/* ── Context menu constants ───────────────────────────────────────────────── */

/* Width of the context menu popup in pixels. */
#define CONTEXT_MENU_WIDTH       200

/* Height of each menu item row in pixels. */
#define CONTEXT_MENU_ITEM_HEIGHT 32

/* Corner radius for the context menu background. */
#define CONTEXT_MENU_CORNER_R    8.0

/* Font for context menu item labels. */
#define CONTEXT_MENU_FONT        "Sans 11"

/* Left padding for item text inside the menu. */
#define CONTEXT_MENU_TEXT_PAD    12

/* Number of menu items. */
#define CONTEXT_MENU_ITEM_COUNT  6

/* Forward declarations */
static int clock_timer_cb(void *data);
static void render_pinned_apps(struct ZenShelf *shelf, cairo_t *cr,
                               const struct ZenColorPalette *palette);
static char *get_config_path(char *buf, size_t size);

static const char *resolve_home_dir(void)
{
    const char *home = getenv("HOME");
    struct passwd *passwd_entry = NULL;

    if (home && home[0] != '\0') {
        return home;
    }

    passwd_entry = getpwuid(getuid());
    if (passwd_entry && passwd_entry->pw_dir
        && passwd_entry->pw_dir[0] != '\0') {
        return passwd_entry->pw_dir;
    }

    return NULL;
}

/*
 * Format the current local time into shelf->clock_text as "HH:MM\0".
 */
static void update_clock_text(struct ZenShelf *shelf)
{
    time_t    now = time(NULL);
    struct tm tm_local;

    localtime_r(&now, &tm_local);
    strftime(shelf->clock_text, sizeof(shelf->clock_text), "%H:%M", &tm_local);
}

static int clock_timer_cb(void *data)
{
    struct ZenShelf *shelf = data;

    update_clock_text(shelf);
    zen_shelf_repaint(shelf);

    /* Re-arm for the next minute. */
    wl_event_source_timer_update(shelf->clock_timer, 60000);
    return 0;
}

/*
 * Render a placeholder icon for a pinned app: filled circle in accent color
 * with the first letter of the app name centered in white.
 */
static void render_placeholder_icon(cairo_t *cr,
                                    const struct ZenColorPalette *palette,
                                    const char *name,
                                    int x, int y, int size)
{
    double cx = x + size / 2.0;
    double cy = y + size / 2.0;
    double r  = size / 2.0 - 2.0;

    /* Filled circle in accent color */
    cairo_save(cr);
    cairo_arc(cr, cx, cy, r, 0.0, 2.0 * 3.14159265358979323846);
    cairo_set_source_rgba(cr,
                          (double)palette->accent[0],
                          (double)palette->accent[1],
                          (double)palette->accent[2],
                          (double)palette->accent[3]);
    cairo_fill(cr);
    cairo_restore(cr);

    /* First letter of app name, centered */
    if (name && name[0] != '\0') {
        char letter[2] = { name[0], '\0' };

        PangoLayout          *layout    = pango_cairo_create_layout(cr);
        PangoFontDescription *font_desc =
            pango_font_description_from_string(ICON_FONT);

        pango_layout_set_font_description(layout, font_desc);
        pango_font_description_free(font_desc);
        pango_layout_set_text(layout, letter, -1);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);

        cairo_save(cr);
        cairo_move_to(cr,
                      cx - tw / 2.0,
                      cy - th / 2.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        pango_cairo_show_layout(cr, layout);
        cairo_restore(cr);

        g_object_unref(layout);
    }
}

/*
 * Render all pinned app icons and their running indicator dots.
 * Icons are laid out left-to-right starting at ICON_LEFT_MARGIN.
 */
static void render_pinned_apps(struct ZenShelf *shelf, cairo_t *cr,
                               const struct ZenColorPalette *palette)
{
    int icon_size = DEFAULT_ICON_SIZE;
    if (shelf->config && shelf->config->icon_size > 0) {
        icon_size = shelf->config->icon_size;
    }

    int slot_w = icon_size + ICON_SLOT_PADDING;
    int icon_y = (shelf->height - icon_size) / 2;

    for (int i = 0; i < shelf->pinned_count; i++) {
        struct ZenAppEntry *entry = &shelf->pinned_apps[i];
        int icon_x = ICON_LEFT_MARGIN + i * slot_w;

        /* Try to load icon from path; fall back to placeholder */
        bool icon_loaded = false;

        if (entry->icon_path && entry->icon_path[0] != '\0') {
            cairo_surface_t *img =
                cairo_image_surface_create_from_png(entry->icon_path);
            if (img && cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
                int img_w = cairo_image_surface_get_width(img);
                int img_h = cairo_image_surface_get_height(img);

                if (img_w > 0 && img_h > 0) {
                    cairo_save(cr);
                    cairo_translate(cr, (double)icon_x, (double)icon_y);
                    cairo_scale(cr,
                                (double)icon_size / (double)img_w,
                                (double)icon_size / (double)img_h);
                    cairo_set_source_surface(cr, img, 0.0, 0.0);
                    cairo_paint(cr);
                    cairo_restore(cr);
                    icon_loaded = true;
                }
            }
            if (img) cairo_surface_destroy(img);
        }

        if (!icon_loaded) {
            const char *display_name = entry->name ? entry->name : entry->app_id;
            render_placeholder_icon(cr, palette, display_name,
                                    icon_x, icon_y, icon_size);
        }

        /* Running indicator dot below icon */
        if (entry->is_running) {
            int dot_cx = icon_x + icon_size / 2;
            int dot_cy = icon_y + icon_size + RUNNING_DOT_OFFSET + RUNNING_DOT_RADIUS;

            cairo_save(cr);
            cairo_arc(cr, (double)dot_cx, (double)dot_cy,
                      (double)RUNNING_DOT_RADIUS, 0.0,
                      2.0 * 3.14159265358979323846);
            cairo_set_source_rgba(cr,
                                  (double)palette->accent[0],
                                  (double)palette->accent[1],
                                  (double)palette->accent[2],
                                  (double)palette->accent[3]);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }
}

/* ── System Tray D-Bus Subscriptions ─────────────────────────────────────── */

/*
 * UPower PropertiesChanged handler.
 * Reads Percentage and State from the changed properties dict.
 */
static int upower_properties_changed_cb(sd_bus_message *msg, void *userdata,
                                         sd_bus_error *error)
{
    (void)error;
    struct ZenShelf *shelf = userdata;
    int ret;

    /* Message format: sa{sv}as (interface, changed props, invalidated props) */
    const char *iface = NULL;
    ret = sd_bus_message_read(msg, "s", &iface);
    if (ret < 0) return 0;

    /* Only care about the device interface */
    if (!iface || strcmp(iface, "org.freedesktop.UPower.Device") != 0) {
        return 0;
    }

    ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{sv}");
    if (ret < 0) return 0;

    double percentage = -1.0;
    uint32_t state = 0;
    bool got_percentage = false;
    bool got_state = false;

    while (sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char *key = NULL;
        ret = sd_bus_message_read_basic(msg, SD_BUS_TYPE_STRING, &key);
        if (ret < 0) {
            sd_bus_message_exit_container(msg);
            break;
        }

        if (key && strcmp(key, "Percentage") == 0) {
            ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_VARIANT, "d");
            if (ret >= 0) {
                sd_bus_message_read_basic(msg, SD_BUS_TYPE_DOUBLE, &percentage);
                sd_bus_message_exit_container(msg);
                got_percentage = true;
            }
        } else if (key && strcmp(key, "State") == 0) {
            ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_VARIANT, "u");
            if (ret >= 0) {
                sd_bus_message_read_basic(msg, SD_BUS_TYPE_UINT32, &state);
                sd_bus_message_exit_container(msg);
                got_state = true;
            }
        } else {
            sd_bus_message_skip(msg, "v");
        }

        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);

    if (!got_percentage && !got_state) return 0;

    /* Update battery indicator */
    shelf->battery.available = true;

    if (got_percentage) {
        free(shelf->battery.label);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)percentage);
        shelf->battery.label = strdup(buf);
        /* icon_index: 0=empty,1=low,2=mid,3=high,4=full,5=charging */
        if (state == 1 /* charging */ || state == 4 /* fully-charged */) {
            shelf->battery.icon_index = 5;
        } else if (percentage < 10.0) {
            shelf->battery.icon_index = 0;
        } else if (percentage < 25.0) {
            shelf->battery.icon_index = 1;
        } else if (percentage < 50.0) {
            shelf->battery.icon_index = 2;
        } else if (percentage < 75.0) {
            shelf->battery.icon_index = 3;
        } else {
            shelf->battery.icon_index = 4;
        }
    }

    zen_shelf_repaint(shelf);
    return 0;
}

/*
 * Subscribe to UPower PropertiesChanged on the DisplayDevice.
 * Sets shelf->battery.available = false and logs a warning on failure.
 */
static void shelf_subscribe_upower(struct ZenShelf *shelf)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    if (!compositor->bus) return;

    int ret = sd_bus_match_signal(
        compositor->bus,
        &shelf->upower_slot,
        "org.freedesktop.UPower",
        "/org/freedesktop/UPower/devices/DisplayDevice",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        upower_properties_changed_cb,
        shelf);

    if (ret < 0) {
        wlr_log(WLR_INFO,
                "shelf: UPower unavailable (%s), battery indicator disabled",
                strerror(-ret));
        shelf->battery.available = false;
        free(shelf->battery.label);
        shelf->battery.label = strdup("N/A");
    } else {
        shelf->battery.available = true;
    }
}

/*
 * BlueZ PropertiesChanged handler.
 * Reads Powered from the changed properties dict on hci0.
 */
static int bluez_properties_changed_cb(sd_bus_message *msg, void *userdata,
                                        sd_bus_error *error)
{
    (void)error;
    struct ZenShelf *shelf = userdata;
    int ret;

    const char *iface = NULL;
    ret = sd_bus_message_read(msg, "s", &iface);
    if (ret < 0) return 0;

    if (!iface || strcmp(iface, "org.bluez.Adapter1") != 0) {
        return 0;
    }

    ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{sv}");
    if (ret < 0) return 0;

    bool powered = false;
    bool got_powered = false;

    while (sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char *key = NULL;
        ret = sd_bus_message_read_basic(msg, SD_BUS_TYPE_STRING, &key);
        if (ret < 0) {
            sd_bus_message_exit_container(msg);
            break;
        }

        if (key && strcmp(key, "Powered") == 0) {
            ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_VARIANT, "b");
            if (ret >= 0) {
                int val = 0;
                sd_bus_message_read_basic(msg, SD_BUS_TYPE_BOOLEAN, &val);
                powered = (bool)val;
                sd_bus_message_exit_container(msg);
                got_powered = true;
            }
        } else {
            sd_bus_message_skip(msg, "v");
        }

        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);

    if (!got_powered) return 0;

    shelf->bluetooth.available = true;
    free(shelf->bluetooth.label);
    shelf->bluetooth.label = strdup(powered ? "On" : "Off");
    shelf->bluetooth.icon_index = powered ? 1 : 0;

    zen_shelf_repaint(shelf);
    return 0;
}

/*
 * Subscribe to BlueZ PropertiesChanged on /org/bluez/hci0.
 */
static void shelf_subscribe_bluez(struct ZenShelf *shelf)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    if (!compositor->bus) return;

    int ret = sd_bus_match_signal(
        compositor->bus,
        &shelf->bluez_slot,
        "org.bluez",
        "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        bluez_properties_changed_cb,
        shelf);

    if (ret < 0) {
        wlr_log(WLR_INFO,
                "shelf: BlueZ unavailable (%s), bluetooth indicator disabled",
                strerror(-ret));
        shelf->bluetooth.available = false;
        free(shelf->bluetooth.label);
        shelf->bluetooth.label = strdup("N/A");
    } else {
        shelf->bluetooth.available = true;
    }
}

/*
 * NetworkManager StateChanged handler.
 * Signal carries a single uint32 state value.
 */
static int nm_state_changed_cb(sd_bus_message *msg, void *userdata,
                                sd_bus_error *error)
{
    (void)error;
    struct ZenShelf *shelf = userdata;

    uint32_t state = 0;
    int ret = sd_bus_message_read(msg, "u", &state);
    if (ret < 0) return 0;

    shelf->network.available = true;
    free(shelf->network.label);

    /*
     * NM states: 0=unknown, 10=asleep, 20=disconnected, 30=disconnecting,
     *            40=connecting, 50=connected_local, 60=connected_site,
     *            70=connected_global
     */
    if (state >= 70) {
        shelf->network.label = strdup("Online");
        shelf->network.icon_index = 2;
    } else if (state >= 40) {
        shelf->network.label = strdup("Connecting");
        shelf->network.icon_index = 1;
    } else {
        shelf->network.label = strdup("Offline");
        shelf->network.icon_index = 0;
    }

    zen_shelf_repaint(shelf);
    return 0;
}

/*
 * Subscribe to NetworkManager StateChanged signal.
 */
static void shelf_subscribe_network(struct ZenShelf *shelf)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    if (!compositor->bus) return;

    int ret = sd_bus_match_signal(
        compositor->bus,
        &shelf->network_slot,
        "org.freedesktop.NetworkManager",
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager",
        "StateChanged",
        nm_state_changed_cb,
        shelf);

    if (ret < 0) {
        wlr_log(WLR_INFO,
                "shelf: NetworkManager unavailable (%s), network indicator disabled",
                strerror(-ret));
        shelf->network.available = false;
        free(shelf->network.label);
        shelf->network.label = strdup("N/A");
    } else {
        shelf->network.available = true;
    }
}

/*
 * Volume poll timer callback — runs wpctl to read default sink volume.
 * wpctl output format: "Volume: 0.75" or "Volume: 0.75 [MUTED]"
 */
static int volume_poll_cb(void *data)
{
    struct ZenShelf *shelf = data;

    FILE *fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (!fp) {
        shelf->volume.available = false;
        free(shelf->volume.label);
        shelf->volume.label = strdup("N/A");
        zen_shelf_repaint(shelf);
        wl_event_source_timer_update(shelf->volume_timer, 1000);
        return 0;
    }

    char line[64] = {0};
    if (fgets(line, sizeof(line), fp)) {
        float vol = 0.0f;
        /* "Volume: 0.75" or "Volume: 0.75 [MUTED]" */
        if (sscanf(line, "Volume: %f", &vol) == 1) {
            shelf->volume.available = true;
            free(shelf->volume.label);
            char buf[16];
            int pct = (int)(vol * 100.0f + 0.5f);
            if (pct > 100) pct = 100;
            if (pct < 0)   pct = 0;
            /* Check for muted */
            if (strstr(line, "[MUTED]")) {
                snprintf(buf, sizeof(buf), "%d%% M", pct);
                shelf->volume.icon_index = 0;
            } else {
                snprintf(buf, sizeof(buf), "%d%%", pct);
                shelf->volume.icon_index = pct > 50 ? 2 : (pct > 0 ? 1 : 0);
            }
            shelf->volume.label = strdup(buf);
        } else {
            shelf->volume.available = false;
            free(shelf->volume.label);
            shelf->volume.label = strdup("N/A");
        }
    } else {
        shelf->volume.available = false;
        free(shelf->volume.label);
        shelf->volume.label = strdup("N/A");
    }
    pclose(fp);

    zen_shelf_repaint(shelf);
    wl_event_source_timer_update(shelf->volume_timer, 1000);
    return 0;
}

/*
 * Set up a 1-second timer to poll PipeWire volume via wpctl.
 */
static void shelf_poll_volume(struct ZenShelf *shelf)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    struct wl_event_loop *event_loop =
        wl_display_get_event_loop(compositor->wl_display);

    shelf->volume_timer = wl_event_loop_add_timer(event_loop,
                                                   volume_poll_cb, shelf);
    if (!shelf->volume_timer) {
        wlr_log(WLR_INFO, "%s",
                "shelf: failed to create volume poll timer, volume disabled");
        shelf->volume.available = false;
        free(shelf->volume.label);
        shelf->volume.label = strdup("N/A");
        return;
    }

    shelf->volume.available = true;
    /* Fire immediately for the first reading */
    wl_event_source_timer_update(shelf->volume_timer, 100);
}

/*
 * UDisks2 InterfacesAdded handler.
 * Checks if a block device with removable media was added.
 */
static int udisks_interfaces_added_cb(sd_bus_message *msg, void *userdata,
                                       sd_bus_error *error)
{
    (void)error;
    struct ZenShelf *shelf = userdata;

    /* Skip the object path */
    const char *path = NULL;
    int ret = sd_bus_message_read(msg, "o", &path);
    if (ret < 0) return 0;

    /* Check if any of the added interfaces is a filesystem (mounted drive) */
    ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
    if (ret < 0) return 0;

    bool found_fs = false;
    while (sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0) {
        const char *iface = NULL;
        sd_bus_message_read_basic(msg, SD_BUS_TYPE_STRING, &iface);
        if (iface && strcmp(iface, "org.freedesktop.UDisks2.Filesystem") == 0) {
            found_fs = true;
        }
        sd_bus_message_skip(msg, "a{sv}");
        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);

    if (found_fs) {
        shelf->storage.available = true;
        free(shelf->storage.label);
        shelf->storage.label = strdup("USB");
        shelf->storage.icon_index = 1;
        zen_shelf_repaint(shelf);
    }

    return 0;
}

/*
 * UDisks2 InterfacesRemoved handler.
 */
static int udisks_interfaces_removed_cb(sd_bus_message *msg, void *userdata,
                                         sd_bus_error *error)
{
    (void)error;
    struct ZenShelf *shelf = userdata;

    const char *path = NULL;
    int ret = sd_bus_message_read(msg, "o", &path);
    if (ret < 0) return 0;

    /* Check if a filesystem interface was removed */
    ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "s");
    if (ret < 0) return 0;

    bool found_fs = false;
    const char *iface = NULL;
    while (sd_bus_message_read_basic(msg, SD_BUS_TYPE_STRING, &iface) > 0) {
        if (iface && strcmp(iface, "org.freedesktop.UDisks2.Filesystem") == 0) {
            found_fs = true;
        }
    }
    sd_bus_message_exit_container(msg);

    if (found_fs) {
        shelf->storage.available = false;
        free(shelf->storage.label);
        shelf->storage.label = strdup("");
        shelf->storage.icon_index = 0;
        zen_shelf_repaint(shelf);
    }

    return 0;
}

/*
 * Subscribe to UDisks2 InterfacesAdded and InterfacesRemoved signals.
 * Uses a single slot for both via the ObjectManager interface.
 * We subscribe to InterfacesAdded; for InterfacesRemoved we use a second slot
 * stored in udisks_slot (we reuse the field for the removed signal and add
 * the added signal inline since we only have one slot field — we chain them
 * by using a NULL slot for the second match, which is fire-and-forget).
 */
static void shelf_subscribe_udisks(struct ZenShelf *shelf)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    if (!compositor->bus) return;

    /* Subscribe to InterfacesAdded */
    int ret = sd_bus_match_signal(
        compositor->bus,
        &shelf->udisks_slot,
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesAdded",
        udisks_interfaces_added_cb,
        shelf);

    if (ret < 0) {
        wlr_log(WLR_INFO,
                "shelf: UDisks2 unavailable (%s), storage indicator disabled",
                strerror(-ret));
        shelf->storage.available = false;
        free(shelf->storage.label);
        shelf->storage.label = strdup("N/A");
        return;
    }

    /* Subscribe to InterfacesRemoved (slot not stored — fire-and-forget) */
    ret = sd_bus_match_signal(
        compositor->bus,
        NULL,
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesRemoved",
        udisks_interfaces_removed_cb,
        shelf);

    if (ret < 0) {
        wlr_log(WLR_INFO,
                "shelf: UDisks2 InterfacesRemoved subscription failed (%s)",
                strerror(-ret));
    }

    shelf->storage.available = true;
}

/*
 * NameOwnerChanged handler — re-subscribes tray indicators when a D-Bus
 * service comes back online after being unavailable.
 */
static int name_owner_changed_cb(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *error)
{
    (void)error;
    struct ZenShelf *shelf = userdata;

    const char *name = NULL;
    const char *old_owner = NULL;
    const char *new_owner = NULL;

    int ret = sd_bus_message_read(msg, "sss", &name, &old_owner, &new_owner);
    if (ret < 0) return 0;

    /* Only care about services appearing (new_owner non-empty, old_owner empty) */
    if (!new_owner || new_owner[0] == '\0') return 0;
    if (!name) return 0;

    if (strcmp(name, "org.freedesktop.UPower") == 0 &&
        !shelf->battery.available) {
        wlr_log(WLR_INFO, "%s", "shelf: UPower appeared, re-subscribing");
        if (shelf->upower_slot) {
            sd_bus_slot_unref(shelf->upower_slot);
            shelf->upower_slot = NULL;
        }
        shelf_subscribe_upower(shelf);
    } else if (strcmp(name, "org.bluez") == 0 &&
               !shelf->bluetooth.available) {
        wlr_log(WLR_INFO, "%s", "shelf: BlueZ appeared, re-subscribing");
        if (shelf->bluez_slot) {
            sd_bus_slot_unref(shelf->bluez_slot);
            shelf->bluez_slot = NULL;
        }
        shelf_subscribe_bluez(shelf);
    } else if (strcmp(name, "org.freedesktop.NetworkManager") == 0 &&
               !shelf->network.available) {
        wlr_log(WLR_INFO, "%s", "shelf: NetworkManager appeared, re-subscribing");
        if (shelf->network_slot) {
            sd_bus_slot_unref(shelf->network_slot);
            shelf->network_slot = NULL;
        }
        shelf_subscribe_network(shelf);
    } else if (strcmp(name, "org.freedesktop.UDisks2") == 0 &&
               !shelf->storage.available) {
        wlr_log(WLR_INFO, "%s", "shelf: UDisks2 appeared, re-subscribing");
        if (shelf->udisks_slot) {
            sd_bus_slot_unref(shelf->udisks_slot);
            shelf->udisks_slot = NULL;
        }
        shelf_subscribe_udisks(shelf);
    }

    return 0;
}

/*
 * Subscribe to org.freedesktop.DBus NameOwnerChanged to detect services
 * coming online and re-subscribe tray indicators.
 */
static void shelf_subscribe_name_owner_changed(struct ZenShelf *shelf)
{
    struct ZenCompositor *compositor = shelf->shell->compositor;
    if (!compositor->bus) return;

    int ret = sd_bus_match_signal(
        compositor->bus,
        &shelf->name_owner_slot,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameOwnerChanged",
        name_owner_changed_cb,
        shelf);

    if (ret < 0) {
        wlr_log(WLR_INFO,
                "shelf: NameOwnerChanged subscription failed (%s), "
                "no auto-retry for tray services",
                strerror(-ret));
    }
}

int zen_shelf_init(struct ZenShelf *shelf, struct ZenShell *shell)
{
    int ret = -1;

    memset(shelf, 0, sizeof(*shelf));
    shelf->shell = shell;

    struct ZenCompositor *compositor = shell->compositor;

    /* 1. Determine primary output dimensions */
    int out_w = 1920, out_h = 1080; /* safe defaults */

    struct wlr_box layout_box;
    wlr_output_layout_get_box(compositor->output_layout, NULL, &layout_box);
    if (layout_box.width > 0)  out_w = layout_box.width;
    if (layout_box.height > 0) out_h = layout_box.height;

    /* 2. Set shelf geometry: full width, 48px at bottom */
    shelf->x      = 0;
    shelf->y      = out_h - SHELF_HEIGHT;
    shelf->width  = out_w;
    shelf->height = SHELF_HEIGHT;

    /* 3. Load shelf config and populate pinned apps */
    shelf->config = calloc(1, sizeof(ZenShelfConfig));
    if (!shelf->config) {
        wlr_log(WLR_ERROR, "%s", "shelf: failed to allocate config");
        goto cleanup;
    }

    {
        char        path_buf[512];
        const char *config_path = get_config_path(path_buf, sizeof(path_buf));

        int load_ret = zen_shelf_config_load(shelf->config, config_path);
        if (load_ret != 0) {
            wlr_log(WLR_INFO, "%s",
                    "shelf: shelf.json missing or invalid, using defaults");
        }
    }

    /* Populate pinned_apps array from config */
    {
        ZenShelfConfig *cfg = shelf->config;
        if (cfg->pinned_apps_count > 0) {
            shelf->pinned_apps = calloc((size_t)cfg->pinned_apps_count,
                                        sizeof(struct ZenAppEntry));
            if (!shelf->pinned_apps) {
                wlr_log(WLR_ERROR, "%s",
                        "shelf: failed to allocate pinned_apps");
                goto cleanup;
            }

            for (int i = 0; i < cfg->pinned_apps_count; i++) {
                struct ZenAppEntry *entry = &shelf->pinned_apps[i];
                const char *app_id = cfg->pinned_apps[i];

                entry->app_id = strdup(app_id ? app_id : "");
                if (!entry->app_id) goto cleanup;

                /* Derive a display name from the app_id (last component) */
                const char *dot = strrchr(app_id ? app_id : "", '.');
                const char *display = dot ? dot + 1 : (app_id ? app_id : "");
                entry->name = strdup(display);
                if (!entry->name) goto cleanup;

                /* icon_path and exec_cmd left NULL — loaded from desktop
                 * entries in a later task (2.5) */
                entry->icon_path = NULL;
                entry->exec_cmd  = NULL;
                entry->is_running = false;

                shelf->pinned_count++;
            }
        }
    }

    /* 4. Allocate and create the bar buffer */
    shelf->bar_buffer = calloc(1, sizeof(*shelf->bar_buffer));
    if (!shelf->bar_buffer) {
        wlr_log(WLR_ERROR, "%s", "shelf: failed to allocate bar_buffer");
        goto cleanup;
    }

    /*
     * Use compositor->shell_overlay_tree as parent.
     * TODO(task 2.1.7): switch to compositor->shelf_tree once that field
     * is added to ZenCompositor.
     */
    if (zen_cairo_buffer_create(shelf->bar_buffer,
                                compositor->renderer,
                                compositor->shell_overlay_tree,
                                shelf->width, shelf->height) != 0) {
        wlr_log(WLR_ERROR, "%s", "shelf: zen_cairo_buffer_create failed");
        free(shelf->bar_buffer);
        shelf->bar_buffer = NULL;
        goto cleanup;
    }

    /* Position the scene node at the correct screen coordinates. */
    wlr_scene_node_set_position(&shelf->bar_buffer->scene_buffer->node,
                                shelf->x, shelf->y);

    /* Apply SceneFX rounded corners (>=8px top-left/top-right). */
    wlr_scene_buffer_set_corner_radius(shelf->bar_buffer->scene_buffer,
                                       8, CORNER_LOCATION_TOP);

    /* Create SceneFX drop shadow behind the shelf bar. */
    {
        float shadow_color[4] = { 0.0f, 0.0f, 0.0f, 0.6f };
        shelf->shadow = wlr_scene_shadow_create(
            compositor->shell_overlay_tree,
            shelf->width, shelf->height, 8, 10.0f, shadow_color);
        if (shelf->shadow) {
            wlr_scene_node_set_position(&shelf->shadow->node,
                                        shelf->x, shelf->y);
            /* Place shadow behind the bar buffer. */
            wlr_scene_node_place_below(&shelf->shadow->node,
                                       &shelf->bar_buffer->scene_buffer->node);
        }
    }

    /* 5. Set up clock timer */
    struct wl_event_loop *event_loop =
        wl_display_get_event_loop(compositor->wl_display);

    shelf->clock_timer = wl_event_loop_add_timer(event_loop,
                                                  clock_timer_cb, shelf);
    if (!shelf->clock_timer) {
        wlr_log(WLR_ERROR, "%s", "shelf: failed to create clock timer");
        goto cleanup;
    }

    /* Arm the timer for the first tick in 60 seconds. */
    wl_event_source_timer_update(shelf->clock_timer, 60000);

    /* 7. Subscribe to D-Bus tray indicator signals.
     * Each subscription is best-effort: failure sets available=false and logs
     * a warning without aborting shelf init. */
    shelf_subscribe_upower(shelf);
    shelf_subscribe_bluez(shelf);
    shelf_subscribe_network(shelf);
    shelf_poll_volume(shelf);
    shelf_subscribe_udisks(shelf);
    shelf_subscribe_name_owner_changed(shelf);

    /* 8. Initial paint */
    update_clock_text(shelf);
    zen_shelf_repaint(shelf);

    ret = 0;

cleanup:
    if (ret != 0) {
        zen_shelf_destroy(shelf);
    }
    return ret;
}

void zen_shelf_destroy(struct ZenShelf *shelf)
{
    if (!shelf) return;

    if (shelf->clock_timer) {
        wl_event_source_remove(shelf->clock_timer);
        shelf->clock_timer = NULL;
    }

    if (shelf->auto_hide_timer) {
        wl_event_source_remove(shelf->auto_hide_timer);
        shelf->auto_hide_timer = NULL;
    }

    if (shelf->volume_timer) {
        wl_event_source_remove(shelf->volume_timer);
        shelf->volume_timer = NULL;
    }

    if (shelf->name_owner_slot) {
        sd_bus_slot_unref(shelf->name_owner_slot);
        shelf->name_owner_slot = NULL;
    }

    if (shelf->upower_slot) {
        sd_bus_slot_unref(shelf->upower_slot);
        shelf->upower_slot = NULL;
    }

    if (shelf->bluez_slot) {
        sd_bus_slot_unref(shelf->bluez_slot);
        shelf->bluez_slot = NULL;
    }

    if (shelf->network_slot) {
        sd_bus_slot_unref(shelf->network_slot);
        shelf->network_slot = NULL;
    }

    if (shelf->udisks_slot) {
        sd_bus_slot_unref(shelf->udisks_slot);
        shelf->udisks_slot = NULL;
    }

    if (shelf->shadow) {
        wlr_scene_node_destroy(&shelf->shadow->node);
        shelf->shadow = NULL;
    }

    if (shelf->bar_buffer) {
        zen_cairo_buffer_destroy(shelf->bar_buffer);
        free(shelf->bar_buffer);
        shelf->bar_buffer = NULL;
    }

    if (shelf->context_menu_buf) {
        zen_cairo_buffer_destroy(shelf->context_menu_buf);
        free(shelf->context_menu_buf);
        shelf->context_menu_buf = NULL;
    }

    /* Free pinned app entries. */
    for (int i = 0; i < shelf->pinned_count; i++) {
        free(shelf->pinned_apps[i].app_id);
        free(shelf->pinned_apps[i].name);
        free(shelf->pinned_apps[i].icon_path);
        free(shelf->pinned_apps[i].exec_cmd);
    }
    free(shelf->pinned_apps);
    shelf->pinned_apps  = NULL;
    shelf->pinned_count = 0;

    /* Free shelf config. */
    if (shelf->config) {
        zen_shelf_config_cleanup(shelf->config);
        free(shelf->config);
        shelf->config = NULL;
    }

    /* Free tray indicator labels. */
    free(shelf->battery.label);
    free(shelf->bluetooth.label);
    free(shelf->network.label);
    free(shelf->volume.label);
    free(shelf->storage.label);
    shelf->battery.label   = NULL;
    shelf->bluetooth.label = NULL;
    shelf->network.label   = NULL;
    shelf->volume.label    = NULL;
    shelf->storage.label   = NULL;

    /* Free drag state. */
    free(shelf->drag_app_id);
    shelf->drag_app_id = NULL;
    shelf->drag_active = false;
}

/* ── Tray Indicator Rendering ────────────────────────────────────────────── */

/* Font for tray indicator labels. */
#define TRAY_FONT "Sans 10"

/* Gap between adjacent tray indicators (pixels). */
#define TRAY_INDICATOR_GAP 8

/*
 * Render system tray indicators right-to-left, starting from the left edge
 * of the clock area.  Each visible indicator is drawn as a text label using
 * the theme foreground color.
 *
 * Indicators are rendered in this order (right-to-left):
 *   battery → bluetooth → network → volume → storage
 *
 * Returns the total pixel width consumed by all rendered indicators
 * (including gaps), so the caller can offset other elements if needed.
 */
static int render_tray_indicators(struct ZenShelf *shelf, cairo_t *cr,
                                  const struct ZenColorPalette *palette,
                                  int right_edge_x)
{
    /* Build display strings for each indicator.
     * NULL means "skip this indicator". */
    char bat_buf[24]  = {0};
    char bt_buf[16]   = {0};
    char net_buf[16]  = {0};
    char vol_buf[24]  = {0};
    char stor_buf[8]  = {0};

    const char *labels[5] = { NULL, NULL, NULL, NULL, NULL };

    /* Battery: show raw label (e.g. "85%") */
    if (shelf->battery.available &&
        shelf->battery.label && shelf->battery.label[0] != '\0') {
        snprintf(bat_buf, sizeof(bat_buf), "%s", shelf->battery.label);
        labels[0] = bat_buf;
    }

    /* Bluetooth: prepend "BT: " to the stored "On"/"Off" label */
    if (shelf->bluetooth.available &&
        shelf->bluetooth.label && shelf->bluetooth.label[0] != '\0') {
        snprintf(bt_buf, sizeof(bt_buf), "BT: %s", shelf->bluetooth.label);
        labels[1] = bt_buf;
    }

    /* Network: show label as-is ("Online"/"Offline"/"Connecting") */
    if (shelf->network.available &&
        shelf->network.label && shelf->network.label[0] != '\0') {
        snprintf(net_buf, sizeof(net_buf), "%s", shelf->network.label);
        labels[2] = net_buf;
    }

    /* Volume: prepend "Vol: " to the stored percentage label */
    if (shelf->volume.available &&
        shelf->volume.label && shelf->volume.label[0] != '\0') {
        snprintf(vol_buf, sizeof(vol_buf), "Vol: %s", shelf->volume.label);
        labels[3] = vol_buf;
    }

    /* Storage: show "USB" only when available and label is non-empty */
    if (shelf->storage.available &&
        shelf->storage.label && shelf->storage.label[0] != '\0') {
        snprintf(stor_buf, sizeof(stor_buf), "%s", shelf->storage.label);
        labels[4] = stor_buf;
    }

    /* Set up a temporary Pango layout for measuring and drawing. */
    PangoLayout          *layout    = pango_cairo_create_layout(cr);
    PangoFontDescription *font_desc =
        pango_font_description_from_string(TRAY_FONT);
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    cairo_set_source_rgba(cr,
                          (double)palette->foreground[0],
                          (double)palette->foreground[1],
                          (double)palette->foreground[2],
                          (double)palette->foreground[3]);

    int cursor_x = right_edge_x; /* current right edge, moves left */

    /* Render right-to-left: battery(0) is rightmost, storage(4) leftmost */
    for (int i = 0; i < 5; i++) {
        if (!labels[i]) continue;

        pango_layout_set_text(layout, labels[i], -1);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);

        cursor_x -= tw;

        int label_y = (shelf->height - th) / 2;

        cairo_save(cr);
        cairo_move_to(cr, (double)cursor_x, (double)label_y);
        pango_cairo_show_layout(cr, layout);
        cairo_restore(cr);

        cursor_x -= TRAY_INDICATOR_GAP;
    }

    g_object_unref(layout);

    /* Return total width consumed (right_edge_x - cursor_x). */
    return right_edge_x - cursor_x;
}

void zen_shelf_repaint(struct ZenShelf *shelf)
{
    if (!shelf || !shelf->bar_buffer || !shelf->bar_buffer->cr) return;

    struct ZenShell              *shell   = shelf->shell;
    cairo_t                      *cr      = shelf->bar_buffer->cr;
    const struct ZenColorPalette *palette =
        zen_theme_config_get_palette(shell->theme);

    /* 1. Clear with theme background color */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr,
                          (double)palette->background[0],
                          (double)palette->background[1],
                          (double)palette->background[2],
                          (double)palette->background[3]);
    cairo_paint(cr);
    cairo_restore(cr);

    /* 2. Render pinned app icons (left portion) */
    render_pinned_apps(shelf, cr, palette);

    /* 3. Render clock text in the right-side tray area */
    PangoLayout          *layout    = zen_cairo_buffer_get_pango(shelf->bar_buffer);
    PangoFontDescription *font_desc =
        pango_font_description_from_string(CLOCK_FONT);

    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    pango_layout_set_text(layout, shelf->clock_text, -1);

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    /* Right-align clock with padding; vertically center in shelf. */
    int clock_x = shelf->width - text_w - CLOCK_PADDING_RIGHT;
    int clock_y = (shelf->height - text_h) / 2;

    cairo_save(cr);
    cairo_move_to(cr, (double)clock_x, (double)clock_y);
    cairo_set_source_rgba(cr,
                          (double)palette->foreground[0],
                          (double)palette->foreground[1],
                          (double)palette->foreground[2],
                          (double)palette->foreground[3]);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);

    /* 3a. Render tray indicators to the left of the clock. */
    int tray_right_edge = clock_x - TRAY_INDICATOR_GAP;
    render_tray_indicators(shelf, cr, palette, tray_right_edge);

    /* 4. Upload pixels to the scene buffer */
    zen_cairo_buffer_submit(shelf->bar_buffer);
    zen_shell_testctl_sync(shell);
}

/* ── Config path helper ──────────────────────────────────────────────────── */

/*
 * Build the shelf config path "$HOME/.config/zenos/shelf.json" into buf.
 * Returns buf on success, NULL if HOME is not set.
 */
static char *get_config_path(char *buf, size_t size)
{
    const char *home = resolve_home_dir();
    if (!home) return NULL;
    snprintf(buf, size, "%s/.config/zenos/shelf.json", home);
    return buf;
}

/* ── Pin / Unpin ─────────────────────────────────────────────────────────── */

int zen_shelf_pin_app(struct ZenShelf *shelf, const char *app_id)
{
    int   ret        = -1;
    char *id_copy    = NULL;
    char *name_copy  = NULL;
    char  path_buf[512];

    if (!shelf || !app_id) goto cleanup;

    /* Idempotent: already pinned → success */
    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            ret = 0;
            goto cleanup;
        }
    }

    /* Grow the pinned_apps array by one entry */
    struct ZenAppEntry *new_arr = realloc(
        shelf->pinned_apps,
        (size_t)(shelf->pinned_count + 1) * sizeof(struct ZenAppEntry));
    if (!new_arr) goto cleanup;
    shelf->pinned_apps = new_arr;

    /* Populate the new entry */
    id_copy = strdup(app_id);
    if (!id_copy) goto cleanup;

    /* Derive display name from last dot-component of app_id */
    const char *dot     = strrchr(app_id, '.');
    const char *display = dot ? dot + 1 : app_id;
    name_copy = strdup(display);
    if (!name_copy) goto cleanup;

    struct ZenAppEntry *entry = &shelf->pinned_apps[shelf->pinned_count];
    entry->app_id    = id_copy;   id_copy   = NULL;
    entry->name      = name_copy; name_copy = NULL;
    entry->icon_path = NULL;
    entry->exec_cmd  = NULL;
    entry->is_running = false;
    shelf->pinned_count++;

    /* Update config's pinned_apps array */
    {
        ZenShelfConfig *cfg = shelf->config;
        char **new_apps = realloc(cfg->pinned_apps,
                                  (size_t)shelf->pinned_count * sizeof(char *));
        if (!new_apps) {
            /* Roll back the entry we just added */
            free(shelf->pinned_apps[shelf->pinned_count - 1].app_id);
            free(shelf->pinned_apps[shelf->pinned_count - 1].name);
            shelf->pinned_count--;
            goto cleanup;
        }
        cfg->pinned_apps = new_apps;
        cfg->pinned_apps[shelf->pinned_count - 1] = strdup(app_id);
        if (!cfg->pinned_apps[shelf->pinned_count - 1]) {
            /* Roll back */
            free(shelf->pinned_apps[shelf->pinned_count - 1].app_id);
            free(shelf->pinned_apps[shelf->pinned_count - 1].name);
            shelf->pinned_count--;
            cfg->pinned_apps_count = shelf->pinned_count;
            goto cleanup;
        }
        cfg->pinned_apps_count = shelf->pinned_count;
    }

    /* Persist to shelf.json */
    {
        const char *config_path = get_config_path(path_buf, sizeof(path_buf));
        if (config_path) {
            int save_ret = zen_shelf_config_save(shelf->config, config_path);
            if (save_ret != 0) {
                wlr_log(WLR_INFO, "%s",
                        "shelf: pin_app: failed to save shelf.json");
            }
        } else {
            wlr_log(WLR_INFO, "%s",
                    "shelf: pin_app: HOME not set, cannot save shelf.json");
        }
    }

    zen_shelf_repaint(shelf);
    ret = 0;

cleanup:
    free(id_copy);
    free(name_copy);
    return ret;
}

int zen_shelf_unpin_app(struct ZenShelf *shelf, const char *app_id)
{
    int  ret = -1;
    char path_buf[512];

    if (!shelf || !app_id) goto cleanup;

    /* Find the index of app_id */
    int idx = -1;
    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) goto cleanup; /* not found */

    /* Free the entry's fields */
    free(shelf->pinned_apps[idx].app_id);
    free(shelf->pinned_apps[idx].name);
    free(shelf->pinned_apps[idx].icon_path);
    free(shelf->pinned_apps[idx].exec_cmd);

    /* Shift remaining entries down */
    int remaining = shelf->pinned_count - idx - 1;
    if (remaining > 0) {
        memmove(&shelf->pinned_apps[idx],
                &shelf->pinned_apps[idx + 1],
                (size_t)remaining * sizeof(struct ZenAppEntry));
    }
    shelf->pinned_count--;

    /* Update config's pinned_apps array */
    {
        ZenShelfConfig *cfg = shelf->config;

        /* Find and free the matching config entry */
        int cfg_idx = -1;
        for (int i = 0; i < cfg->pinned_apps_count; i++) {
            if (cfg->pinned_apps[i] &&
                strcmp(cfg->pinned_apps[i], app_id) == 0) {
                cfg_idx = i;
                break;
            }
        }
        if (cfg_idx >= 0) {
            free(cfg->pinned_apps[cfg_idx]);
            int cfg_remaining = cfg->pinned_apps_count - cfg_idx - 1;
            if (cfg_remaining > 0) {
                memmove(&cfg->pinned_apps[cfg_idx],
                        &cfg->pinned_apps[cfg_idx + 1],
                        (size_t)cfg_remaining * sizeof(char *));
            }
            cfg->pinned_apps_count--;
        }
    }

    /* Persist to shelf.json */
    {
        const char *config_path = get_config_path(path_buf, sizeof(path_buf));
        if (config_path) {
            int save_ret = zen_shelf_config_save(shelf->config, config_path);
            if (save_ret != 0) {
                wlr_log(WLR_INFO, "%s",
                        "shelf: unpin_app: failed to save shelf.json");
            }
        } else {
            wlr_log(WLR_INFO, "%s",
                    "shelf: unpin_app: HOME not set, cannot save shelf.json");
        }
    }

    zen_shelf_repaint(shelf);
    ret = 0;

cleanup:
    return ret;
}

/* ── Drag-to-pin ─────────────────────────────────────────────────────────── */

/*
 * Begin a drag operation for the given app_id.
 * Stores a copy of app_id so zen_shelf_drag_end() can pin it on drop.
 */
void zen_shelf_drag_begin(struct ZenShelf *shelf, const char *app_id)
{
    if (!shelf || !app_id) return;

    free(shelf->drag_app_id);
    shelf->drag_app_id = strdup(app_id);
    if (!shelf->drag_app_id) {
        wlr_log(WLR_ERROR, "%s", "shelf: drag_begin: strdup failed");
        shelf->drag_active = false;
        return;
    }

    shelf->drag_active = true;
    wlr_log(WLR_DEBUG, "shelf: drag_begin: app_id='%s'", app_id);
}

/*
 * End a drag at absolute screen coordinates (x, y).
 * Pins drag_app_id if the drop point is within the shelf bounds.
 * Clears drag state unconditionally.
 */
void zen_shelf_drag_end(struct ZenShelf *shelf, int x, int y)
{
    if (!shelf) return;

    if (!shelf->drag_active || !shelf->drag_app_id) {
        shelf->drag_active = false;
        free(shelf->drag_app_id);
        shelf->drag_app_id = NULL;
        return;
    }

    /* Check whether (x, y) is within shelf bounds. */
    bool on_shelf = (x >= shelf->x && x < shelf->x + shelf->width &&
                     y >= shelf->y && y < shelf->y + shelf->height);

    if (on_shelf) {
        wlr_log(WLR_DEBUG,
                "shelf: drag_end: drop at (%d, %d) — pinning '%s'",
                x, y, shelf->drag_app_id);
        zen_shelf_pin_app(shelf, shelf->drag_app_id);
    } else {
        wlr_log(WLR_DEBUG,
                "shelf: drag_end: drop at (%d, %d) outside shelf — no pin",
                x, y);
    }

    /* Clear drag state. */
    free(shelf->drag_app_id);
    shelf->drag_app_id = NULL;
    shelf->drag_active = false;
}

/*
 * Cancel an in-progress drag without pinning.
 * Clears drag state.
 */
void zen_shelf_drag_cancel(struct ZenShelf *shelf)
{
    if (!shelf) return;

    wlr_log(WLR_DEBUG, "%s", "shelf: drag_cancel");
    free(shelf->drag_app_id);
    shelf->drag_app_id = NULL;
    shelf->drag_active = false;
}

/*
 * Compute shelf geometry (x, y, width, height) for the given position and
 * output dimensions.  Results are written into the four out-parameters.
 */
static void shelf_compute_geometry(struct ZenShelf *shelf,
                                   ZenShelfPosition position,
                                   int out_w, int out_h,
                                   int *x, int *y, int *w, int *h)
{
    (void)shelf;
    switch (position) {
    case ZEN_SHELF_TOP:
        *x = 0;
        *y = 0;
        *w = out_w;
        *h = SHELF_HEIGHT;
        break;
    case ZEN_SHELF_LEFT:
        *x = 0;
        *y = 0;
        *w = SHELF_HEIGHT;
        *h = out_h;
        break;
    case ZEN_SHELF_RIGHT:
        *x = out_w - SHELF_HEIGHT;
        *y = 0;
        *w = SHELF_HEIGHT;
        *h = out_h;
        break;
    case ZEN_SHELF_BOTTOM:
    default:
        *x = 0;
        *y = out_h - SHELF_HEIGHT;
        *w = out_w;
        *h = SHELF_HEIGHT;
        break;
    }
}

int zen_shelf_apply_config(struct ZenShelf *shelf, const char *position,
                           bool auto_hide, int icon_size)
{
    char path_buf[512];

    if (!shelf || !position) return -1;

    /* Validate position string */
    ZenShelfPosition pos;
    if (strcmp(position, "bottom") == 0) {
        pos = ZEN_SHELF_BOTTOM;
    } else if (strcmp(position, "left") == 0) {
        pos = ZEN_SHELF_LEFT;
    } else if (strcmp(position, "right") == 0) {
        pos = ZEN_SHELF_RIGHT;
    } else if (strcmp(position, "top") == 0) {
        pos = ZEN_SHELF_TOP;
    } else {
        wlr_log(WLR_INFO,
                "shelf: apply_config: invalid position '%s'", position);
        return -1;
    }

    /* Update config fields */
    if (shelf->config) {
        shelf->config->position  = pos;
        shelf->config->auto_hide = auto_hide;
        shelf->config->icon_size = icon_size;
        zen_shelf_config_validate(shelf->config);

        /* Persist immediately */
        const char *config_path = get_config_path(path_buf, sizeof(path_buf));
        if (config_path) {
            int save_ret = zen_shelf_config_save(shelf->config, config_path);
            if (save_ret != 0) {
                wlr_log(WLR_INFO, "%s",
                        "shelf: apply_config: failed to save shelf.json");
            }
        } else {
            wlr_log(WLR_INFO, "%s",
                    "shelf: apply_config: HOME not set, cannot save shelf.json");
        }
    }

    /* Reposition: get output dimensions and recompute geometry */
    {
        struct ZenCompositor *compositor = shelf->shell->compositor;
        int out_w = 1920, out_h = 1080; /* safe defaults */

        struct wlr_box layout_box;
        wlr_output_layout_get_box(compositor->output_layout, NULL, &layout_box);
        if (layout_box.width > 0)  out_w = layout_box.width;
        if (layout_box.height > 0) out_h = layout_box.height;

        int new_x, new_y, new_w, new_h;
        shelf_compute_geometry(shelf, pos, out_w, out_h,
                               &new_x, &new_y, &new_w, &new_h);

        bool dims_changed = (new_w != shelf->width || new_h != shelf->height);

        shelf->x      = new_x;
        shelf->y      = new_y;
        shelf->width  = new_w;
        shelf->height = new_h;

        /* Move the scene node to the new position */
        if (shelf->bar_buffer && shelf->bar_buffer->scene_buffer) {
            wlr_scene_node_set_position(
                &shelf->bar_buffer->scene_buffer->node,
                shelf->x, shelf->y);
        }

        /* If dimensions changed, recreate the bar buffer */
        if (dims_changed && shelf->bar_buffer) {
            zen_cairo_buffer_destroy(shelf->bar_buffer);
            free(shelf->bar_buffer);
            shelf->bar_buffer = NULL;

            shelf->bar_buffer = calloc(1, sizeof(*shelf->bar_buffer));
            if (!shelf->bar_buffer) {
                wlr_log(WLR_ERROR, "%s",
                        "shelf: apply_config: failed to allocate bar_buffer");
                return -1;
            }

            struct wlr_scene_tree *parent =
                compositor->shelf_tree
                ? compositor->shelf_tree
                : compositor->shell_overlay_tree;

            if (zen_cairo_buffer_create(shelf->bar_buffer,
                                        compositor->renderer,
                                        parent,
                                        shelf->width, shelf->height) != 0) {
                wlr_log(WLR_ERROR, "%s",
                        "shelf: apply_config: zen_cairo_buffer_create failed");
                free(shelf->bar_buffer);
                shelf->bar_buffer = NULL;
                return -1;
            }

            /* Re-position the newly created buffer */
            wlr_scene_node_set_position(
                &shelf->bar_buffer->scene_buffer->node,
                shelf->x, shelf->y);
        }
    }

    zen_shelf_repaint(shelf);
    return 0;
}

void zen_shelf_update_running(struct ZenShelf *shelf, const char *app_id,
                              bool running)
{
    if (!shelf || !app_id) return;

    for (int i = 0; i < shelf->pinned_count; i++) {
        if (shelf->pinned_apps[i].app_id &&
            strcmp(shelf->pinned_apps[i].app_id, app_id) == 0) {
            shelf->pinned_apps[i].is_running = running;
        }
    }

    zen_shelf_repaint(shelf);
}

void zen_shelf_handle_click(struct ZenShelf *shelf, int x, int y)
{
    if (!shelf) return;

    /* Dismiss context menu if click is outside its bounds. */
    if (shelf->context_menu_visible) {
        int mx = shelf->context_menu_x;
        int my = shelf->context_menu_y - shelf->y; /* shelf-relative y */
        int mw = CONTEXT_MENU_WIDTH;
        int mh = CONTEXT_MENU_ITEM_COUNT * CONTEXT_MENU_ITEM_HEIGHT;

        bool inside = (x >= mx && x < mx + mw &&
                       y >= my && y < my + mh);
        if (!inside) {
            zen_shelf_hide_context_menu(shelf);
            return;
        }

        /* Determine which item was clicked by y offset within the menu. */
        int item_y = y - my;
        int item   = item_y / CONTEXT_MENU_ITEM_HEIGHT;

        /* Hide menu before acting so repaint sees the correct state. */
        zen_shelf_hide_context_menu(shelf);

        bool   cur_auto_hide = shelf->config ? shelf->config->auto_hide : false;
        int    cur_icon_size = shelf->config ? shelf->config->icon_size  : DEFAULT_ICON_SIZE;
        const char *cur_pos  = "bottom";
        if (shelf->config) {
            switch (shelf->config->position) {
            case ZEN_SHELF_TOP:   cur_pos = "top";    break;
            case ZEN_SHELF_LEFT:  cur_pos = "left";   break;
            case ZEN_SHELF_RIGHT: cur_pos = "right";  break;
            default:              cur_pos = "bottom";  break;
            }
        }

        /* Icon size cycle: 16 → 24 → 32 → 48 → 64 → 16 … */
        static const int icon_sizes[] = { 16, 24, 32, 48, 64 };
        static const int icon_sizes_count =
            (int)(sizeof(icon_sizes) / sizeof(icon_sizes[0]));

        switch (item) {
        case 0: /* Position: Bottom */
            zen_shelf_apply_config(shelf, "bottom", cur_auto_hide, cur_icon_size);
            break;
        case 1: /* Position: Left */
            zen_shelf_apply_config(shelf, "left", cur_auto_hide, cur_icon_size);
            break;
        case 2: /* Position: Right */
            zen_shelf_apply_config(shelf, "right", cur_auto_hide, cur_icon_size);
            break;
        case 3: /* Position: Top */
            zen_shelf_apply_config(shelf, "top", cur_auto_hide, cur_icon_size);
            break;
        case 4: /* Auto-hide: Toggle */
            zen_shelf_apply_config(shelf, cur_pos, !cur_auto_hide, cur_icon_size);
            break;
        case 5: { /* Icon Size: ± — cycle through sizes */
            int next_size = icon_sizes[0];
            for (int i = 0; i < icon_sizes_count; i++) {
                if (icon_sizes[i] == cur_icon_size) {
                    next_size = icon_sizes[(i + 1) % icon_sizes_count];
                    break;
                }
            }
            zen_shelf_apply_config(shelf, cur_pos, cur_auto_hide, next_size);
            break;
        }
        default:
            break;
        }
        return;
    }

    int icon_size = DEFAULT_ICON_SIZE;
    if (shelf->config && shelf->config->icon_size > 0)
        icon_size = shelf->config->icon_size;

    int slot_w = icon_size + ICON_SLOT_PADDING;
    int icon_y_top = (shelf->height - icon_size) / 2;
    int icon_y_bot = icon_y_top + icon_size;

    /* Reject clicks outside the icon row's vertical band. */
    if (y < icon_y_top || y >= icon_y_bot) return;

    /* Launcher button: click in the left margin area (x < ICON_LEFT_MARGIN + icon_size)
     * before the first pinned app slot toggles the app launcher overlay. */
    if (x < ICON_LEFT_MARGIN) {
        struct ZenShell *shell_mod = shelf->shell;
        if (shell_mod && shell_mod->launcher)
            zen_app_launcher_toggle(shell_mod->launcher);
        return;
    }

    /* Determine which icon slot was clicked. */
    int rel_x = x - ICON_LEFT_MARGIN;
    if (rel_x < 0) return;

    int slot = rel_x / slot_w;
    if (slot < 0 || slot >= shelf->pinned_count) return;

    /* Verify the click landed within the icon itself (not the padding). */
    int offset_in_slot = rel_x - slot * slot_w;
    if (offset_in_slot >= icon_size) return;

    struct ZenAppEntry   *entry      = &shelf->pinned_apps[slot];
    struct ZenShell      *shell_mod  = shelf->shell;
    struct ZenCompositor *compositor = shell_mod->compositor;

    if (!entry->is_running) {
        /* ── Launch path ─────────────────────────────────────────────── */
        if (!entry->exec_cmd) {
            wlr_log(WLR_INFO,
                    "shelf: no exec_cmd for app '%s', cannot launch",
                    entry->app_id ? entry->app_id : "(null)");
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            wlr_log(WLR_ERROR, "shelf: fork() failed for app '%s'",
                    entry->app_id ? entry->app_id : "(null)");
            return;
        }

        if (pid == 0) {
            /* Child: redirect stdin/stdout/stderr to /dev/null. */
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO)
                    close(devnull);
            }

            /* Build argv: split exec_cmd on spaces (simple tokenisation). */
            char *cmd_copy = strdup(entry->exec_cmd);
            if (!cmd_copy) _exit(127);

            /* Count tokens. */
            int    argc = 0;
            char  *p    = cmd_copy;
            while (*p) {
                while (*p == ' ') p++;
                if (*p) { argc++; while (*p && *p != ' ') p++; }
            }

            char **argv = calloc((size_t)(argc + 1), sizeof(char *));
            if (!argv) { free(cmd_copy); _exit(127); }

            int i = 0;
            p = cmd_copy;
            while (*p) {
                while (*p == ' ') p++;
                if (*p) {
                    argv[i++] = p;
                    while (*p && *p != ' ') p++;
                    if (*p) *p++ = '\0';
                }
            }
            argv[i] = NULL;

            execvp(argv[0], argv);
            /* execvp only returns on error. */
            _exit(127);
        }

        /* Parent: child is running; we do not wait (fire-and-forget). */
        wlr_log(WLR_INFO, "shelf: launched '%s' (pid %d)",
                entry->app_id ? entry->app_id : "(null)", (int)pid);

    } else {
        /* ── Focus path ──────────────────────────────────────────────── */
        /*
         * compositor->toplevels is MRU-ordered (head = most recently used).
         * Find the first toplevel whose app_id matches this entry.
         */
        struct ZenToplevel *target = NULL;
        struct ZenToplevel *tl;

        wl_list_for_each(tl, &compositor->toplevels, link) {
            if (tl->app_id && entry->app_id &&
                strcmp(tl->app_id, entry->app_id) == 0) {
                target = tl;
                break;
            }
        }

        if (target) {
            zen_xdg_focus_toplevel(compositor, target);
            wlr_log(WLR_DEBUG, "shelf: focused toplevel for app '%s'",
                    entry->app_id ? entry->app_id : "(null)");
        } else {
            wlr_log(WLR_INFO,
                    "shelf: is_running=true but no toplevel found for '%s'",
                    entry->app_id ? entry->app_id : "(null)");
        }
    }
}

/* ── Context Menu ────────────────────────────────────────────────────────── */

/* Menu item labels (display only; selection handled in task 2.4.3). */
static const char *const CONTEXT_MENU_ITEMS[CONTEXT_MENU_ITEM_COUNT] = {
    "Position: Bottom",
    "Position: Left",
    "Position: Right",
    "Position: Top",
    "Auto-hide: Toggle",
    "Icon Size: \xc2\xb1",   /* ± in UTF-8 */
};

/*
 * Draw a rounded rectangle path on cr.
 * (x, y) is the top-left corner; w and h are width and height; r is the
 * corner radius.
 */
static void cairo_rounded_rect(cairo_t *cr,
                                double x, double y,
                                double w, double h,
                                double r)
{
    double x1 = x + w;
    double y1 = y + h;

    cairo_move_to(cr, x + r, y);
    cairo_line_to(cr, x1 - r, y);
    cairo_arc(cr, x1 - r, y + r,  r, -3.14159265358979323846 / 2.0, 0.0);
    cairo_line_to(cr, x1, y1 - r);
    cairo_arc(cr, x1 - r, y1 - r, r,  0.0,         3.14159265358979323846 / 2.0);
    cairo_line_to(cr, x + r, y1);
    cairo_arc(cr, x + r,  y1 - r, r,  3.14159265358979323846 / 2.0,  3.14159265358979323846);
    cairo_line_to(cr, x, y + r);
    cairo_arc(cr, x + r,  y + r,  r,  3.14159265358979323846,        3.0 * 3.14159265358979323846 / 2.0);
    cairo_close_path(cr);
}

void zen_shelf_show_context_menu(struct ZenShelf *shelf)
{
    if (!shelf) return;
    if (shelf->context_menu_visible) return;

    struct ZenShell      *shell_mod  = shelf->shell;
    struct ZenCompositor *compositor = shell_mod->compositor;

    int menu_w = CONTEXT_MENU_WIDTH;
    int menu_h = CONTEXT_MENU_ITEM_COUNT * CONTEXT_MENU_ITEM_HEIGHT;

    /* Allocate and create the context menu buffer. */
    shelf->context_menu_buf = calloc(1, sizeof(*shelf->context_menu_buf));
    if (!shelf->context_menu_buf) {
        wlr_log(WLR_ERROR, "%s", "shelf: context_menu: calloc failed");
        return;
    }

    struct wlr_scene_tree *parent = compositor->context_menu_tree
                                    ? compositor->context_menu_tree
                                    : compositor->shell_overlay_tree;

    if (zen_cairo_buffer_create(shelf->context_menu_buf,
                                compositor->renderer,
                                parent,
                                menu_w, menu_h) != 0) {
        wlr_log(WLR_ERROR, "%s", "shelf: context_menu: buffer create failed");
        free(shelf->context_menu_buf);
        shelf->context_menu_buf = NULL;
        return;
    }

    /* Position: just above the shelf at the stored click x, clamped to screen. */
    int mx = shelf->context_menu_x;
    int my = shelf->y - menu_h;

    /* Clamp horizontally so the menu stays on screen. */
    if (mx + menu_w > shelf->width) {
        mx = shelf->width - menu_w;
    }
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;

    /* Update stored position to the clamped value. */
    shelf->context_menu_x = mx;
    shelf->context_menu_y = my;

    wlr_scene_node_set_position(&shelf->context_menu_buf->scene_buffer->node,
                                mx, my);

    /* ── Render ──────────────────────────────────────────────────────── */
    const struct ZenColorPalette *palette =
        zen_theme_config_get_palette(shell_mod->theme);

    cairo_t *cr = shelf->context_menu_buf->cr;

    /* Clear to transparent. */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);

    /* Background: rounded rect in surface color. */
    cairo_save(cr);
    cairo_rounded_rect(cr, 0.0, 0.0,
                       (double)menu_w, (double)menu_h,
                       CONTEXT_MENU_CORNER_R);
    cairo_set_source_rgba(cr,
                          (double)palette->surface[0],
                          (double)palette->surface[1],
                          (double)palette->surface[2],
                          (double)palette->surface[3]);
    cairo_fill_preserve(cr);

    /* Border. */
    cairo_set_source_rgba(cr,
                          (double)palette->border[0],
                          (double)palette->border[1],
                          (double)palette->border[2],
                          (double)palette->border[3]);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    /* Item labels. */
    PangoLayout          *layout    = pango_cairo_create_layout(cr);
    PangoFontDescription *font_desc =
        pango_font_description_from_string(CONTEXT_MENU_FONT);
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    cairo_set_source_rgba(cr,
                          (double)palette->foreground[0],
                          (double)palette->foreground[1],
                          (double)palette->foreground[2],
                          (double)palette->foreground[3]);

    for (int i = 0; i < CONTEXT_MENU_ITEM_COUNT; i++) {
        pango_layout_set_text(layout, CONTEXT_MENU_ITEMS[i], -1);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);

        int item_y = i * CONTEXT_MENU_ITEM_HEIGHT;
        int text_y = item_y + (CONTEXT_MENU_ITEM_HEIGHT - th) / 2;

        cairo_save(cr);
        cairo_move_to(cr, (double)CONTEXT_MENU_TEXT_PAD, (double)text_y);
        pango_cairo_show_layout(cr, layout);
        cairo_restore(cr);

        /* Separator line between items (skip after last). */
        if (i < CONTEXT_MENU_ITEM_COUNT - 1) {
            double sep_y = (double)(item_y + CONTEXT_MENU_ITEM_HEIGHT) - 0.5;
            cairo_save(cr);
            cairo_set_source_rgba(cr,
                                  (double)palette->border[0],
                                  (double)palette->border[1],
                                  (double)palette->border[2],
                                  (double)palette->border[3] * 0.5);
            cairo_set_line_width(cr, 0.5);
            cairo_move_to(cr, (double)CONTEXT_MENU_TEXT_PAD, sep_y);
            cairo_line_to(cr, (double)(menu_w - CONTEXT_MENU_TEXT_PAD), sep_y);
            cairo_stroke(cr);
            cairo_restore(cr);
        }
    }

    g_object_unref(layout);

    zen_cairo_buffer_submit(shelf->context_menu_buf);

    shelf->context_menu_visible = true;
    zen_shell_testctl_sync(shelf->shell);
    wlr_log(WLR_DEBUG, "shelf: context menu shown at (%d, %d)", mx, my);
}

void zen_shelf_hide_context_menu(struct ZenShelf *shelf)
{
    if (!shelf) return;
    if (!shelf->context_menu_visible) return;

    if (shelf->context_menu_buf) {
        zen_cairo_buffer_destroy(shelf->context_menu_buf);
        free(shelf->context_menu_buf);
        shelf->context_menu_buf = NULL;
    }

    shelf->context_menu_visible = false;
    zen_shell_testctl_sync(shelf->shell);
    wlr_log(WLR_DEBUG, "%s", "shelf: context menu hidden");
}

void zen_shelf_handle_right_click(struct ZenShelf *shelf, int x, int y)
{
    if (!shelf) return;

    if (shelf->context_menu_visible) {
        zen_shelf_hide_context_menu(shelf);
    } else {
        shelf->context_menu_x = x;
        shelf->context_menu_y = y;
        zen_shelf_show_context_menu(shelf);
    }
}
