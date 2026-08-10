/*
 * Zen OS — Shared Data Models Implementation
 *
 * Init functions zero-initialize structs with safe defaults.
 * Cleanup functions free all heap-allocated members.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "zen/models.h"

#include <stdlib.h>
#include <string.h>

/* ── Helper: free a string array ─────────────────────────────────────────── */

static void free_string_array(char **arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

/* ── ShelfConfig ─────────────────────────────────────────────────────────── */

void zen_shelf_config_init(ZenShelfConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->position = ZEN_SHELF_BOTTOM;
    cfg->icon_size = 48;
}

void zen_shelf_config_cleanup(ZenShelfConfig *cfg) {
    free_string_array(cfg->pinned_apps, cfg->pinned_apps_count);
    memset(cfg, 0, sizeof(*cfg));
}

/* ── AppInfo ─────────────────────────────────────────────────────────────── */

void zen_app_info_init(ZenAppInfo *info) {
    memset(info, 0, sizeof(*info));
}

void zen_app_info_cleanup(ZenAppInfo *info) {
    free(info->app_id);
    free(info->name);
    free(info->icon_path);
    free(info->exec);
    free_string_array(info->categories, info->categories_count);
    memset(info, 0, sizeof(*info));
}

/* ── Deployment ──────────────────────────────────────────────────────────── */

void zen_deployment_init(ZenDeployment *dep) {
    memset(dep, 0, sizeof(*dep));
}

void zen_deployment_cleanup(ZenDeployment *dep) {
    free(dep->commit);
    free(dep->version);
    free_string_array(dep->layered_packages, dep->layered_packages_count);
    memset(dep, 0, sizeof(*dep));
}

/* ── UpdateInfo ──────────────────────────────────────────────────────────── */

void zen_update_info_init(ZenUpdateInfo *info) {
    memset(info, 0, sizeof(*info));
}

void zen_update_info_cleanup(ZenUpdateInfo *info) {
    free(info->commit);
    free(info->version);
    free(info->changelog);
    memset(info, 0, sizeof(*info));
}

/* ── PackageInfo ─────────────────────────────────────────────────────────── */

void zen_package_info_init(ZenPackageInfo *info) {
    memset(info, 0, sizeof(*info));
}

void zen_package_info_cleanup(ZenPackageInfo *info) {
    free(info->name);
    free(info->version);
    free(info->description);
    free_string_array(info->permissions, info->permissions_count);
    memset(info, 0, sizeof(*info));
}

/* ── BlockedRequest ──────────────────────────────────────────────────────── */

void zen_blocked_request_init(ZenBlockedRequest *req) {
    memset(req, 0, sizeof(*req));
}

void zen_blocked_request_cleanup(ZenBlockedRequest *req) {
    free(req->source_app);
    free(req->destination);
    free(req->rule_matched);
    memset(req, 0, sizeof(*req));
}

/* ── AuditEntry ──────────────────────────────────────────────────────────── */

void zen_audit_entry_init(ZenAuditEntry *entry) {
    memset(entry, 0, sizeof(*entry));
}

void zen_audit_entry_cleanup(ZenAuditEntry *entry) {
    free(entry->details);
    free(entry->source_app);
    memset(entry, 0, sizeof(*entry));
}

/* ── PressureInfo ────────────────────────────────────────────────────────── */

void zen_pressure_info_init(ZenPressureInfo *info) {
    memset(info, 0, sizeof(*info));
}

/* ── Budget ──────────────────────────────────────────────────────────────── */

void zen_budget_init(ZenBudget *budget) {
    memset(budget, 0, sizeof(*budget));
}

void zen_budget_cleanup(ZenBudget *budget) {
    free(budget->cgroup_path);
    memset(budget, 0, sizeof(*budget));
}

/* ── Notification ────────────────────────────────────────────────────────── */

void zen_notification_init(ZenNotification *notif) {
    memset(notif, 0, sizeof(*notif));
    notif->timeout_ms = -1;  /* persistent by default */
}

void zen_notification_cleanup(ZenNotification *notif) {
    free(notif->app_id);
    free(notif->summary);
    free(notif->body);
    free(notif->icon);
    memset(notif, 0, sizeof(*notif));
}

/* ── OOBEConfig ──────────────────────────────────────────────────────────── */

void zen_oobe_config_init(ZenOOBEConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
}

void zen_oobe_config_cleanup(ZenOOBEConfig *cfg) {
    free(cfg->locale);
    free(cfg->timezone);
    free(cfg->keyboard_layout);
    free(cfg->username);
    free(cfg->password_hash);
    free(cfg->network_ssid);
    free(cfg->network_psk);
    memset(cfg, 0, sizeof(*cfg));
}

/* ── KioskConfig ─────────────────────────────────────────────────────────── */

void zen_kiosk_config_init(ZenKioskConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
}

void zen_kiosk_config_cleanup(ZenKioskConfig *cfg) {
    free(cfg->app_id);
    memset(cfg, 0, sizeof(*cfg));
}

/* ── AccessibilityConfig ─────────────────────────────────────────────────── */

void zen_accessibility_config_init(ZenAccessibilityConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->font_scale = 1.0f;
}
