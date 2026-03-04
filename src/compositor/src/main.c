/*
 * Zen OS Compositor — Entry Point
 *
 * Minimal wlroots + SceneFX compositor that initializes the Wayland display,
 * creates the SceneFX-enhanced renderer, configures outputs, and renders
 * a solid-color frame.
 *
 * Architecture: In-process shell (Option A).  The desktop shell will be
 * integrated into this binary in future sub-phases.
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "zen/compositor.h"

/* Zen OS brand color: deep navy (#1a1a2e) */
static const float ZEN_CLEAR_COLOR[4] = {
    0.102f, 0.102f, 0.180f, 1.0f
};

/* ── Output Handlers ─────────────────────────────────────────────────────── */

static void output_handle_frame(struct wl_listener *listener,
                                void *data __attribute__((unused))) {
    struct ZenOutput *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_handle_request_state(struct wl_listener *listener,
                                         void *data) {
    struct ZenOutput *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_handle_destroy(struct wl_listener *listener,
                                  void *data __attribute__((unused))) {
    struct ZenOutput *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    free(output);
}

/* ── New Output ──────────────────────────────────────────────────────────── */

static void handle_new_output(struct wl_listener *listener, void *data) {
    struct ZenCompositor *compositor =
        wl_container_of(listener, compositor, new_output);
    struct wlr_output *wlr_output = data;

    /* Initialize rendering on this output. */
    wlr_output_init_render(wlr_output, compositor->allocator,
                           compositor->renderer);

    /* Set preferred mode. */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    /* Allocate per-output state. */
    struct ZenOutput *output = calloc(1, sizeof(*output));
    if (!output) {
        wlr_log(WLR_ERROR, "%s", "Failed to allocate ZenOutput");
        return;
    }

    output->wlr_output = wlr_output;
    output->compositor = compositor;

    /* Register output event listeners. */
    output->frame.notify = output_handle_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_handle_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    /* Add output to layout and scene. */
    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add_auto(compositor->output_layout, wlr_output);
    output->scene_output =
        wlr_scene_output_create(compositor->scene, wlr_output);
    wlr_scene_output_layout_add_output(compositor->scene_layout,
                                        l_output, output->scene_output);

    wlr_log(WLR_INFO, "Output configured: %s (%dx%d)",
            wlr_output->name,
            wlr_output->width, wlr_output->height);
}

/* ── Boot Signal ─────────────────────────────────────────────────────────── */

/*
 * Emit ZEN_BOOT_OK to serial port.  This signal tells zen-test-cli that
 * the compositor has fully initialized and is about to enter the event loop.
 *
 * IMPORTANT: This MUST be called from the compositor binary, NOT from the
 * init script.  This guarantees the signal only fires when the compositor
 * is actually running and rendering.
 */
static void emit_boot_signal(void) {
    FILE *serial = fopen("/dev/ttyS0", "w");
    if (serial) {
        fprintf(serial, "ZEN_BOOT_OK\n");
        fflush(serial);
        fclose(serial);
        wlr_log(WLR_INFO, "%s", "Boot signal emitted: ZEN_BOOT_OK");
    } else {
        /* Not fatal — serial port may not exist outside QEMU. */
        wlr_log(WLR_DEBUG, "%s", "Could not open /dev/ttyS0 (not in QEMU?)");
    }
}

/* ── Compositor Lifecycle ────────────────────────────────────────────────── */

int zen_compositor_create(struct ZenCompositor *compositor) {
    int ret = -1;

    memset(compositor, 0, sizeof(*compositor));
    memcpy(compositor->clear_color, ZEN_CLEAR_COLOR,
           sizeof(ZEN_CLEAR_COLOR));

    wlr_log_init(WLR_DEBUG, NULL);

    /* 1. Wayland display */
    compositor->wl_display = wl_display_create();
    if (!compositor->wl_display) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wl_display");
        goto cleanup;
    }

    /* 2. Backend (headless in QEMU, DRM on real hardware) */
    compositor->backend =
        wlr_backend_autocreate(wl_display_get_event_loop(compositor->wl_display),
                               NULL);
    if (!compositor->backend) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_backend");
        goto cleanup;
    }

    /* 3. Renderer — SceneFX fx_renderer via wlr_renderer_autocreate.
     *
     * When SceneFX is linked, wlr_renderer_autocreate() returns an
     * fx_renderer instance that supports blur/shadow/rounded-corner
     * effects.  SceneFX overrides the renderer factory at link time.
     */
    compositor->renderer = wlr_renderer_autocreate(compositor->backend);
    if (!compositor->renderer) {
        wlr_log(WLR_ERROR, "%s", "Failed to create renderer");
        goto cleanup;
    }
    wlr_renderer_init_wl_display(compositor->renderer,
                                  compositor->wl_display);

    /* 4. Allocator */
    compositor->allocator =
        wlr_allocator_autocreate(compositor->backend,
                                  compositor->renderer);
    if (!compositor->allocator) {
        wlr_log(WLR_ERROR, "%s", "Failed to create allocator");
        goto cleanup;
    }

    /* 5. Scene graph */
    compositor->scene = wlr_scene_create();
    if (!compositor->scene) {
        wlr_log(WLR_ERROR, "%s", "Failed to create scene");
        goto cleanup;
    }

    /* 6. Output layout */
    compositor->output_layout =
        wlr_output_layout_create(compositor->wl_display);
    if (!compositor->output_layout) {
        wlr_log(WLR_ERROR, "%s", "Failed to create output layout");
        goto cleanup;
    }

    /* 7. Bind scene to output layout */
    compositor->scene_layout =
        wlr_scene_attach_output_layout(compositor->scene,
                                        compositor->output_layout);
    if (!compositor->scene_layout) {
        wlr_log(WLR_ERROR, "%s", "Failed to attach scene to output layout");
        goto cleanup;
    }

    /* Set background color on the scene tree root. */
    /* The clear color is set per-output in the frame handler via the
     * scene graph — wlr_scene handles clearing. For now, the default
     * black is overridden by setting a scene rect as background. */
    {
        struct wlr_scene_rect *bg = wlr_scene_rect_create(
            &compositor->scene->tree, 0, 0, compositor->clear_color);
        if (bg) {
            /* The rect will be resized to fill each output in the frame
             * handler once we know the output dimensions. For now, use
             * a large default. */
            wlr_scene_rect_set_size(bg, 8192, 8192);
        }
    }

    /* 8. Listen for new outputs */
    compositor->new_output.notify = handle_new_output;
    wl_signal_add(&compositor->backend->events.new_output,
                  &compositor->new_output);

    /* 9. Start the backend */
    if (!wlr_backend_start(compositor->backend)) {
        wlr_log(WLR_ERROR, "%s", "Failed to start backend");
        goto cleanup;
    }

    wlr_log(WLR_INFO, "%s", "Zen OS Compositor initialized successfully");
    ret = 0;

cleanup:
    if (ret != 0) {
        zen_compositor_destroy(compositor);
    }
    return ret;
}

