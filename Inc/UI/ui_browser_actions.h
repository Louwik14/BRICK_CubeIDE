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
    UI_BROWSER_ACTION_CLEAR,
    UI_BROWSER_ACTION_INIT,
    UI_BROWSER_ACTION_RETURN,
    UI_BROWSER_ACTION_PREVIEW,
    UI_BROWSER_ACTION_REFRESH,
    UI_BROWSER_ACTION_UNLOAD,
    UI_BROWSER_ACTION_DELETE
} ui_browser_action_t;

typedef enum {
    UI_BROWSER_PATCH = 0,
    UI_BROWSER_PROJECT,
    UI_BROWSER_ASSET_SD,
    UI_BROWSER_ASSET_POOL
} ui_browser_context_t;

static inline ui_browser_action_t ui_browser_action_resolve_context(
    button_id_t button, uint8_t shift, ui_browser_context_t context)
{
    if (button < BTN_PAGE_1 || button > BTN_PAGE_4) return UI_BROWSER_ACTION_NONE;
    if (context == UI_BROWSER_ASSET_SD || context == UI_BROWSER_ASSET_POOL) {
        if (shift != 0U) {
            if (button == BTN_PAGE_2) return UI_BROWSER_ACTION_REFRESH;
            if (button == BTN_PAGE_3 && context == UI_BROWSER_ASSET_SD)
                return UI_BROWSER_ACTION_DELETE;
            return UI_BROWSER_ACTION_NONE;
        }
        switch (button) {
            case BTN_PAGE_1: return UI_BROWSER_ACTION_RETURN;
            case BTN_PAGE_2: return context == UI_BROWSER_ASSET_SD
                ? UI_BROWSER_ACTION_LOAD : UI_BROWSER_ACTION_UNLOAD;
            case BTN_PAGE_3: return UI_BROWSER_ACTION_RENAME;
            case BTN_PAGE_4: return UI_BROWSER_ACTION_PREVIEW;
            default: return UI_BROWSER_ACTION_NONE;
        }
    }
    if (shift != 0U) {
        if (button == BTN_PAGE_4) return UI_BROWSER_ACTION_SAVE;
        if (button == BTN_PAGE_2) return context == UI_BROWSER_PROJECT
            ? UI_BROWSER_ACTION_BLANK : UI_BROWSER_ACTION_INIT;
        if (button == BTN_PAGE_3) return UI_BROWSER_ACTION_RENAME;
        return UI_BROWSER_ACTION_NONE;
    }
    switch (button) {
        case BTN_PAGE_1: return UI_BROWSER_ACTION_RETURN;
        case BTN_PAGE_2: return UI_BROWSER_ACTION_LOAD;
        case BTN_PAGE_3: return UI_BROWSER_ACTION_CLEAR;
        case BTN_PAGE_4: return UI_BROWSER_ACTION_NEW;
        default: return UI_BROWSER_ACTION_NONE;
    }
}

static inline ui_browser_action_t ui_browser_action_resolve(
    button_id_t button, uint8_t shift, uint8_t project)
{
    return ui_browser_action_resolve_context(button, shift,
        project != 0U ? UI_BROWSER_PROJECT : UI_BROWSER_PATCH);
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
        case UI_BROWSER_ACTION_INIT: return "INIT";
        case UI_BROWSER_ACTION_RETURN: return "RETURN";
        case UI_BROWSER_ACTION_PREVIEW: return "PREVIEW";
        case UI_BROWSER_ACTION_REFRESH: return "REFRESH";
        case UI_BROWSER_ACTION_UNLOAD: return "UNLOAD";
        case UI_BROWSER_ACTION_DELETE: return "DELETE";
        default: return "-";
    }
}
#endif
