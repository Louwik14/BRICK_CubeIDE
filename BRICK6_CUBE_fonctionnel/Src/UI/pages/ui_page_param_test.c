#include "pages/ui_page_param_test.h"

#include <stdio.h>

#include "drv_display.h"
#include "param_registry.h"
#include "ui_param.h"

static const ui_param_bank_t s_param_test_bank = {
    .params = {
        PARAM_DAISY_COMP_THRESHOLD_DB,
        PARAM_DAISY_COMP_RATIO,
        PARAM_DAISY_COMP_ATTACK_S,
        PARAM_DAISY_COMP_RELEASE_S,
    }
};

void ui_page_param_test_enter(void)
{
    ui_param_set_bank(&s_param_test_bank);
}

void ui_page_param_test_leave(void)
{
}

void ui_page_param_test_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

void ui_page_param_test_tick(void)
{
}

void ui_page_param_test_render(void)
{
    char line[32];

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const param_id_t id = s_param_test_bank.params[i];
        const param_desc_t *desc = &param_registry[id];
        const float v = param_get(id);

        snprintf(line, sizeof(line), "%s %.2f", desc->name, (double)v);
        drv_display_draw_text(0, (uint8_t)(i * 8U), line);
    }
}

const ui_page_t g_ui_page_param_test = {
    .enter = ui_page_param_test_enter,
    .leave = ui_page_param_test_leave,
    .handle_event = ui_page_param_test_handle_event,
    .tick = ui_page_param_test_tick,
    .render = ui_page_param_test_render,
};
