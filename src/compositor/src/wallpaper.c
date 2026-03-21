/*
 * Zen OS Compositor — Desktop Wallpaper Module
 *
 * Loads a PNG wallpaper from data/branding/wallpaper/ (or a user-configured
 * path from ~/.config/zenos/theme.json) and renders it as the bottom-most
 * wlr_scene_buffer node in the scene graph.  Falls back to a solid brand
 * color (#1a1a2e) if no image is found or loading fails.
 *
 * Scaling: aspect-fill with center crop — the image is scaled so that it
 * covers the full output in both dimensions while preserving aspect ratio,
 * then center-cropped via a Cairo transform.
 *
 * Error handling: goto cleanup pattern (single exit point) per AGENTS.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <cairo.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <drm_fourcc.h>

#include "zen/compositor.h"
#include "zen/wallpaper.h"

/* ── Brand fallback color: #1a1a2e ──────────────────────────────────────── */
#define ZEN_BRAND_R 0.102f
#define ZEN_BRAND_G 0.102f
#define ZEN_BRAND_B 0.180f

/* ── Internal wlr_buffer wrapping a Cairo pixel array ───────────────────── */

struct ZenWallpaperBuffer {
    struct wlr_buffer  base;    /* MUST be first */
    uint8_t           *data;    /* pixel data — owned by the Cairo surface */
    size_t             stride;
};

static void wallpaper_buf_destroy(struct wlr_buffer *wlr_buf) {
    struct ZenWallpaperBuffer *wb = (struct ZenWallpaperBuffer *)wlr_buf;
    free(wb);
}

static bool wallpaper_buf_begin_data_ptr_access(struct wlr_buffer *wlr_buf,
                                                 uint32_t flags,
                                                 void **data,
                                                 uint32_t *format,
                                                 size_t *stride) {
    (void)flags;
    struct ZenWallpaperBuffer *wb = (struct ZenWallpaperBuffer *)wlr_buf;
    *data   = wb->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = wb->stride;
    return true;
}

static void wallpaper_buf_end_data_ptr_access(struct wlr_buffer *wlr_buf) {
    (void)wlr_buf;
}

static const struct wlr_buffer_impl wallpaper_buf_impl = {
    .destroy               = wallpaper_buf_destroy,
    .begin_data_ptr_access = wallpaper_buf_begin_data_ptr_access,
    .end_data_ptr_access   = wallpaper_buf_end_data_ptr_access,
};

/* ── Per-output wallpaper state ──────────────────────────────────────────── */

/*
 * ZenWallpaperOutput holds the rendered wallpaper for one output.
 * Stored in a singly-linked list rooted at ZenWallpaperState.outputs.
 */
struct ZenWallpaperOutput {
    struct wlr_output         *wlr_output;
    cairo_surface_t           *surface;     /* scaled+cropped ARGB32 surface */
    struct ZenWallpaperBuffer *wbuf;        /* wlr_buffer wrapping surface */
    struct wlr_scene_buffer   *scene_buf;   /* scene node under wallpaper_tree */
    struct ZenWallpaperOutput *next;
};

/* ── Module state ────────────────────────────────────────────────────────── */

struct ZenWallpaperState {
    cairo_surface_t           *src_image;   /* original loaded PNG, or NULL */
    struct ZenWallpaperOutput *outputs;     /* linked list of per-output state */
};

/* We store the state pointer in compositor->wallpaper_tree->node.data via
 * a small indirection: we keep a static pointer per compositor instance.
 * Since this is a single-compositor binary, a module-level pointer suffices. */
static struct ZenWallpaperState *g_wallpaper_state = NULL;

/* ── theme.json wallpaper path parser ───────────────────────────────────── */

/*
 * Parse ~/.config/zenos/theme.json for the "wallpaper_path" key.
 * Uses pure C17 string scanning — no JSON library.
 *
 * Returns a heap-allocated path string on success (caller must free),
 * or NULL if the key is absent or the file cannot be read.
 */
