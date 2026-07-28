#ifndef UI_CORE_NAVIGATION_BRIDGE_H
#define UI_CORE_NAVIGATION_BRIDGE_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_event.h"

void ui_core_navigation_bridge_handle_event(const ui_event_t *ev);
void ui_core_navigation_bridge_request_hall_mode_page(ui_hall_mode_t target_mode,
                                                      uint8_t target_page,
                                                      uint8_t open_target_page);
void ui_core_navigation_bridge_request_cfg_page(void);
void ui_core_navigation_bridge_open_rec_cfg_page(void);
void ui_core_navigation_bridge_sync_active_track_ensemble(void);
void ui_core_navigation_bridge_sync_created_track_destination(void);

#endif /* UI_CORE_NAVIGATION_BRIDGE_H */
