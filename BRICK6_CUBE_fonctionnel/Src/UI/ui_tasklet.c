#include "ui_tasklet.h"

#include <stdint.h>
#include <stdio.h>

#include "control_router.h"
#include "drv_display.h"
#include "drv_encoders.h"
#include "main.h"

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void ui_tasklet_poll(void)
{
    static uint8_t init = 0U;
    static uint32_t last_ui_ms = 0U;
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

    {
        const uint32_t now = HAL_GetTick();
        if((now - last_ui_ms) < 50U)
            return;

        last_ui_ms = now;

        char thr_txt[24];
        char rat_txt[24];
        char atk_txt[24];
        char rel_txt[24];

        snprintf(thr_txt, sizeof(thr_txt), "THR: %.1f dB", (double)threshold_db);
        snprintf(rat_txt, sizeof(rat_txt), "RAT: %.1f", (double)ratio);
        snprintf(atk_txt, sizeof(atk_txt), "ATK: %u", (unsigned int)attack_index);
        snprintf(rel_txt, sizeof(rel_txt), "REL: %u", (unsigned int)release_index);

        drv_display_clear_rect(0, 0, 80, 32);
        drv_display_draw_text(0, 0, thr_txt);
        drv_display_draw_text(0, 8, rat_txt);
        drv_display_draw_text(0, 16, atk_txt);
        drv_display_draw_text(0, 24, rel_txt);
        drv_display_update();
    }
}
