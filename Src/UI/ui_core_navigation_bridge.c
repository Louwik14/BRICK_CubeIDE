#include "ui_core_navigation_bridge.h"

#include "ui_page_manager.h"
#include "ui_navigation.h"
#include "pages/ui_page_template_cfg.h"

#define UI_CORE_NAVIGATION_BRIDGE_TARGET_PAGE_NONE 0xFFU

void ui_core_navigation_bridge_handle_event(const ui_event_t *ev)
{
    ui_navigation_handle_event(ev);
}

void ui_core_navigation_bridge_request_hall_mode_page(ui_hall_mode_t target_mode,
                                                      uint8_t target_page,
                                                      uint8_t open_target_page)
{
    if ((target_mode == UI_HALL_MODE_AUDIO_REC)
            && (target_page != UI_CORE_NAVIGATION_BRIDGE_TARGET_PAGE_NONE))
    {
        ui_navigation_request_page_with_availability(target_page);
        return;
    }

    if ((open_target_page != 0U) && (target_page != UI_CORE_NAVIGATION_BRIDGE_TARGET_PAGE_NONE))
    {
        ui_navigation_request_page_with_availability(target_page);
    }
}

void ui_core_navigation_bridge_request_cfg_page(void)
{
    ui_navigation_request_ensemble_page(UI_PAGE_TEMPLATE_CFG);
}

void ui_core_navigation_bridge_open_rec_cfg_page(void)
{
    ui_page_template_rec_cfg_open_main();
    ui_navigation_request_page_with_availability(UI_PAGE_TEMPLATE_REC_CFG);
}

void ui_core_navigation_bridge_sync_active_track_ensemble(void)
{
    ui_navigation_sync_active_track_ensemble();
}

void ui_core_navigation_bridge_sync_created_track_destination(void)
{
    ui_navigation_sync_created_track_destination();
}
