#include "pages/ui_page_param_test.h"

#include <stdio.h>

#include "drv_display.h"
#include "param_registry.h"
#include "ui_param.h"

static const ui_param_bank_t g_param_test_bank = {
    .params = {
        PARAM_DAISY_COMP_THRESHOLD_DB,
        PARAM_DAISY_COMP_RATIO,
        PARAM_DAISY_COMP_ATTACK_S,
        PARAM_DAISY_COMP_RELEASE_S,
    },
};

static void ui_page_param_test_enter(void)
{
    ui_param_set_bank(&g_param_test_bank);
}

static void ui_page_param_test_leave(void) {}

static void ui_page_param_test_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_param_test_tick(void) {}

static void ui_page_param_test_format_value(param_id_t id, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_DB:
            (void)snprintf(out, out_len, "%.1f %s", (double)value, desc->unit);
            break;

        case PARAM_DISPLAY_TIME_MS:
            (void)snprintf(out, out_len, "%.1f ms", (double)(value * 1000.0f));
            break;

        case PARAM_DISPLAY_RATIO:
            (void)snprintf(out, out_len, "%.2f", (double)value);
            break;

        default:
            if ((desc->unit != 0) && (desc->unit[0] != '\0'))
            {
                (void)snprintf(out, out_len, "%.2f %s", (double)value, desc->unit);
            }
            else
            {
                (void)snprintf(out, out_len, "%.2f", (double)value);
            }
            break;
    }
}

static void ui_page_param_test_render(void)
{
    for (uint8_t i = 0U; i < 4U; i++)
    {
        const param_id_t id = g_param_test_bank.params[i];
        char value_txt[20];
        char line_txt[32];

        ui_page_param_test_format_value(id, value_txt, (uint32_t)sizeof(value_txt));
        (void)snprintf(line_txt, sizeof(line_txt), "%s %s", param_registry[id].name, value_txt);

        drv_display_draw_text(0U, (uint8_t)(i * 16U), line_txt);
    }

}

const ui_page_t g_ui_page_param_test = {
    .enter = ui_page_param_test_enter,
    .leave = ui_page_param_test_leave,
    .handle_event = ui_page_param_test_handle_event,
    .tick = ui_page_param_test_tick,
    .render = ui_page_param_test_render,
};