static char *parse_wallpaper_path_from_theme(void) {
    char *result = NULL;
    char *buf    = NULL;
    FILE *f      = NULL;

    const char *home = getenv("HOME");
    if (!home) {
        goto cleanup;
    }

    /* Build path: $HOME/.config/zenos/theme.json */
    size_t path_len = strlen(home) + sizeof("/.config/zenos/theme.json");
    char *path = malloc(path_len);
    if (!path) {
        goto cleanup;
    }
    snprintf(path, path_len, "%s/.config/zenos/theme.json", home);

    f = fopen(path, "r");
    free(path);
    if (!f) {
        goto cleanup;
    }

    /* Read entire file into buf. */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize <= 0 || fsize > 65536) {
        goto cleanup;
    }

    buf = malloc((size_t)fsize + 1);
    if (!buf) {
        goto cleanup;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        goto cleanup;
    }
    buf[fsize] = '\0';

    /*
     * Locate "wallpaper_path" key.
     * Expected format: "wallpaper_path": "/some/path.png"
     * We search for the key, skip whitespace and ':', then extract the
     * quoted string value.
     */
    const char *key = "\"wallpaper_path\"";
    char *pos = strstr(buf, key);
    if (!pos) {
        goto cleanup;
    }
    pos += strlen(key);

    /* Skip whitespace and colon. */
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        pos++;
    }
    if (*pos != ':') {
        goto cleanup;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        pos++;
    }
    if (*pos != '"') {
        goto cleanup;
    }
    pos++; /* skip opening quote */

    /* Find closing quote, handling simple backslash escapes. */
    const char *start = pos;
    while (*pos && *pos != '"') {
        if (*pos == '\\') {
            pos++; /* skip escaped char */
            if (!*pos) break;
        }
        pos++;
    }
    if (*pos != '"') {
        goto cleanup;
    }

    size_t val_len = (size_t)(pos - start);
    result = malloc(val_len + 1);
    if (!result) {
        goto cleanup;
    }
    memcpy(result, start, val_len);
    result[val_len] = '\0';

cleanup:
    free(buf);
    if (f) fclose(f);
    return result;
}

/* ── PNG discovery ───────────────────────────────────────────────────────── */

/*
 * Scan dir_path for the first *.png file.
 * Returns a heap-allocated full path on success (caller must free), or NULL.
 */
static char *find_first_png(const char *dir_path) {
    char *result = NULL;
    DIR  *dir    = opendir(dir_path);
    if (!dir) {
        return NULL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t nlen = strlen(name);
        if (nlen < 5) continue;
        if (strcasecmp(name + nlen - 4, ".png") != 0) continue;

        size_t full_len = strlen(dir_path) + 1 + nlen + 1;
        result = malloc(full_len);
        if (result) {
            snprintf(result, full_len, "%s/%s", dir_path, name);
        }
        break;
    }

    closedir(dir);
    return result;
}

/* ── Aspect-fill scaling ─────────────────────────────────────────────────── */

/*
 * Compute scale factor for aspect-fill (cover) scaling.
 * The image is scaled so that both dimensions are >= output dimensions,
 * preserving aspect ratio.  Returns the scale factor.
 *
 * Scaled dimensions: s_w = img_w * scale, s_h = img_h * scale
 * Invariants: s_w >= out_w AND s_h >= out_h AND s_w/s_h == img_w/img_h
 */
static double compute_aspect_fill_scale(int img_w, int img_h,
                                         int out_w, int out_h) {
    double scale_x = (double)out_w / (double)img_w;
    double scale_y = (double)out_h / (double)img_h;
    /* Use the larger scale so the image covers the output in both axes. */
    return (scale_x > scale_y) ? scale_x : scale_y;
}

/* ── Render one output ───────────────────────────────────────────────────── */

/*
 * Render the wallpaper (or brand-color fallback) for a single output.
 * Allocates a ZenWallpaperOutput and prepends it to state->outputs.
 */
