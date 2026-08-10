/*
 * Zen OS Shell — Shelf Configuration Persistence
 *
 * Load, save, validate, and JSON-serialize the shelf configuration
 * (position, auto-hide, icon size, pinned app list).
 * Backed by ~/.config/zenos/shelf.json.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_SHELL_SHELF_CONFIG_H
#define ZEN_SHELL_SHELF_CONFIG_H

#include "zen/models.h"

/*
 * Load shelf configuration from the file at `path` into `cfg`.
 * Missing or invalid fields are replaced with defaults:
 *   position      → ZEN_SHELF_BOTTOM
 *   auto_hide     → false
 *   icon_size     → 32
 *   pinned_apps   → empty list
 * Returns 0 on success, -1 on I/O error (defaults still applied).
 */
int zen_shelf_config_load(ZenShelfConfig *cfg, const char *path);

/*
 * Save shelf configuration from `cfg` to the file at `path`.
 * Creates parent directories if they do not exist.
 * Returns 0 on success, -1 on write failure.
 */
int zen_shelf_config_save(const ZenShelfConfig *cfg, const char *path);

/*
 * Validate and clamp fields in `cfg` in-place:
 *   position  — must be in [0, 3]; values outside range are clamped to
 *               ZEN_SHELF_BOTTOM (0).
 *   icon_size — clamped to [16, 64].
 * Returns the number of fields that were clamped (0 means fully valid).
 */
int zen_shelf_config_validate(ZenShelfConfig *cfg);

/*
 * Serialize `cfg` to a heap-allocated JSON string.
 * The caller is responsible for calling free() on the returned pointer.
 * Returns NULL on allocation failure.
 */
char *zen_shelf_config_to_json(const ZenShelfConfig *cfg);

/*
 * Deserialize `cfg` from the JSON string `json`.
 * Unknown fields are silently ignored (forward-compatibility).
 * Returns 0 on success, -1 on parse error.
 */
int zen_shelf_config_from_json(ZenShelfConfig *cfg, const char *json);

#endif /* ZEN_SHELL_SHELF_CONFIG_H */
