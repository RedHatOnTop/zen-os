/*
 * Zen OS — Shell-Specific PBT Generators
 *
 * Extends zen_pbt.h with generators that depend on shell model types.
 * Include this header (after zen_pbt.h) only in shell unit tests.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_PBT_SHELL_H
#define ZEN_PBT_SHELL_H

#include "zen_pbt.h"
#include "zen/models.h"

/*
 * Random ZenShelfConfig generator.
 * Fills *cfg with randomized but valid-range values.
 * pinned_apps are heap-allocated; caller must zen_shelf_config_cleanup(cfg).
 */
static inline void zen_pbt_rand_shelf_config(ZenShelfConfig *cfg) {
    zen_shelf_config_init(cfg);

    /* Random position in valid enum range */
    cfg->position = (ZenShelfPosition)zen_pbt_rand_int(
        ZEN_SHELF_BOTTOM, ZEN_SHELF_TOP);

    cfg->auto_hide = zen_pbt_rand_bool();

    /* icon_size in valid clamped range [16, 64] */
    cfg->icon_size = zen_pbt_rand_int(16, 64);

    /* 0–4 pinned apps */
    int count = zen_pbt_rand_int(0, 4);
    if (count > 0) {
        cfg->pinned_apps = calloc((size_t)count, sizeof(char *));
        if (!cfg->pinned_apps) return;
        for (int i = 0; i < count; i++) {
            /* Generate a simple app_id-safe string (lowercase alphanum only) */
            int len = zen_pbt_rand_int(4, 20);
            char *s = calloc(1, (size_t)len + 1);
            if (!s) {
                cfg->pinned_apps_count = i;
                return;
            }
            for (int j = 0; j < len; j++) {
                int r = zen_pbt_rand_int(0, 35);
                s[j] = (char)(r < 26 ? 'a' + r : '0' + (r - 26));
            }
            s[len] = '\0';
            cfg->pinned_apps[i] = s;
        }
        cfg->pinned_apps_count = count;
    }
}

#endif /* ZEN_PBT_SHELL_H */
