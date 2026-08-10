/*
 * Zen OS Shell — App Launcher
 *
 * Fullscreen overlay grid of installed applications with real-time
 * search filtering.  Rendered via Cairo + Pango; translucent
 * semi-transparent background (TODO: SceneFX Gaussian blur when
 * SceneFX scene_buffer blur API is wired up).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <cairo.h>
#include <ctype.h>
#include <pango/pangocairo.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "zen/app-launcher.h"
#include "zen/cairo_buffer.h"
#include "zen/compositor.h"
#include "zen/desktop-entry.h"
#include "zen/shell.h"
#include "zen/shell-testctl.h"
#include "zen/theme-config.h"

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

/* ── Layout constants ────────────────────────────────────────────────────── */

#define ICON_SIZE   64
#define CELL_W     120
#define CELL_H     100
#define SEARCH_H    60
#define PADDING     20

/* XKB keysym values (subset used here — avoids pulling in xkbcommon headers) */
#define XKB_KEY_Escape    0xff1b
#define XKB_KEY_Return    0xff0d
#define XKB_KEY_Up        0xff52
#define XKB_KEY_Down      0xff54
#define XKB_KEY_Left      0xff51
#define XKB_KEY_Right     0xff53
#define XKB_KEY_BackSpace 0xff08

/* Scan directories for .desktop files (NULL-terminated). */
/* Note: actual dirs are built dynamically in do_scan() with $HOME expansion. */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Case-insensitive substring search. */
static bool str_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    if (*needle == '\0') return true;

    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0)
            return true;
    }
    return false;
}

/* Rebuild filtered[] from entries[] based on current search_text. */
static void filter_entries(struct ZenAppLauncher *launcher)
{
    free(launcher->filtered);
    launcher->filtered       = NULL;
    launcher->filtered_count = 0;

    if (launcher->entry_count == 0)
        return;

    launcher->filtered = calloc((size_t)launcher->entry_count,
                                sizeof(*launcher->filtered));
    if (!launcher->filtered)
        return;

    int count = 0;
    const char *query = launcher->search_text;

    for (int i = 0; i < launcher->entry_count; i++) {
        struct ZenDesktopEntry *e = &launcher->entries[i];

        if (*query == '\0') {
            /* No filter — show all visible entries */
            launcher->filtered[count++] = e;
            continue;
        }

        /* Match against name */
        if (str_contains_ci(e->name, query)) {
            launcher->filtered[count++] = e;
            continue;
        }

        /* Match against keywords */
        bool matched = false;
        for (int k = 0; k < e->keywords_count && !matched; k++) {
            if (str_contains_ci(e->keywords[k], query))
                matched = true;
        }
        if (matched) {
            launcher->filtered[count++] = e;
        }
    }

    launcher->filtered_count = count;

    /* Clamp selected_index to new bounds */
    if (launcher->selected_index >= count)
        launcher->selected_index = count > 0 ? count - 1 : 0;
}

/* Recalculate grid_cols and grid_rows from current filtered_count and
 * primary output width. */
static void recalc_grid(struct ZenAppLauncher *launcher, int out_w)
{
    int cols = (out_w - 2 * PADDING) / CELL_W;
    if (cols < 1)  cols = 1;
    if (cols > 10) cols = 10;
    launcher->grid_cols = cols;

    if (launcher->filtered_count == 0) {
        launcher->grid_rows = 0;
    } else {
        launcher->grid_rows = (launcher->filtered_count + cols - 1) / cols;
    }
}

/* Draw a placeholder icon: filled circle in accent color + first letter. */
static void draw_placeholder_icon(cairo_t *cr,
                                  const struct ZenColorPalette *pal,
                                  const char *name,
                                  double cx, double cy, double r)
{
    /* Circle background using accent color */
    cairo_arc(cr, cx, cy, r, 0, 2 * 3.14159265358979323846);
    cairo_set_source_rgba(cr,
        (double)pal->accent[0],
        (double)pal->accent[1],
        (double)pal->accent[2],
        (double)pal->accent[3]);
    cairo_fill(cr);

    /* First letter in foreground color */
    if (name && *name) {
        char letter[2] = { (char)toupper((unsigned char)*name), '\0' };
        cairo_set_source_rgba(cr,
            (double)pal->foreground[0],
            (double)pal->foreground[1],
            (double)pal->foreground[2],
            (double)pal->foreground[3]);
        cairo_select_font_face(cr, "Sans",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, r * 0.9);
        cairo_text_extents_t te;
        cairo_text_extents(cr, letter, &te);
        cairo_move_to(cr,
                      cx - te.width / 2.0 - te.x_bearing,
                      cy - te.height / 2.0 - te.y_bearing);
        cairo_show_text(cr, letter);
    }
}

