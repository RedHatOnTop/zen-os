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
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <scenefx/render/fx_renderer/fx_renderer.h>

#include "zen/compositor.h"
#include "zen/dbus.h"
#include "zen/input.h"
#include "zen/keybinds.h"
#include "zen/layer.h"
#include "zen/lock.h"
#include "zen/xdg.h"
#include "zen/cairo_buffer.h"
#include "zen/wallpaper.h"

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

    /* Apply default scale (1.0); per-output HiDPI scale can be set here
     * once a config system is available (Req 12.5). */
    wlr_output_state_set_scale(&state, 1.0f);

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

    /* Render wallpaper for this output (Req 6.1, task 1.7.6). */
    if (compositor->wallpaper_tree) {
        zen_wallpaper_render_output(compositor, output);
    }

    /* Create "Zen OS" test overlay on the first output only (Req 5.3). */
    if (!compositor->test_overlay && compositor->shell_overlay_tree) {
        int ow = wlr_output->width;
        int oh = wlr_output->height;

        if (ow > 0 && oh > 0) {
            /* Overlay dimensions: full-width strip at bottom, 48px tall. */
            const int overlay_h = 48;
            const int overlay_w = ow;

            compositor->test_overlay = calloc(1, sizeof(*compositor->test_overlay));
            if (compositor->test_overlay) {
                if (zen_cairo_buffer_create(compositor->test_overlay,
                                            compositor->renderer,
                                            compositor->shell_overlay_tree,
                                            overlay_w, overlay_h) == 0) {
                    cairo_t *cr = compositor->test_overlay->cr;
                    PangoLayout *layout =
                        zen_cairo_buffer_get_pango(compositor->test_overlay);

                    /* Clear to transparent. */
                    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
                    cairo_paint(cr);

                    /* Set up Pango font. */
                    PangoFontDescription *font_desc =
                        pango_font_description_from_string("Sans Bold 18");
                    pango_layout_set_font_description(layout, font_desc);
                    pango_font_description_free(font_desc);

                    pango_layout_set_text(layout, "Zen OS", -1);

                    /* Measure text to center it horizontally. */
                    int text_w = 0, text_h = 0;
                    pango_layout_get_pixel_size(layout, &text_w, &text_h);

                    int x = (overlay_w - text_w) / 2;
                    int y = (overlay_h - text_h) / 2;

                    /* Render white text with slight shadow for visibility. */
                    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
                    cairo_move_to(cr, x + 1, y + 1);
                    pango_cairo_show_layout(cr, layout);

                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
                    cairo_move_to(cr, x, y);
                    pango_cairo_show_layout(cr, layout);

                    zen_cairo_buffer_submit(compositor->test_overlay);

                    /* Position overlay at bottom-center of the output. */
                    wlr_scene_node_set_position(
                        &compositor->test_overlay->scene_buffer->node,
                        0, oh - overlay_h);

                    wlr_log(WLR_INFO,
                            "Test overlay created: 'Zen OS' at (%d,%d) on %s",
                            x, oh - overlay_h, wlr_output->name);
                } else {
                    wlr_log(WLR_ERROR, "%s",
                            "Failed to create test overlay cairo buffer");
                    free(compositor->test_overlay);
                    compositor->test_overlay = NULL;
                }
            }
        }
    }

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

    /* 3. Renderer — SceneFX fx_renderer via fx_renderer_create().
     *
     * Use SceneFX's own renderer creation function rather than
     * wlr_renderer_autocreate(). fx_renderer_create() wraps the GLES2
     * renderer in the fx_renderer type that SceneFX requires internally.
     * Using wlr_renderer_autocreate() returns a raw wlroots GLES2 renderer
     * which fails the wlr_renderer_is_fx() check inside SceneFX.
     */
    compositor->renderer = fx_renderer_create(compositor->backend);
    if (!compositor->renderer) {
        wlr_log(WLR_ERROR, "%s", "Failed to create fx_renderer — "
                 "ensure Mesa (llvmpipe or GPU) is installed");
        goto cleanup;
    }

    wlr_renderer_init_wl_display(compositor->renderer,
                                  compositor->wl_display);

    /* 4. wl_compositor + wl_subcompositor globals (Req 1) */
    compositor->wlr_comp =
        wlr_compositor_create(compositor->wl_display, 5,
                              compositor->renderer);
    if (!compositor->wlr_comp) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_compositor");
        goto cleanup;
    }

    wlr_subcompositor_create(compositor->wl_display);

    /* 5. Allocator */
    compositor->allocator =
        wlr_allocator_autocreate(compositor->backend,
                                  compositor->renderer);
    if (!compositor->allocator) {
        wlr_log(WLR_ERROR, "%s", "Failed to create allocator");
        goto cleanup;
    }

    /* 6. Scene graph */
    compositor->scene = wlr_scene_create();
    if (!compositor->scene) {
        wlr_log(WLR_ERROR, "%s", "Failed to create scene");
        goto cleanup;
    }

    /* 7. Output layout */
    compositor->output_layout =
        wlr_output_layout_create(compositor->wl_display);
    if (!compositor->output_layout) {
        wlr_log(WLR_ERROR, "%s", "Failed to create output layout");
        goto cleanup;
    }

    /* 8. Bind scene to output layout */
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

    /* 9. Listen for new outputs */
    compositor->new_output.notify = handle_new_output;
    wl_signal_add(&compositor->backend->events.new_output,
                  &compositor->new_output);

    /* 10. Start the backend */
    if (!wlr_backend_start(compositor->backend)) {
        wlr_log(WLR_ERROR, "%s", "Failed to start backend");
        goto cleanup;
    }

    /* 11. XDG shell (Req 2) */
    if (zen_xdg_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize XDG shell");
        goto cleanup;
    }

    /* 12. Input routing — seat, cursor, xcursor, new_input listener (Req 3) */
    if (zen_input_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize input module");
        goto cleanup;
    }

    /* 13. Shell overlay tree — plain wlr_scene_tree for Cairo overlays (Req 5).
     *     The test overlay ZenCairoBuffer is created in handle_new_output
     *     once we know the primary output dimensions. */
    compositor->shell_overlay_tree =
        wlr_scene_tree_create(&compositor->scene->tree);
    if (!compositor->shell_overlay_tree) {
        wlr_log(WLR_ERROR, "%s", "Failed to create shell_overlay_tree");
        goto cleanup;
    }

    /* 14. Wallpaper (Req 6) — must be initialized after wallpaper_tree is
     *     created inside zen_wallpaper_init(). Actual per-output rendering
     *     happens in handle_new_output() via zen_wallpaper_render_output(). */
    if (zen_wallpaper_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize wallpaper module");
        goto cleanup;
    }

    /* 15. Keybinding registry (Req 7) — default bindings + optional JSON config. */
    if (zen_keybinds_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize keybinding registry");
        goto cleanup;
    }

    /* 16. Layer shell protocol (Req 8) — wlr-layer-shell-v1 for panels/overlays. */
    if (zen_layer_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize layer shell");
        goto cleanup;
    }

    /* 17. D-Bus interface (Req 9) — org.zenos.Compositor on session bus. */
    if (zen_dbus_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize D-Bus interface");
        goto cleanup;
    }

    /* 18. Screen lock manager (Req 11) */
    if (zen_lock_init(compositor) != 0) {
        wlr_log(WLR_ERROR, "%s", "Failed to initialize lock module");
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

    /* Destroy modules in reverse initialization order. */
    if (compositor->test_overlay) {
        zen_cairo_buffer_destroy(compositor->test_overlay);
        free(compositor->test_overlay);
        compositor->test_overlay = NULL;
    }
    zen_lock_destroy(compositor);
    zen_dbus_destroy(compositor);
    zen_layer_destroy(compositor);
    zen_keybinds_destroy(compositor);
    zen_wallpaper_destroy(compositor);
    zen_input_destroy(compositor);
    zen_xdg_destroy(compositor);

    /* Destroy core resources in reverse creation order. */
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
