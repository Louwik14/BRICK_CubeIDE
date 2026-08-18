#ifndef UI_PAGE_MANAGER_H
#define UI_PAGE_MANAGER_H

#include <stdint.h>

#include "ui_page.h"

/*
 * Stable page IDs used by navigation rules and page registration order.
 * Keep these values workflow-agnostic; rules map buttons to page IDs.
 */
enum
{
    UI_PAGE_PARAM_TEST = 0U,
    UI_PAGE_HALL_KEY_DEBUG,
    UI_PAGE_CALIBRATION,
    UI_PAGE_USER_CALIBRATION,
    UI_PAGE_TEMPLATE_ENV = 4U,
    UI_PAGE_TEMPLATE_CFG,
    UI_PAGE_TEMPLATE_REC_CFG,
    UI_PAGE_TEMPLATE_TONE,
    UI_PAGE_TEMPLATE_MOD,
    UI_PAGE_TEMPLATE_KEYBOARD,
    UI_PAGE_MIDI_FX,
    UI_PAGE_TEMPLATE_SEQ,
    UI_PAGE_TEMPLATE_MACRO,
    UI_PAGE_TEMPLATE_MIX,
    UI_PAGE_TEMPLATE_PLAY,
    UI_PAGE_AUDIO_REC = 15U,
    UI_PAGE_REC_EDIT,
    UI_PAGE_PATCH_ASSIGN,
    UI_PAGE_NAME_EDIT,
    UI_PAGE_SETTINGS,
    UI_PAGE_LOWCOST_BUTTON_TEST,
    UI_PAGE_RESERVED_DIAGNOSTIC,

    UI_PAGE_COUNT
};

/* Initialize internal static storage and reset active page state. */
void ui_page_manager_init(void);

/* Register pages during startup (typically from ui_core_init). */
void ui_page_manager_register(const ui_page_t *page);
/* Switch active page: leave(current) -> set id -> enter(new). */
void ui_page_set(uint8_t page_id);

/* Read active page pointer/id for event dispatch and rendering. */
const ui_page_t *ui_page_get(void);
uint8_t ui_page_get_id(void);
void ui_page_sync_active_context(void);

#endif /* UI_PAGE_MANAGER_H */