/* Render the full overlay onto launcher->overlay_buffer. */
static void render_overlay(struct ZenAppLauncher *launcher)
{
    if (!launcher->overlay_buffer) return;

    struct ZenCairoBuffer *buf = launcher->overlay_buffer;
    cairo_t *cr = buf->cr;
    int w = buf->width;

    const struct ZenColorPalette *pal = NULL;
    if (launcher->shell && launcher->shell->theme)
        pal = zen_theme_config_get_palette(launcher->shell->theme);

    /* ── Background ─────────────────────────────────────────────────────── */
    /*
     * TODO: Replace with SceneFX translucent Gaussian blur background once
     * the SceneFX scene_buffer blur API is wired up (task 2.5.4 note).
     * For now, use a solid semi-transparent dark overlay.
     */
    if (pal) {
        cairo_set_source_rgba(cr,
            (double)pal->background[0],
            (double)pal->background[1],
            (double)pal->background[2],
            0.92);
    } else {
        cairo_set_source_rgba(cr, 0.102, 0.102, 0.180, 0.92);
    }
    cairo_paint(cr);

    /* ── Search field ───────────────────────────────────────────────────── */
    double sf_x = PADDING;
    double sf_y = PADDING;
    double sf_w = w - 2 * PADDING;
    double sf_h = SEARCH_H - PADDING / 2;
    double sf_r = 8.0; /* corner radius */

    /* Rounded rect for search field */
    float surf_r = pal ? pal->surface[0] : 0.145f;
    float surf_g = pal ? pal->surface[1] : 0.145f;
    float surf_b = pal ? pal->surface[2] : 0.271f;
    float surf_a = pal ? pal->surface[3] : 0.95f;

    cairo_set_source_rgba(cr, surf_r, surf_g, surf_b, surf_a);
    cairo_new_path(cr);
    cairo_arc(cr, sf_x + sf_r,        sf_y + sf_r,        sf_r, 3.14159 * 1.0, 3.14159 * 1.5);
    cairo_arc(cr, sf_x + sf_w - sf_r, sf_y + sf_r,        sf_r, 3.14159 * 1.5, 3.14159 * 2.0);
    cairo_arc(cr, sf_x + sf_w - sf_r, sf_y + sf_h - sf_r, sf_r, 0,             3.14159 * 0.5);
    cairo_arc(cr, sf_x + sf_r,        sf_y + sf_h - sf_r, sf_r, 3.14159 * 0.5, 3.14159 * 1.0);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Search text or placeholder */
    PangoLayout *layout = zen_cairo_buffer_get_pango(buf);
    PangoFontDescription *font_desc = pango_font_description_from_string("Sans 14");
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    const char *search_display = launcher->search_text[0]
        ? launcher->search_text
        : "Search applications...";

    float fg_r, fg_g, fg_b, fg_a;
    if (launcher->search_text[0]) {
        fg_r = pal ? pal->foreground[0] : 0.878f;
        fg_g = pal ? pal->foreground[1] : 0.878f;
        fg_b = pal ? pal->foreground[2] : 0.878f;
        fg_a = pal ? pal->foreground[3] : 1.0f;
    } else {
        fg_r = pal ? pal->text_secondary[0] : 0.533f;
        fg_g = pal ? pal->text_secondary[1] : 0.533f;
        fg_b = pal ? pal->text_secondary[2] : 0.533f;
        fg_a = pal ? pal->text_secondary[3] : 1.0f;
    }

    cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
    pango_layout_set_text(layout, search_display, -1);
    pango_layout_set_width(layout, (int)((sf_w - 20) * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    int text_h;
    pango_layout_get_pixel_size(layout, NULL, &text_h);
    cairo_move_to(cr, sf_x + 12, sf_y + (sf_h - text_h) / 2.0);
    pango_cairo_show_layout(cr, layout);

    /* Text cursor (only when search_text is non-empty) */
    if (launcher->search_text[0]) {
        int text_w;
        pango_layout_get_pixel_size(layout, &text_w, NULL);
        double cursor_x = sf_x + 12 + text_w + 2;
        double cursor_y = sf_y + (sf_h - text_h) / 2.0;
        cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, cursor_x, cursor_y);
        cairo_line_to(cr, cursor_x, cursor_y + text_h);
        cairo_stroke(cr);
    }

    /* ── Application grid ───────────────────────────────────────────────── */
    int grid_top = SEARCH_H + PADDING;

    if (launcher->filtered_count == 0) {
        /* "No applications found" message */
        PangoFontDescription *msg_font = pango_font_description_from_string("Sans 16");
        pango_layout_set_font_description(layout, msg_font);
        pango_font_description_free(msg_font);
        pango_layout_set_text(layout, "No applications found", -1);
        pango_layout_set_width(layout, -1);

        int mw, mh;
        pango_layout_get_pixel_size(layout, &mw, &mh);

        float sec_r = pal ? pal->text_secondary[0] : 0.533f;
        float sec_g = pal ? pal->text_secondary[1] : 0.533f;
        float sec_b = pal ? pal->text_secondary[2] : 0.533f;
        cairo_set_source_rgba(cr, sec_r, sec_g, sec_b, 1.0);
        cairo_move_to(cr, (w - mw) / 2.0, grid_top + 40);
        pango_cairo_show_layout(cr, layout);
    } else {
        recalc_grid(launcher, w);

        PangoFontDescription *name_font = pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(layout, name_font);
        pango_font_description_free(name_font);

        for (int i = 0; i < launcher->filtered_count; i++) {
            struct ZenDesktopEntry *e = launcher->filtered[i];
            int col = i % launcher->grid_cols;
            int row = i / launcher->grid_cols;

            double cell_x = PADDING + col * CELL_W;
            double cell_y = grid_top + row * CELL_H;

            /* Highlight selected item */
            if (i == launcher->selected_index) {
                float acc_r = pal ? pal->accent[0] : 0.290f;
                float acc_g = pal ? pal->accent[1] : 0.620f;
                float acc_b = pal ? pal->accent[2] : 1.0f;
                cairo_set_source_rgba(cr, acc_r, acc_g, acc_b, 0.25);
                cairo_rectangle(cr, cell_x, cell_y, CELL_W, CELL_H);
                cairo_fill(cr);
            }

            /* Icon: placeholder circle + first letter */
            double icon_cx = cell_x + CELL_W / 2.0;
            double icon_cy = cell_y + ICON_SIZE / 2.0 + 8;
            draw_placeholder_icon(cr, pal ? pal : &(struct ZenColorPalette){
                .accent     = {0.290f, 0.620f, 1.0f, 1.0f},
                .foreground = {0.878f, 0.878f, 0.878f, 1.0f},
            }, e->name, icon_cx, icon_cy, ICON_SIZE / 2.0);

            /* App name below icon */
            pango_layout_set_text(layout, e->name ? e->name : "", -1);
            pango_layout_set_width(layout, (CELL_W - 8) * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

            int nw, nh;
            pango_layout_get_pixel_size(layout, &nw, &nh);

            float name_r = pal ? pal->foreground[0] : 0.878f;
            float name_g = pal ? pal->foreground[1] : 0.878f;
            float name_b = pal ? pal->foreground[2] : 0.878f;
            cairo_set_source_rgba(cr, name_r, name_g, name_b, 1.0);
            cairo_move_to(cr, cell_x + 4, cell_y + ICON_SIZE + 12);
            pango_cairo_show_layout(cr, layout);
        }
    }

    zen_cairo_buffer_submit(buf);
}

/* ── Scan helper ─────────────────────────────────────────────────────────── */

static int do_scan(struct ZenAppLauncher *launcher)
{
    /* Build scan dirs array with expanded $HOME */
    char home_dir[512] = {0};
    const char *home = resolve_home_dir();
    if (home)
        snprintf(home_dir, sizeof(home_dir),
                 "%s/.local/share/applications", home);

    const char *dirs[5];
    int di = 0;
    if (home_dir[0])
        dirs[di++] = home_dir;
    dirs[di++] = "/var/lib/flatpak/exports/share/applications";
    dirs[di++] = "/usr/share/applications";
    dirs[di]   = NULL;

    struct ZenDesktopEntry *all   = NULL;
    int                     total = 0;

    if (zen_desktop_entry_scan(dirs, &all, &total) < 0) {
        wlr_log(WLR_ERROR, "%s", "app-launcher: desktop entry scan failed");
        return -1;
    }

    /* Keep only visible entries */
    struct ZenDesktopEntry *visible = NULL;
    int vis_count = 0;

    if (total > 0) {
        visible = calloc((size_t)total, sizeof(*visible));
        if (!visible) {
            for (int i = 0; i < total; i++)
                zen_desktop_entry_cleanup(&all[i]);
            free(all);
            return -1;
        }

        for (int i = 0; i < total; i++) {
            if (zen_desktop_entry_is_visible(&all[i])) {
                visible[vis_count++] = all[i];
                /* ownership transferred — zero out source so cleanup skips it */
                memset(&all[i], 0, sizeof(all[i]));
            } else {
                zen_desktop_entry_cleanup(&all[i]);
            }
        }
    }

    free(all);

    /* Replace existing entries */
    if (launcher->entries) {
        for (int i = 0; i < launcher->entry_count; i++)
            zen_desktop_entry_cleanup(&launcher->entries[i]);
        free(launcher->entries);
    }

    launcher->entries     = visible;
    launcher->entry_count = vis_count;
    return 0;
}

/* ── Launch helper ───────────────────────────────────────────────────────── */

static void launch_selected(struct ZenAppLauncher *launcher)
{
    if (launcher->filtered_count == 0) return;
    if (launcher->selected_index < 0 ||
        launcher->selected_index >= launcher->filtered_count) return;

    struct ZenDesktopEntry *e = launcher->filtered[launcher->selected_index];
    if (!e || !e->exec) return;

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: exec the application */
        setsid();
        execl("/bin/sh", "sh", "-c", e->exec, (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        /* Parent: reap asynchronously */
        waitpid(pid, NULL, WNOHANG);
    } else {
        wlr_log(WLR_ERROR, "app-launcher: fork failed for '%s'", e->exec);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int zen_app_launcher_init(struct ZenAppLauncher *launcher,
                          struct ZenShell *shell)
{
    int ret = -1;

    if (!launcher || !shell) return -1;
    memset(launcher, 0, sizeof(*launcher));

    launcher->shell          = shell;
    launcher->selected_index = 0;
    launcher->visible        = false;

    /* Scan .desktop files */
    if (do_scan(launcher) < 0)
        goto cleanup;

    /* Build initial filter (all visible entries) */
    filter_entries(launcher);

    /* Determine primary output dimensions */
    struct ZenCompositor *comp = shell->compositor;
    int out_w = 1920;
    int out_h = 1080;

    if (comp && comp->output_layout) {
        struct wlr_box ext = {0};
        wlr_output_layout_get_box(comp->output_layout, NULL, &ext);
        if (ext.width > 0 && ext.height > 0) {
            out_w = ext.width;
            out_h = ext.height;
        }
    }

    recalc_grid(launcher, out_w);

    /* Create fullscreen overlay buffer (hidden initially) */
    if (comp && comp->renderer && comp->launcher_tree) {
        launcher->overlay_buffer = calloc(1, sizeof(*launcher->overlay_buffer));
        if (!launcher->overlay_buffer)
            goto cleanup;

        if (zen_cairo_buffer_create(launcher->overlay_buffer,
                                    comp->renderer,
                                    comp->launcher_tree,
                                    out_w, out_h) < 0) {
            free(launcher->overlay_buffer);
            launcher->overlay_buffer = NULL;
            goto cleanup;
        }

        /* Hide the scene node until shown */
        wlr_scene_node_set_enabled(
            &launcher->overlay_buffer->scene_buffer->node, false);
    }

    ret = 0;
    zen_shell_testctl_sync(shell);

cleanup:
    if (ret < 0) {
        free(launcher->filtered);
        launcher->filtered = NULL;
        if (launcher->entries) {
            for (int i = 0; i < launcher->entry_count; i++)
                zen_desktop_entry_cleanup(&launcher->entries[i]);
            free(launcher->entries);
            launcher->entries = NULL;
        }
    }
    return ret;
}

void zen_app_launcher_destroy(struct ZenAppLauncher *launcher)
{
    if (!launcher) return;

    if (launcher->overlay_buffer) {
        zen_cairo_buffer_destroy(launcher->overlay_buffer);
        free(launcher->overlay_buffer);
        launcher->overlay_buffer = NULL;
    }

    free(launcher->filtered);
    launcher->filtered       = NULL;
    launcher->filtered_count = 0;

    if (launcher->entries) {
        for (int i = 0; i < launcher->entry_count; i++)
            zen_desktop_entry_cleanup(&launcher->entries[i]);
        free(launcher->entries);
        launcher->entries     = NULL;
        launcher->entry_count = 0;
    }
}

void zen_app_launcher_toggle(struct ZenAppLauncher *launcher)
{
    if (!launcher) return;
    if (launcher->visible)
        zen_app_launcher_hide(launcher);
    else
        zen_app_launcher_show(launcher);
}

void zen_app_launcher_show(struct ZenAppLauncher *launcher)
{
    if (!launcher) return;
    launcher->visible = true;

    if (launcher->overlay_buffer && launcher->overlay_buffer->scene_buffer) {
        wlr_scene_node_set_enabled(
            &launcher->overlay_buffer->scene_buffer->node, true);
    }

    zen_app_launcher_repaint(launcher);
    zen_shell_testctl_sync(launcher->shell);

    /* TODO: Give keyboard focus to search field via wlr_seat */
}

void zen_app_launcher_hide(struct ZenAppLauncher *launcher)
{
    if (!launcher) return;
    launcher->visible = false;

    if (launcher->overlay_buffer && launcher->overlay_buffer->scene_buffer) {
        wlr_scene_node_set_enabled(
            &launcher->overlay_buffer->scene_buffer->node, false);
    }

    /* Reset search state */
    memset(launcher->search_text, 0, sizeof(launcher->search_text));
    launcher->selected_index = 0;
    filter_entries(launcher);
    zen_shell_testctl_sync(launcher->shell);
}

void zen_app_launcher_repaint(struct ZenAppLauncher *launcher)
{
    if (!launcher || !launcher->visible) return;
    render_overlay(launcher);
    zen_shell_testctl_sync(launcher->shell);
}

bool zen_app_launcher_handle_key(struct ZenAppLauncher *launcher,
                                 uint32_t keysym, uint32_t modifiers)
{
    (void)modifiers;
    if (!launcher || !launcher->visible) return false;

    switch (keysym) {
    case XKB_KEY_Escape:
        zen_app_launcher_hide(launcher);
        return true;

    case XKB_KEY_Return:
        launch_selected(launcher);
        zen_app_launcher_hide(launcher);
        return true;

    case XKB_KEY_BackSpace: {
        size_t len = strlen(launcher->search_text);
        if (len > 0) {
            launcher->search_text[len - 1] = '\0';
            filter_entries(launcher);
            zen_app_launcher_repaint(launcher);
        }
        return true;
    }

    case XKB_KEY_Up:
        if (launcher->grid_cols > 0 &&
            launcher->selected_index >= launcher->grid_cols) {
            launcher->selected_index -= launcher->grid_cols;
            zen_app_launcher_repaint(launcher);
        }
        return true;

    case XKB_KEY_Down:
        if (launcher->grid_cols > 0 &&
            launcher->selected_index + launcher->grid_cols <
                launcher->filtered_count) {
            launcher->selected_index += launcher->grid_cols;
            zen_app_launcher_repaint(launcher);
        }
        return true;

    case XKB_KEY_Left:
        if (launcher->selected_index > 0) {
            launcher->selected_index--;
            zen_app_launcher_repaint(launcher);
        }
        return true;

    case XKB_KEY_Right:
        if (launcher->selected_index + 1 < launcher->filtered_count) {
            launcher->selected_index++;
            zen_app_launcher_repaint(launcher);
        }
        return true;

    default:
        return false;
    }
}

void zen_app_launcher_handle_text(struct ZenAppLauncher *launcher,
                                  const char *text)
{
    if (!launcher || !text || !launcher->visible) return;

    size_t cur_len = strlen(launcher->search_text);
    size_t add_len = strlen(text);

    if (cur_len + add_len >= sizeof(launcher->search_text) - 1)
        return; /* buffer full */

    strncat(launcher->search_text,
            text,
            sizeof(launcher->search_text) - cur_len - 1);

    filter_entries(launcher);
    zen_app_launcher_repaint(launcher);
}

int zen_app_launcher_rescan(struct ZenAppLauncher *launcher)
{
    if (!launcher) return -1;

    if (do_scan(launcher) < 0)
        return -1;

    filter_entries(launcher);

    if (launcher->visible)
        zen_app_launcher_repaint(launcher);
    else
        zen_shell_testctl_sync(launcher->shell);

    return 0;
}
