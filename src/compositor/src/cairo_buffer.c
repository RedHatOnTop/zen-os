/*
 * Zen OS Compositor — Cairo + Pango Rendering Pipeline
 *
 * Implements ZenCairoBuffer: a Cairo ARGB32 image surface wrapped in a
 * custom wlr_buffer and uploaded as a wlr_scene_buffer node in the
 * SceneFX scene graph.
 *
 * Workflow:
 *   1. zen_cairo_buffer_create()  — allocate surface, Cairo ctx, Pango layout,
 *                                   register wlr_buffer, create scene node
 *   2. Draw with Cairo / Pango via zen_cairo_buffer_get_pango()
 *   3. zen_cairo_buffer_submit()  — flush surface, push pixels to scene node
 *   4. zen_cairo_buffer_destroy() — release all resources
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

/* drm_fourcc.h provides DRM_FORMAT_ARGB8888 */
#include <drm_fourcc.h>

#include "zen/cairo_buffer.h"

/* ── Internal buffer type ────────────────────────────────────────────────── */

/*
 * ZenRawBuffer wraps a Cairo pixel array as a wlr_buffer so that
 * wlr_scene_buffer_set_buffer() can consume it.
 *
 * Cairo ARGB32 stores pixels as packed 32-bit little-endian ARGB, which
 * matches DRM_FORMAT_ARGB8888 exactly — no conversion needed.
 */
struct ZenRawBuffer {
    struct wlr_buffer base;   /* MUST be first — wlr_buffer_init writes here */
    uint8_t          *data;   /* pixel data owned by the Cairo surface */
    size_t            stride; /* bytes per row */
};

/* ── wlr_buffer_impl callbacks ───────────────────────────────────────────── */

static void zen_raw_buffer_destroy(struct wlr_buffer *wlr_buf) {
    struct ZenRawBuffer *raw = (struct ZenRawBuffer *)wlr_buf;
    /* pixel data is owned by the Cairo surface — do not free here */
    free(raw);
}

static bool zen_raw_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buf,
                                                  uint32_t flags,
                                                  void **data,
                                                  uint32_t *format,
                                                  size_t *stride) {
    (void)flags;
    struct ZenRawBuffer *raw = (struct ZenRawBuffer *)wlr_buf;
    *data   = raw->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = raw->stride;
    return true;
}

static void zen_raw_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buf) {
    (void)wlr_buf;
    /* nothing to do — Cairo owns the memory */
}

static const struct wlr_buffer_impl zen_raw_buffer_impl = {
    .destroy               = zen_raw_buffer_destroy,
    .begin_data_ptr_access = zen_raw_buffer_begin_data_ptr_access,
    .end_data_ptr_access   = zen_raw_buffer_end_data_ptr_access,
};

/* ── Public API ──────────────────────────────────────────────────────────── */

