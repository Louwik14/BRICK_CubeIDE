#include "pages/ui_page_main.h"

#include "drv_display.h"
#include "ui_param.h"

static const ui_param_bank_t g_main_bank = {
    .params = {
        PARAM_DAISY_COMP_THRESHOLD_DB,
        PARAM_DAISY_COMP_RATIO,
        PARAM_DAISY_COMP_ATTACK_S,
        PARAM_DAISY_COMP_RELEASE_S,
    },
};

void ui_page_main_enter(void)
{
    ui_param_set_bank(&g_main_bank);
}

void ui_page_main_leave(void) {}
void ui_page_main_handle_event(const ui_event_t *ev)
{
    (void)ev;
}
void ui_page_main_tick(void) {}
void ui_page_main_render(void)
{
    drv_display_draw_text(0U, 0U, "BRICK6 MAIN");
    drv_display_draw_text(0U, 16U, "BTN1: PARAM TEST");
    drv_display_draw_text(0U, 32U, "BTN2: MAIN PAGE");
}

const ui_page_t g_ui_page_main = {
    .enter = ui_page_main_enter,
    .leave = ui_page_main_leave,
    .handle_event = ui_page_main_handle_event,
    .tick = ui_page_main_tick,
    .render = ui_page_main_render,
};
