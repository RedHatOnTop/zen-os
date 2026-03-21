#ifndef ZEN_COMPOSITOR_CAIRO_BUFFER_H
#define ZEN_COMPOSITOR_CAIRO_BUFFER_H

#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>

struct wlr_scene_buffer;
struct wlr_scene_tree;
struct wlr_renderer;

struct ZenCairoBuffer {
    cairo_surface_t         *surface;
    cairo_t                 *cr;
    PangoLayout             *pango_layout;
    struct wlr_buffer       *wlr_buf;
    struct wlr_scene_buffer *scene_buffer;
    int                      width;
    int                      height;
};

/* Create a Cairo buffer of the given dimensions.
 * parent: scene tree node to attach the buffer to.
 * Returns 0 on success, -1 on failure. */
int zen_cairo_buffer_create(struct ZenCairoBuffer *buf,
                            struct wlr_renderer *renderer,
                            struct wlr_scene_tree *parent,
                            int width, int height);

/* Get the Pango layout for text rendering. */
PangoLayout *zen_cairo_buffer_get_pango(struct ZenCairoBuffer *buf);

/* Upload the Cairo surface content to the wlr_scene_buffer. */
int zen_cairo_buffer_submit(struct ZenCairoBuffer *buf);

/* Destroy the Cairo buffer and release all resources. */
void zen_cairo_buffer_destroy(struct ZenCairoBuffer *buf);

#endif /* ZEN_COMPOSITOR_CAIRO_BUFFER_H */
