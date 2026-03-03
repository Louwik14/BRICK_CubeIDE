#include "ui_tasklet.h"

#include <stdint.h>
#include <stdio.h>

#include "control_router.h"
#include "cpu_load.h"
#include "drv_display.h"
#include "drv_encoders.h"

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void ui_tasklet_poll(void)
{
    static uint8_t init = 0U;
    static uint32_t ui_tick = 0U;
    static float threshold_db = -18.0f;
    static float ratio = 2.0f;
    static uint8_t attack_index = 2U;
    static uint8_t release_index = 2U;

    if(!init)
    {
        init = 1U;
        drv_display_init();
        drv_encoders_init();

        control_router_set_param(CTRL_PARAM_BUS_COMP_THRESHOLD_DB, threshold_db);
        control_router_set_param(CTRL_PARAM_BUS_COMP_RATIO, ratio);
        control_router_set_param(CTRL_PARAM_BUS_COMP_ATTACK_INDEX, (float)attack_index);
        control_router_set_param(CTRL_PARAM_BUS_COMP_RELEASE_INDEX, (float)release_index);
    }

    drv_encoders_poll();

    {
        const int16_t d0 = drv_encoder_get_delta(0U);
        const int16_t d1 = drv_encoder_get_delta(1U);
        const int16_t d2 = drv_encoder_get_delta(2U);
        const int16_t d3 = drv_encoder_get_delta(3U);

        if(d0 != 0)
        {
            threshold_db = clampf(threshold_db + ((float)d0 * 0.5f), -40.0f, 0.0f);
            control_router_set_param(CTRL_PARAM_BUS_COMP_THRESHOLD_DB, threshold_db);
        }

        if(d1 != 0)
        {
            ratio = clampf(ratio + ((float)d1 * 0.1f), 1.0f, 10.0f);
            control_router_set_param(CTRL_PARAM_BUS_COMP_RATIO, ratio);
        }

        if(d2 != 0)
        {
            int32_t idx = (int32_t)attack_index + (int32_t)d2;
            if(idx < 0)
                idx = 0;
            if(idx > 5)
                idx = 5;
            attack_index = (uint8_t)idx;
            control_router_set_param(CTRL_PARAM_BUS_COMP_ATTACK_INDEX, (float)attack_index);
        }

        if(d3 != 0)
        {
            int32_t idx = (int32_t)release_index + (int32_t)d3;
            if(idx < 0)
                idx = 0;
            if(idx > 4)
                idx = 4;
            release_index = (uint8_t)idx;
            control_router_set_param(CTRL_PARAM_BUS_COMP_RELEASE_INDEX, (float)release_index);
        }
    }

    ui_tick++;
    if(ui_tick >= 20U)
    {
        ui_tick = 0U;

        char cpu_txt[24];
        const uint32_t cpu_pm = cpu_load_get_permille();
        const uint32_t cpu_int = cpu_pm / 10U;
        const uint32_t cpu_dec = cpu_pm % 10U;

        snprintf(cpu_txt, sizeof(cpu_txt), "CPU: %lu.%lu%%",
                 (unsigned long)cpu_int,
                 (unsigned long)cpu_dec);

        drv_display_clear_rect(0, 0, 80, 8);
        drv_display_draw_text(0, 0, cpu_txt);
        drv_display_update();
    }
}
