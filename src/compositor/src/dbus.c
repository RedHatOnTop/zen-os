/*
 * Zen OS Compositor — D-Bus Interface Implementation
 *
 * Registers org.zenos.Compositor on the session bus and integrates
 * the sd-bus fd into the wl_event_loop via wl_event_loop_add_fd().
 *
 * All method handlers run on the Wayland event loop thread, avoiding
 * race conditions with compositor state.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <systemd/sd-bus.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "zen/compositor.h"
#include "zen/dbus.h"
#include "zen/xdg.h"
#include "zen/dbus-errors.h"

/* ── Method Handlers ─────────────────────────────────────────────────────── */

/*
 * LaunchApp — fork+execvp the given app_id.
 * Signature: s -> b
 */
static int handle_launch_app(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error) {
    (void)userdata;

    const char *app_id = NULL;
    int ret = sd_bus_message_read(msg, "s", &app_id);
    if (ret < 0) {
        return ret;
    }

    if (!app_id || app_id[0] == '\0') {
        sd_bus_error_set(error, "org.zenos.Error.InvalidArgument",
                         "app_id must not be empty");
        return -EINVAL;
    }

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: exec the application. */
        char *argv[] = { (char *)app_id, NULL };
        execvp(app_id, argv);
        /* execvp only returns on error. */
        _exit(127);
    } else if (pid < 0) {
        wlr_log(WLR_ERROR, "LaunchApp: fork failed for '%s'", app_id);
        sd_bus_error_set(error, "org.zenos.Error.Internal",
                         "fork failed");
        return -errno;
    }

    wlr_log(WLR_INFO, "LaunchApp: launched '%s' (pid %d)", app_id, (int)pid);
    return sd_bus_reply_method_return(msg, "b", 1);
}

/*
 * GetRunningApps — return array of app_id strings from toplevels list.
 * Signature: (none) -> as
 */
static int handle_get_running_apps(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error) {
    (void)error;
    struct ZenCompositor *compositor = userdata;

    sd_bus_message *reply = NULL;
    int ret = sd_bus_message_new_method_return(msg, &reply);
    if (ret < 0) {
        return ret;
    }

    ret = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "s");
    if (ret < 0) {
        sd_bus_message_unref(reply);
        return ret;
    }

    struct ZenToplevel *toplevel;
    wl_list_for_each(toplevel, &compositor->toplevels, link) {
        const char *id = toplevel->app_id ? toplevel->app_id : "";
        ret = sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, id);
        if (ret < 0) {
            sd_bus_message_unref(reply);
            return ret;
        }
    }

    ret = sd_bus_message_close_container(reply);
    if (ret < 0) {
        sd_bus_message_unref(reply);
        return ret;
    }

    ret = sd_bus_send(NULL, reply, NULL);
    sd_bus_message_unref(reply);
    return ret;
}

/*
 * PinToShelf — pin an application to the shelf.
 * Signature: s -> b
 */
static int handle_pin_to_shelf(sd_bus_message *msg, void *userdata,
                                sd_bus_error *error) {
    (void)userdata;

    const char *app_id = NULL;
    int ret = sd_bus_message_read(msg, "s", &app_id);
    if (ret < 0) {
        return ret;
    }

    if (!app_id || app_id[0] == '\0') {
        sd_bus_error_set(error, "org.zenos.Error.InvalidArgument",
                         "app_id must not be empty");
        return -EINVAL;
    }

    wlr_log(WLR_INFO, "PinToShelf: %s", app_id);
    return sd_bus_reply_method_return(msg, "b", 1);
}

/*
 * UnpinFromShelf — unpin an application from the shelf.
 * Signature: s -> b
 */
static int handle_unpin_from_shelf(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error) {
    (void)userdata;

    const char *app_id = NULL;
    int ret = sd_bus_message_read(msg, "s", &app_id);
    if (ret < 0) {
        return ret;
    }

    if (!app_id || app_id[0] == '\0') {
        sd_bus_error_set(error, "org.zenos.Error.InvalidArgument",
                         "app_id must not be empty");
        return -EINVAL;
    }

    wlr_log(WLR_INFO, "UnpinFromShelf: %s", app_id);
    return sd_bus_reply_method_return(msg, "b", 1);
}

/*
 * SetShelfConfig — apply shelf configuration.
 * Signature: sbi -> b
 */
static int handle_set_shelf_config(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error) {
    (void)userdata;

    const char *position = NULL;
    int auto_hide = 0;
    int32_t icon_size = 0;

    int ret = sd_bus_message_read(msg, "sbi", &position, &auto_hide, &icon_size);
    if (ret < 0) {
        return ret;
    }

    if (icon_size <= 0) {
        sd_bus_error_set(error, "org.zenos.Error.InvalidArgument",
                         "icon_size must be greater than 0");
        return -EINVAL;
    }

    wlr_log(WLR_INFO, "SetShelfConfig: position=%s auto_hide=%d icon_size=%d",
            position ? position : "(null)", auto_hide, (int)icon_size);
    return sd_bus_reply_method_return(msg, "b", 1);
}

/*
 * ToggleDarkMode — set dark mode state.
 * Signature: b -> b
 */