static int render_output_internal(struct ZenWallpaperState *state,
                                   struct ZenCompositor *compositor,
                                   struct ZenOutput *output) {
    int ret = -1;

    struct wlr_output *wlr_out = output->wlr_output;
    int out_w = wlr_out->width;
    int out_h = wlr_out->height;

    if (out_w <= 0 || out_h <= 0) {
        wlr_log(WLR_ERROR, "wallpaper: output %s has invalid dimensions %dx%d",
                wlr_out->name, out_w, out_h);
        return -1;
    }

    struct ZenWallpaperOutput *wo = calloc(1, sizeof(*wo));
    if (!wo) {
        wlr_log(WLR_ERROR, "%s", "wallpaper: failed to allocate ZenWallpaperOutput");
        return -1;
    }
    wo->wlr_output = wlr_out;

    /* Create ARGB32 surface for this output. */
    wo->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out_w, out_h);
    if (cairo_surface_status(wo->surface) != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "wallpaper: failed to create Cairo surface: %s",
                cairo_status_to_string(cairo_surface_status(wo->surface)));
        goto cleanup;
    }

    cairo_t *cr = cairo_create(wo->surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "wallpaper: failed to create Cairo context: %s",
                cairo_status_to_string(cairo_status(cr)));
        cairo_destroy(cr);
        goto cleanup;
    }

    if (state->src_image) {
        /* ── Aspect-fill scaling with center crop ── */
        int img_w = cairo_image_surface_get_width(state->src_image);
        int img_h = cairo_image_surface_get_height(state->src_image);

        double scale = compute_aspect_fill_scale(img_w, img_h, out_w, out_h);
        double scaled_w = img_w * scale;
        double scaled_h = img_h * scale;

        /* Center-crop offset: how much of the scaled image extends beyond
         * the output on each side. */
        double offset_x = (scaled_w - out_w) / 2.0;
        double offset_y = (scaled_h - out_h) / 2.0;

        /* Apply transform: scale image then shift so center aligns. */
        cairo_save(cr);
        cairo_translate(cr, -offset_x, -offset_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, state->src_image, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);

        wlr_log(WLR_DEBUG,
                "wallpaper: output %s — image %dx%d scaled %.3f → %.0fx%.0f, "
                "crop offset (%.1f, %.1f)",
                wlr_out->name, img_w, img_h, scale,
                scaled_w, scaled_h, offset_x, offset_y);
    } else {
        /* ── Solid brand color fallback (#1a1a2e) ── */
        cairo_set_source_rgb(cr, ZEN_BRAND_R, ZEN_BRAND_G, ZEN_BRAND_B);
        cairo_paint(cr);
        wlr_log(WLR_INFO, "wallpaper: output %s — using brand color fallback",
                wlr_out->name);
    }

    cairo_surface_flush(wo->surface);
    cairo_destroy(cr);

    /* Wrap pixel data in a wlr_buffer. */
    wo->wbuf = calloc(1, sizeof(*wo->wbuf));
    if (!wo->wbuf) {
        wlr_log(WLR_ERROR, "%s", "wallpaper: failed to allocate ZenWallpaperBuffer");
        goto cleanup;
    }
    wo->wbuf->data   = cairo_image_surface_get_data(wo->surface);
    wo->wbuf->stride = (size_t)cairo_image_surface_get_stride(wo->surface);
    wlr_buffer_init(&wo->wbuf->base, &wallpaper_buf_impl, out_w, out_h);

    /* Create scene buffer node under wallpaper_tree. */
    wo->scene_buf = wlr_scene_buffer_create(compositor->wallpaper_tree,
                                             &wo->wbuf->base);
    if (!wo->scene_buf) {
        wlr_log(WLR_ERROR, "%s", "wallpaper: failed to create wlr_scene_buffer");
        wlr_buffer_drop(&wo->wbuf->base);
        wo->wbuf = NULL;
        goto cleanup;
    }

    /* Position at (0, 0) — wallpaper_tree is the bottom-most layer. */
    wlr_scene_node_set_position(&wo->scene_buf->node, 0, 0);

    /* Prepend to outputs list. */
    wo->next      = state->outputs;
    state->outputs = wo;

    wlr_log(WLR_INFO, "wallpaper: rendered for output %s (%dx%d)",
            wlr_out->name, out_w, out_h);
    ret = 0;

