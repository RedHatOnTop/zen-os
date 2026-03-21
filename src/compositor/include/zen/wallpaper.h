/*
 * Zen OS Compositor — Desktop Wallpaper Module
 *
 * Loads a PNG wallpaper from data/branding/wallpaper/ and renders it as
 * the bottom-most wlr_scene_buffer node in the scene graph.  Falls back
 * to a solid brand color (#1a1a2e) if no image is found or loading fails.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMPOSITOR_WALLPAPER_H
#define ZEN_COMPOSITOR_WALLPAPER_H

struct ZenCompositor;

/* ZenOutput wraps a wlr_output and its scene graph region.
 * Defined in src/compositor/include/zen/compositor.h alongside ZenCompositor. */
struct ZenOutput;

/* Initialize wallpaper rendering. Loads image from
 * data/branding/wallpaper/ or falls back to brand color.
 * Returns 0 on success. */
int zen_wallpaper_init(struct ZenCompositor *compositor);

/* Destroy wallpaper resources. */
void zen_wallpaper_destroy(struct ZenCompositor *compositor);

/* Render wallpaper for a specific output (called on output configure). */
int zen_wallpaper_render_output(struct ZenCompositor *compositor,
                                struct ZenOutput *output);

#endif /* ZEN_COMPOSITOR_WALLPAPER_H */