int zen_cairo_buffer_create(struct ZenCairoBuffer *buf,
                            struct wlr_renderer *renderer,
                            struct wlr_scene_tree *parent,
                            int width, int height) {
    (void)renderer; /* renderer not needed for CPU-side Cairo buffers */

    int ret = -1;

    if (!buf || !parent || width <= 0 || height <= 0) {
        wlr_log(WLR_ERROR, "%s", "zen_cairo_buffer_create: invalid arguments");
        goto cleanup;
    }

    memset(buf, 0, sizeof(*buf));
    buf->width  = width;
    buf->height = height;

    /* 1. Cairo ARGB32 image surface (CPU-side pixel buffer). */
    buf->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                              width, height);
    if (cairo_surface_status(buf->surface) != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "Failed to create Cairo surface: %s",
                cairo_status_to_string(cairo_surface_status(buf->surface)));
        cairo_surface_destroy(buf->surface);
        buf->surface = NULL;
        goto cleanup;
    }

    /* 2. Cairo drawing context. */
    buf->cr = cairo_create(buf->surface);
    if (cairo_status(buf->cr) != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "Failed to create Cairo context: %s",
                cairo_status_to_string(cairo_status(buf->cr)));
        cairo_destroy(buf->cr);
        buf->cr = NULL;
        goto cleanup;
    }

    /* 3. Pango layout for text rendering. */
    buf->pango_layout = pango_cairo_create_layout(buf->cr);
    if (!buf->pango_layout) {
        wlr_log(WLR_ERROR, "%s", "Failed to create Pango layout");
        goto cleanup;
    }

    /* 4. Wrap Cairo pixel data in a wlr_buffer.
     *
     * cairo_image_surface_get_data() returns a pointer to the surface's
     * internal pixel array.  We keep the Cairo surface alive for the
     * lifetime of the ZenCairoBuffer so this pointer remains valid.
     */
    struct ZenRawBuffer *raw = calloc(1, sizeof(*raw));
    if (!raw) {
        wlr_log(WLR_ERROR, "%s", "Failed to allocate ZenRawBuffer");
        goto cleanup;
    }

    raw->data   = cairo_image_surface_get_data(buf->surface);
    raw->stride = (size_t)cairo_image_surface_get_stride(buf->surface);

    wlr_buffer_init(&raw->base, &zen_raw_buffer_impl, width, height);
    buf->wlr_buf = &raw->base;

    /* 5. Create wlr_scene_buffer node under parent. */
    buf->scene_buffer = wlr_scene_buffer_create(parent, buf->wlr_buf);
    if (!buf->scene_buffer) {
        wlr_log(WLR_ERROR, "%s", "Failed to create wlr_scene_buffer");
        wlr_buffer_drop(buf->wlr_buf);
        buf->wlr_buf = NULL;
        goto cleanup;
    }

    wlr_log(WLR_DEBUG, "ZenCairoBuffer created (%dx%d)", width, height);
    ret = 0;

cleanup:
    if (ret != 0) {
        zen_cairo_buffer_destroy(buf);
    }
    return ret;
}

PangoLayout *zen_cairo_buffer_get_pango(struct ZenCairoBuffer *buf) {
    if (!buf) {
        return NULL;
    }
    return buf->pango_layout;
}

int zen_cairo_buffer_submit(struct ZenCairoBuffer *buf) {
    if (!buf || !buf->surface || !buf->scene_buffer || !buf->wlr_buf) {
        wlr_log(WLR_ERROR, "%s", "zen_cairo_buffer_submit: invalid buffer");
        return -1;
    }

    /* Flush Cairo's internal state to the pixel array. */
    cairo_surface_flush(buf->surface);

    /* Push the updated pixels to the scene node.
     * wlr_scene_buffer_set_buffer() takes a reference; the scene node will
     * hold the buffer alive until it is replaced or the node is destroyed. */
    wlr_scene_buffer_set_buffer(buf->scene_buffer, buf->wlr_buf);

    return 0;
}

void zen_cairo_buffer_destroy(struct ZenCairoBuffer *buf) {
    if (!buf) {
        return;
    }

    /* Destroy scene node first — it holds a reference to wlr_buf. */
    if (buf->scene_buffer) {
        wlr_scene_node_destroy(&buf->scene_buffer->node);
        buf->scene_buffer = NULL;
    }

    /* Drop our producer reference to the wlr_buffer.
     * If the scene node already released its consumer reference, this
     * triggers zen_raw_buffer_destroy() which frees the ZenRawBuffer shell.
     * The Cairo pixel data is freed when we destroy the Cairo surface below. */
    if (buf->wlr_buf) {
        wlr_buffer_drop(buf->wlr_buf);
        buf->wlr_buf = NULL;
    }

    if (buf->pango_layout) {
        g_object_unref(buf->pango_layout);
        buf->pango_layout = NULL;
    }

    if (buf->cr) {
        cairo_destroy(buf->cr);
        buf->cr = NULL;
    }

    if (buf->surface) {
        cairo_surface_destroy(buf->surface);
        buf->surface = NULL;
    }

    wlr_log(WLR_DEBUG, "%s", "ZenCairoBuffer destroyed");
}