cleanup:
    if (ret != 0) {
        if (wo->scene_buf) {
            wlr_scene_node_destroy(&wo->scene_buf->node);
        }
        if (wo->surface) {
            cairo_surface_destroy(wo->surface);
        }
        free(wo);
    }
    return ret;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int zen_wallpaper_init(struct ZenCompositor *compositor) {
    int ret = -1;

    struct ZenWallpaperState *state = calloc(1, sizeof(*state));
    if (!state) {
        wlr_log(WLR_ERROR, "%s", "wallpaper: failed to allocate state");
        goto cleanup;
    }

    /* Create wallpaper_tree as the bottom-most scene tree.
     * It is inserted as the first child of the scene root so it renders
     * below all windows and overlays. */
    compositor->wallpaper_tree =
        wlr_scene_tree_create(&compositor->scene->tree);
    if (!compositor->wallpaper_tree) {
        wlr_log(WLR_ERROR, "%s", "wallpaper: failed to create wallpaper_tree");
        goto cleanup;
    }

    /* Determine wallpaper image path:
     * 1. Check theme.json for user-configured path.
     * 2. Fall back to scanning data/branding/wallpaper/ for first PNG.
     * 3. If neither found, use brand color fallback (state->src_image = NULL). */
    char *img_path = parse_wallpaper_path_from_theme();

    if (!img_path) {
        img_path = find_first_png("data/branding/wallpaper");
    }

    if (img_path) {
        state->src_image = cairo_image_surface_create_from_png(img_path);
        if (cairo_surface_status(state->src_image) != CAIRO_STATUS_SUCCESS) {
            wlr_log(WLR_ERROR,
                    "wallpaper: failed to load '%s': %s — using brand color",
                    img_path,
                    cairo_status_to_string(
                        cairo_surface_status(state->src_image)));
            cairo_surface_destroy(state->src_image);
            state->src_image = NULL;
        } else {
            wlr_log(WLR_INFO, "wallpaper: loaded image '%s'", img_path);
        }
        free(img_path);
    } else {
        wlr_log(WLR_INFO, "%s",
                "wallpaper: no image found — using brand color fallback");
    }

    g_wallpaper_state = state;
    ret = 0;

cleanup:
    if (ret != 0) {
        if (state) {
            if (state->src_image) {
                cairo_surface_destroy(state->src_image);
            }
            free(state);
        }
    }
    return ret;
}

void zen_wallpaper_destroy(struct ZenCompositor *compositor) {
    (void)compositor;

    struct ZenWallpaperState *state = g_wallpaper_state;
    if (!state) {
        return;
    }

    /* Destroy per-output resources. */
    struct ZenWallpaperOutput *wo = state->outputs;
    while (wo) {
        struct ZenWallpaperOutput *next = wo->next;

        if (wo->scene_buf) {
            wlr_scene_node_destroy(&wo->scene_buf->node);
            wo->scene_buf = NULL;
        }
        /* wbuf is freed by wallpaper_buf_destroy after scene node drops ref */
        if (wo->surface) {
            cairo_surface_destroy(wo->surface);
            wo->surface = NULL;
        }
        free(wo);
        wo = next;
    }

    if (state->src_image) {
        cairo_surface_destroy(state->src_image);
        state->src_image = NULL;
    }

    free(state);
    g_wallpaper_state = NULL;

    wlr_log(WLR_DEBUG, "%s", "wallpaper: destroyed");
}

int zen_wallpaper_render_output(struct ZenCompositor *compositor,
                                struct ZenOutput *output) {
    struct ZenWallpaperState *state = g_wallpaper_state;
    if (!state) {
        wlr_log(WLR_ERROR, "%s",
                "wallpaper: zen_wallpaper_render_output called before init");
        return -1;
    }
    if (!compositor->wallpaper_tree) {
        wlr_log(WLR_ERROR, "%s", "wallpaper: wallpaper_tree is NULL");
        return -1;
    }
    return render_output_internal(state, compositor, output);
}