static int handle_toggle_dark_mode(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error) {
    (void)error;
    struct ZenCompositor *compositor = userdata;

    int enabled = 0;
    int ret = sd_bus_message_read(msg, "b", &enabled);
    if (ret < 0) {
        return ret;
    }

    compositor->dark_mode = (bool)enabled;
    wlr_log(WLR_INFO, "ToggleDarkMode: dark_mode=%s",
            compositor->dark_mode ? "true" : "false");

    return sd_bus_reply_method_return(msg, "b", (int)compositor->dark_mode);
}

/*
 * ShowNotification — display a notification toast.
 * Signature: ssssii -> i
 */
static int handle_show_notification(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error) {
    (void)userdata;

    const char *app_id = NULL;
    const char *summary = NULL;
    const char *body = NULL;
    const char *icon = NULL;
    int32_t urgency = 0;
    int32_t timeout_ms = 0;

    int ret = sd_bus_message_read(msg, "ssssii",
                                   &app_id, &summary, &body, &icon,
                                   &urgency, &timeout_ms);
    if (ret < 0) {
        return ret;
    }

    if (urgency < 0 || urgency > 2) {
        sd_bus_error_set(error, "org.zenos.Error.InvalidArgument",
                         "urgency must be in range [0, 2]");
        return -EINVAL;
    }

    wlr_log(WLR_INFO,
            "ShowNotification: app_id=%s summary=%s urgency=%d timeout_ms=%d",
            app_id ? app_id : "", summary ? summary : "",
            (int)urgency, (int)timeout_ms);

    /* Return notif_id = 1 (stub implementation). */
    return sd_bus_reply_method_return(msg, "i", 1);
}

/* ── D-Bus vtable ────────────────────────────────────────────────────────── */

static const sd_bus_vtable compositor_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("LaunchApp",        "s",      "b",  handle_launch_app,        0),
    SD_BUS_METHOD("GetRunningApps",   "",       "as", handle_get_running_apps,  0),
    SD_BUS_METHOD("PinToShelf",       "s",      "b",  handle_pin_to_shelf,      0),
    SD_BUS_METHOD("UnpinFromShelf",   "s",      "b",  handle_unpin_from_shelf,  0),
    SD_BUS_METHOD("SetShelfConfig",   "sbi",    "b",  handle_set_shelf_config,  0),
    SD_BUS_METHOD("ToggleDarkMode",   "b",      "b",  handle_toggle_dark_mode,  0),
    SD_BUS_METHOD("ShowNotification", "ssssii", "i",  handle_show_notification, 0),
    SD_BUS_VTABLE_END,
};

/* ── Event loop integration ──────────────────────────────────────────────── */

/*
 * Called by wl_event_loop when the sd-bus fd becomes readable.
 * Dispatches pending D-Bus messages on the Wayland event loop thread.
 */
static int dbus_dispatch_cb(int fd, uint32_t mask, void *data) {
    (void)fd;
    (void)mask;
    struct ZenCompositor *compositor = data;
    sd_bus_process(compositor->bus, NULL);
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int zen_dbus_init(struct ZenCompositor *compositor) {
    int ret = -1;

    /* 1. Open session bus connection. */
    ret = sd_bus_open_user(&compositor->bus);
    if (ret < 0) {
        wlr_log(WLR_ERROR, "zen_dbus_init: sd_bus_open_user failed: %s",
                strerror(-ret));
        goto cleanup;
    }

    /* 2. Register object with vtable. */
    ret = sd_bus_add_object_vtable(compositor->bus,
                                    &compositor->dbus_slot,
                                    "/org/zenos/Compositor",
                                    "org.zenos.Compositor",
                                    compositor_vtable,
                                    compositor);
    if (ret < 0) {
        wlr_log(WLR_ERROR,
                "zen_dbus_init: sd_bus_add_object_vtable failed: %s",
                strerror(-ret));
        goto cleanup;
    }

    /* 3. Request well-known bus name. */
    ret = sd_bus_request_name(compositor->bus, "org.zenos.Compositor", 0);
    if (ret < 0) {
        wlr_log(WLR_ERROR,
                "zen_dbus_init: sd_bus_request_name failed: %s",
                strerror(-ret));
        goto cleanup;
    }

    /* 4. Get the sd-bus file descriptor. */
    int fd = sd_bus_get_fd(compositor->bus);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "zen_dbus_init: sd_bus_get_fd failed: %s",
                strerror(-fd));
        ret = fd;
        goto cleanup;
    }

    /* 5. Integrate into the Wayland event loop. */
    struct wl_event_loop *loop =
        wl_display_get_event_loop(compositor->wl_display);
    compositor->dbus_event_source =
        wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
                              dbus_dispatch_cb, compositor);
    if (!compositor->dbus_event_source) {
        wlr_log(WLR_ERROR, "%s",
                "zen_dbus_init: wl_event_loop_add_fd failed");
        ret = -1;
        goto cleanup;
    }

    wlr_log(WLR_INFO, "%s",
            "D-Bus: org.zenos.Compositor registered on session bus");
    ret = 0;
    return ret;

cleanup:
    zen_dbus_destroy(compositor);
    return ret;
}

void zen_dbus_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    if (compositor->dbus_event_source) {
        wl_event_source_remove(compositor->dbus_event_source);
        compositor->dbus_event_source = NULL;
    }

    if (compositor->dbus_slot) {
        sd_bus_slot_unref(compositor->dbus_slot);
        compositor->dbus_slot = NULL;
    }

    if (compositor->bus) {
        sd_bus_unref(compositor->bus);
        compositor->bus = NULL;
    }
}
