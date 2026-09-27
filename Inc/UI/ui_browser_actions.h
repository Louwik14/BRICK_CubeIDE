#ifndef UI_BROWSER_ACTIONS_H
#define UI_BROWSER_ACTIONS_H

#include <stdint.h>
#include "buttons_ids.h"

typedef enum {
    UI_BROWSER_ACTION_NONE = 0,
    UI_BROWSER_ACTION_NEW,
    UI_BROWSER_ACTION_SAVE,
    UI_BROWSER_ACTION_LOAD,
    UI_BROWSER_ACTION_BLANK,
    UI_BROWSER_ACTION_RENAME,
    UI_BROWSER_ACTION_CLEAR
} ui_browser_action_t;

static inline ui_browser_action_t ui_browser_action_resolve(
    button_id_t button, uint8_t shift, uint8_t project)
{
    if (button < BTN_PAGE_1 || button > BTN_PAGE_4) return UI_BROWSER_ACTION_NONE;
    if (shift != 0U) {
        if (button == BTN_PAGE_1) return UI_BROWSER_ACTION_SAVE;
        if (project != 0U && button == BTN_PAGE_2) return UI_BROWSER_ACTION_BLANK;
        return UI_BROWSER_ACTION_NONE;
    }
    switch (button) {
        case BTN_PAGE_1: return UI_BROWSER_ACTION_NEW;
        case BTN_PAGE_2: return UI_BROWSER_ACTION_LOAD;
        case BTN_PAGE_3: return UI_BROWSER_ACTION_RENAME;
        case BTN_PAGE_4: return UI_BROWSER_ACTION_CLEAR;
        default: return UI_BROWSER_ACTION_NONE;
    }
}

static inline const char *ui_browser_action_label(ui_browser_action_t action,
                                                   uint8_t project)
{
    switch (action) {
        case UI_BROWSER_ACTION_NEW: return "NEW";
        case UI_BROWSER_ACTION_SAVE: return "SAVE";
        case UI_BROWSER_ACTION_LOAD: return "LOAD";
        case UI_BROWSER_ACTION_BLANK: return "BLANK";
        case UI_BROWSER_ACTION_RENAME: return "RENAME";
        case UI_BROWSER_ACTION_CLEAR: return project ? "ERASE" : "CLEAR";
        default: return "-";
    }
}
#endif