void zen_compositor_run(struct ZenCompositor *compositor) {
    /* Emit boot signal BEFORE entering the event loop.
     * This tells zen-test-cli that initialization is complete. */
    emit_boot_signal();

    const char *socket = wl_display_add_socket_auto(compositor->wl_display);
    if (socket) {
        wlr_log(WLR_INFO, "Wayland socket: %s", socket);
        setenv("WAYLAND_DISPLAY", socket, true);
    }

    wlr_log(WLR_INFO, "%s", "Entering event loop");
    wl_display_run(compositor->wl_display);
}

void zen_compositor_destroy(struct ZenCompositor *compositor) {
    if (!compositor) {
        return;
    }

    /* Destroy in reverse creation order. */
    if (compositor->wl_display) {
        wl_display_destroy_clients(compositor->wl_display);
    }
    if (compositor->scene) {
        wlr_scene_node_destroy(&compositor->scene->tree.node);
        compositor->scene = NULL;
    }
    if (compositor->allocator) {
        wlr_allocator_destroy(compositor->allocator);
        compositor->allocator = NULL;
    }
    if (compositor->renderer) {
        wlr_renderer_destroy(compositor->renderer);
        compositor->renderer = NULL;
    }
    if (compositor->backend) {
        wlr_backend_destroy(compositor->backend);
        compositor->backend = NULL;
    }
    if (compositor->wl_display) {
        wl_display_destroy(compositor->wl_display);
        compositor->wl_display = NULL;
    }

    wlr_log(WLR_INFO, "%s", "Compositor destroyed");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    struct ZenCompositor compositor;

    if (zen_compositor_create(&compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize compositor");
        return 1;
    }

    zen_compositor_run(&compositor);
    zen_compositor_destroy(&compositor);

    return 0;
}
