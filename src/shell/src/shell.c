/*
 * Zen OS Shell — Desktop Shell Module
 *
 * Top-level shell lifecycle: initializes and tears down all shell
 * components (Shelf, App Launcher, Quick Settings, Notifications).
 * Manages system-wide theme state (dark/light mode).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "zen/shell.h"
#include "zen/shelf.h"
#include "zen/theme-config.h"
#include "zen/app-launcher.h"
#include "zen/quick-settings.h"
#include "zen/notifications.h"
#include "zen/shell-testctl.h"

static const char *resolve_home_dir(void)
{
    const char *home = getenv("HOME");
    struct passwd *passwd_entry = NULL;

    if (home && home[0] != '\0') {
        return home;
    }

    passwd_entry = getpwuid(getuid());
    if (passwd_entry && passwd_entry->pw_dir
        && passwd_entry->pw_dir[0] != '\0') {
        return passwd_entry->pw_dir;
    }

    return NULL;
}

static const char *get_theme_config_path(char *path, size_t path_size)
{
    const char *home = resolve_home_dir();

    if (!home) {
        return NULL;
    }

    snprintf(path, path_size, "%s/.config/zenos/theme.json", home);
    return path;
}

int zen_shell_init(struct ZenShell *shell, struct ZenCompositor *compositor)
{
    char path[512];

    memset(shell, 0, sizeof(*shell));
    shell->compositor = compositor;

    shell->theme = calloc(1, sizeof(*shell->theme));
    if (!shell->theme)
        goto cleanup;

    zen_theme_config_load(shell->theme,
                          get_theme_config_path(path, sizeof(path)));
    shell->dark_mode = shell->theme->dark_mode;

    shell->shelf = calloc(1, sizeof(*shell->shelf));
    if (!shell->shelf)
        goto cleanup;

    if (zen_shelf_init(shell->shelf, shell) != 0)
        goto cleanup;

    /*
     * App Launcher — now has a real implementation (task 2.5.4).
     * Allocate and initialize; failure is non-fatal (launcher stays NULL).
     */
    shell->launcher = calloc(1, sizeof(*shell->launcher));
    if (shell->launcher) {
        if (zen_app_launcher_init(shell->launcher, shell) != 0) {
            free(shell->launcher);
            shell->launcher = NULL;
            wlr_log(WLR_ERROR, "shell: app launcher init failed (non-fatal)");
        }
    }

    /*
     * Quick Settings and Notifications — still stubs (tasks 2.6, 2.7).
     * The pointers remain NULL until those tasks are implemented.
     */
    (void)zen_quick_settings_init();
    (void)zen_notifications_init();
    zen_shell_testctl_sync(shell);

    return 0;

cleanup:
    zen_shell_destroy(shell);
    return -1;
}

void zen_shell_destroy(struct ZenShell *shell)
{
    if (shell->notifications) {
        zen_notifications_destroy();
        shell->notifications = NULL;
    }

    if (shell->quick_settings) {
        zen_quick_settings_destroy();
        shell->quick_settings = NULL;
    }

    if (shell->launcher) {
        zen_app_launcher_destroy(shell->launcher);
        free(shell->launcher);
        shell->launcher = NULL;
    }

    if (shell->shelf) {
        zen_shelf_destroy(shell->shelf);
        free(shell->shelf);
        shell->shelf = NULL;
    }

    if (shell->theme) {
        zen_theme_config_cleanup(shell->theme);
        free(shell->theme);
        shell->theme = NULL;
    }

    zen_shell_testctl_clear();
}

void zen_shell_set_dark_mode(struct ZenShell *shell, bool enabled)
{
    char path[512];
    const char *config_path = NULL;

    shell->dark_mode = enabled;
    shell->theme->dark_mode = enabled;

    config_path = get_theme_config_path(path, sizeof(path));
    if (config_path) {
        zen_theme_config_save(shell->theme, config_path);
    } else {
        wlr_log(WLR_INFO, "%s",
                "shell: HOME unavailable, skipping theme.json save");
    }

    zen_shell_repaint_all(shell);
    zen_shell_testctl_sync(shell);
}

void zen_shell_repaint_all(struct ZenShell *shell)
{
    if (shell->shelf)
        zen_shelf_repaint(shell->shelf);

    if (shell->launcher)
        zen_app_launcher_repaint(shell->launcher);

    /* quick_settings, notifications: stubs, no-op for now */
    if (shell->quick_settings)
        (void)shell->quick_settings;

    if (shell->notifications)
        (void)shell->notifications;

    zen_shell_testctl_sync(shell);
}
