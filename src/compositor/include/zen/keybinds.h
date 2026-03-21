#ifndef ZEN_COMPOSITOR_KEYBINDS_H
#define ZEN_COMPOSITOR_KEYBINDS_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct ZenCompositor;

typedef enum {
    ZEN_ACTION_LAUNCH_TERMINAL,
    ZEN_ACTION_TOGGLE_LAUNCHER,
    ZEN_ACTION_LOCK_SCREEN,
    ZEN_ACTION_SWITCH_WINDOW,
    ZEN_ACTION_CLOSE_WINDOW,
    ZEN_ACTION_CUSTOM,
    ZEN_ACTION_COUNT,
} ZenKeybindAction;

typedef struct {
    uint32_t         modifiers;  /* WLR_MODIFIER_* bitmask */
    xkb_keysym_t     keysym;
    ZenKeybindAction  action;
    char             *custom_cmd; /* only for ZEN_ACTION_CUSTOM */
    struct wl_list    link;       /* ZenCompositor.keybindings */
} ZenKeybinding;

int zen_keybinds_init(struct ZenCompositor *compositor);
void zen_keybinds_destroy(struct ZenCompositor *compositor);
bool zen_keybinds_handle_key(struct ZenCompositor *compositor, uint32_t modifiers, xkb_keysym_t keysym);
int zen_keybinds_load_config(struct ZenCompositor *compositor, const char *path);

#endif /* ZEN_COMPOSITOR_KEYBINDS_H */
