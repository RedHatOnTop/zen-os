/*
 * Zen OS Shell — Theme Configuration
 *
 * Persistent dark/light mode state and color palettes.
 * Loaded from ~/.config/zenos/theme.json on startup.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_THEME_CONFIG_H
#define ZEN_SHELL_THEME_CONFIG_H

#include <stdbool.h>

/* Color palette for a theme mode. All values are RGBA in [0.0, 1.0]. */
struct ZenColorPalette {
    float background[4];
    float foreground[4];
    float accent[4];
    float border[4];
    float shadow[4];
    float surface[4];
    float text_secondary[4];
};

struct ZenThemeConfig {
    bool                    dark_mode;
    char                   *wallpaper_path;
    struct ZenColorPalette  dark_palette;
    struct ZenColorPalette  light_palette;
};

/* Initialize theme config with defaults (dark mode, built-in palettes). */
void zen_theme_config_init(struct ZenThemeConfig *cfg);

/* Free heap-allocated fields. */
void zen_theme_config_cleanup(struct ZenThemeConfig *cfg);

/* Load theme config from path (e.g. ~/.config/zenos/theme.json).
 * On missing/invalid file, fills cfg with defaults.
 * Returns 0 on success, -1 on error (defaults still applied). */
int zen_theme_config_load(struct ZenThemeConfig *cfg, const char *path);

/* Save theme config to the given path.
 * Returns 0 on success, -1 on write failure. */
int zen_theme_config_save(const struct ZenThemeConfig *cfg, const char *path);

/* Serialize to JSON string. Caller must free(). Returns NULL on failure. */
char *zen_theme_config_to_json(const struct ZenThemeConfig *cfg);

/* Deserialize from JSON string. Returns 0 on success, -1 on parse error. */
int zen_theme_config_from_json(struct ZenThemeConfig *cfg, const char *json);

/* Get the active color palette based on dark_mode state. */
const struct ZenColorPalette *zen_theme_config_get_palette(
    const struct ZenThemeConfig *cfg);

#endif /* ZEN_SHELL_THEME_CONFIG_H */
