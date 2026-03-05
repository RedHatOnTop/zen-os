/*
 * Zen OS — Shared Data Models
 *
 * All struct definitions used across Zen OS components.
 * These map directly to D-Bus interface types and internal state.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_COMMON_MODELS_H
#define ZEN_COMMON_MODELS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* ── Enumerations ────────────────────────────────────────────────────────── */

typedef enum {
    ZEN_SHELF_BOTTOM = 0,
    ZEN_SHELF_LEFT,
    ZEN_SHELF_RIGHT,
    ZEN_SHELF_TOP,
} ZenShelfPosition;

typedef enum {
    ZEN_APP_SOURCE_NATIVE = 0,
    ZEN_APP_SOURCE_FLATPAK,
    ZEN_APP_SOURCE_PWA,
    ZEN_APP_SOURCE_WAYDROID,
} ZenAppSource;

typedef enum {
    ZEN_PKG_SOURCE_FLATPAK = 0,
    ZEN_PKG_SOURCE_LAYERED,
} ZenPackageSource;

typedef enum {
    ZEN_PROTO_TCP = 0,
    ZEN_PROTO_UDP,
    ZEN_PROTO_DNS,
} ZenProtocol;

typedef enum {
    ZEN_AUDIT_BLOCKED = 0,
    ZEN_AUDIT_ALLOWED,
    ZEN_AUDIT_RULE_UPDATE,
} ZenAuditEventType;

typedef enum {
    ZEN_URGENCY_LOW = 0,
    ZEN_URGENCY_NORMAL,
    ZEN_URGENCY_CRITICAL,
} ZenUrgency;

/* ── Data Model Structs ──────────────────────────────────────────────────── */

/* Shelf configuration — position, auto-hide, icon size, pinned apps */
typedef struct {
    ZenShelfPosition position;
    bool             auto_hide;
    int              icon_size;      /* default: 48, range: 32-64 */
    char           **pinned_apps;    /* ordered list of app_id strings */
    int              pinned_apps_count;
} ZenShelfConfig;

/* Application info — represents an installed or running application */
typedef struct {
    char       *app_id;       /* reverse-domain, e.g. "org.zen.browser" */
    char       *name;         /* display name */
    char       *icon_path;    /* path to icon file */
    char       *exec;         /* launch command */
    ZenAppSource source;
    char       **categories;  /* freedesktop categories */
    int          categories_count;
    bool         is_running;
    int          pid;         /* valid only when is_running == true */
} ZenAppInfo;

/* OSTree deployment info */
typedef struct {
    int      index;           /* 0 = current, 1 = previous, etc. */
    char    *commit;          /* OSTree commit hash */
    char    *version;         /* human-readable version */
    time_t   timestamp;       /* deployment creation time */
    bool     is_active;
    bool     is_pinned;
    char   **layered_packages;
    int      layered_packages_count;
} ZenDeployment;

/* Available OS update info */
typedef struct {
    char    *commit;          /* target OSTree commit */
    char    *version;
    int64_t  delta_size_bytes;
    bool     is_security;     /* high-priority flag */
    bool     signature_valid;
    char    *changelog;
} ZenUpdateInfo;

/* Installed package info (Flatpak or layered) */
typedef struct {
    char            *name;
    char            *version;
    ZenPackageSource source;
    char            *description;
    bool             installed;
    char           **permissions;   /* Flatpak permissions */
    int              permissions_count;
    int64_t          size_bytes;
} ZenPackageInfo;

/* Privacy Guard — blocked network request */
typedef struct {
    time_t       timestamp;
    char        *source_app;    /* app_id or process name */
    char        *destination;   /* domain or IP */
    char        *rule_matched;  /* which blocklist rule */
    ZenProtocol  protocol;
} ZenBlockedRequest;

/* Privacy Guard — audit log entry */
typedef struct {
    time_t            timestamp;
    ZenAuditEventType event_type;
    char             *details;
    char             *source_app;
} ZenAuditEntry;

/* Resource Manager — PSI pressure info */
typedef struct {
    float   some_avg10;
    float   some_avg60;
    float   some_avg300;
    float   full_avg10;
    float   full_avg60;
    float   full_avg300;
    int64_t total_stall_us;   /* total stall time in microseconds */
} ZenPressureInfo;

/* Resource Manager — cgroup budget */
typedef struct {
    char    *cgroup_path;         /* e.g. "/sys/fs/cgroup/zenos/waydroid" */
    int64_t  memory_limit_bytes;
    int      cpu_weight;          /* cgroups v2 cpu.weight (1-10000) */
    bool     is_frozen;
} ZenBudget;

/* Notification toast */
typedef struct {
    int          id;
    char        *app_id;
    char        *summary;
    char        *body;
    char        *icon;
    ZenUrgency   urgency;
    int          timeout_ms;      /* -1 for persistent */
} ZenNotification;

/* OOBE configuration collected during first-boot wizard */
typedef struct {
    char *locale;           /* e.g. "en_US.UTF-8" */
    char *timezone;         /* e.g. "America/New_York" */
    char *keyboard_layout;  /* e.g. "us" */
    char *username;
    char *password_hash;    /* hashed, never stored plaintext */
    char *network_ssid;     /* nullable — optional */
    char *network_psk;      /* nullable — optional, stored in keyring */
} ZenOOBEConfig;

/* Kiosk mode configuration */
typedef struct {
    bool  enabled;
    char *app_id;           /* designated fullscreen application */
    bool  auto_restart;     /* restart app on crash/exit */
    bool  disable_shell;    /* hide Shelf, App Launcher, Quick Settings */
} ZenKioskConfig;

/* Accessibility settings */
typedef struct {
    bool  screen_reader_enabled;
    bool  high_contrast;
    bool  dark_mode;
    float font_scale;       /* 1.0 = default, range: 0.5-3.0 */
} ZenAccessibilityConfig;

/* ── Init / Cleanup Function Declarations ────────────────────────────────── */

void zen_shelf_config_init(ZenShelfConfig *cfg);
void zen_shelf_config_cleanup(ZenShelfConfig *cfg);

void zen_app_info_init(ZenAppInfo *info);
void zen_app_info_cleanup(ZenAppInfo *info);

void zen_deployment_init(ZenDeployment *dep);
void zen_deployment_cleanup(ZenDeployment *dep);

void zen_update_info_init(ZenUpdateInfo *info);
void zen_update_info_cleanup(ZenUpdateInfo *info);

void zen_package_info_init(ZenPackageInfo *info);
void zen_package_info_cleanup(ZenPackageInfo *info);

void zen_blocked_request_init(ZenBlockedRequest *req);
void zen_blocked_request_cleanup(ZenBlockedRequest *req);

void zen_audit_entry_init(ZenAuditEntry *entry);
void zen_audit_entry_cleanup(ZenAuditEntry *entry);

void zen_pressure_info_init(ZenPressureInfo *info);

void zen_budget_init(ZenBudget *budget);
void zen_budget_cleanup(ZenBudget *budget);

void zen_notification_init(ZenNotification *notif);
void zen_notification_cleanup(ZenNotification *notif);

void zen_oobe_config_init(ZenOOBEConfig *cfg);
void zen_oobe_config_cleanup(ZenOOBEConfig *cfg);

void zen_kiosk_config_init(ZenKioskConfig *cfg);
void zen_kiosk_config_cleanup(ZenKioskConfig *cfg);

void zen_accessibility_config_init(ZenAccessibilityConfig *cfg);

#endif /* ZEN_COMMON_MODELS_H */
