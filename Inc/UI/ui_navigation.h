#ifndef UI_NAVIGATION_H
#define UI_NAVIGATION_H

#include <stdint.h>

#include "buttons.h"
#include "ui_event.h"

#define UI_NAV_ANY_PAGE 255U

typedef struct
{
    button_id_t button;
    uint8_t required_page;
    uint8_t target_page;
} ui_nav_rule_t;

/*
 * Evaluate button press events against the navigation rules table and
 * perform page changes without hardcoding workflow in ui_core.
 */
void ui_navigation_handle_event(const ui_event_t *event);
uint8_t ui_navigation_is_ensemble_button_available(button_id_t button);
button_id_t ui_navigation_get_button_for_page(uint8_t page_id);
void ui_navigation_sync_active_track_ensemble(void);
void ui_navigation_sync_created_track_destination(void);
void ui_navigation_request_ensemble_page(uint8_t page_id);
void ui_navigation_request_page_with_availability(uint8_t page_id);

#endif /* UI_NAVIGATION_H */
