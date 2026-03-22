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
    UI_PAGE_MAIN = 0U,
    UI_PAGE_PARAM_TEST,
    UI_PAGE_HALL_KEY_DEBUG,
    UI_PAGE_CALIBRATION,
    UI_PAGE_USER_CALIBRATION,
    UI_PAGE_TEMPLATE_DX7,

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

#endif /* UI_PAGE_MANAGER_H */
