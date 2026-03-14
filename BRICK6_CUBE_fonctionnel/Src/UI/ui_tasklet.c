#include "ui_tasklet.h"

#include <stdint.h>
#include <stdio.h>

#include "control_router.h"
#include "cpu_load.h"
#include "drv_display.h"
#include "drv_encoders.h"
#include "buttons.h"
#include "led_layer.h"
#include "led_ids.h"

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void ui_tasklet_poll(void)
{
    static uint8_t init = 0U;
    static uint32_t ui_tick = 0U;

    static uint8_t page = 0U;

    static float threshold_db = -18.0f;
    static float ratio = 2.0f;

    // Daisy attend des secondes
    static const float attack_values[6]  = {0.0001f, 0.0003f, 0.001f, 0.003f, 0.01f, 0.03f};
    static const float release_values[5] = {0.1f, 0.3f, 0.6f, 1.2f, 2.5f};

    static uint8_t attack_index = 2U;
    static uint8_t release_index = 2U;

    static float makeup_db = 0.0f;
    static float auto_makeup = 1.0f;

    static float mix = 1.0f; // NEW dry/wet

    if(!init)
    {
        init = 1U;
        drv_display_init();
        drv_encoders_init();

        control_router_set_param(CTRL_PARAM_DAISY_COMP_THRESHOLD_DB, threshold_db);
        control_router_set_param(CTRL_PARAM_DAISY_COMP_RATIO, ratio);
        control_router_set_param(CTRL_PARAM_DAISY_COMP_ATTACK_S, attack_values[attack_index]);
        control_router_set_param(CTRL_PARAM_DAISY_COMP_RELEASE_S, release_values[release_index]);
        control_router_set_param(CTRL_PARAM_DAISY_COMP_MAKEUP_DB, makeup_db);
        control_router_set_param(CTRL_PARAM_DAISY_COMP_AUTO_MAKEUP, auto_makeup);
        control_router_set_param(CTRL_PARAM_DAISY_COMP_MIX, mix);
    }

    // -------- ENCODERS --------
    drv_encoders_poll();

    const int16_t d0 = drv_encoder_get_delta(0U);
    const int16_t d1 = drv_encoder_get_delta(1U);
    const int16_t d2 = drv_encoder_get_delta(2U);
    const int16_t d3 = drv_encoder_get_delta(3U);

    // Page select
    if(d0 != 0)
    {
        int32_t p = (int32_t)page + (int32_t)d0;
        if(p < 0) p = 0;
        if(p > 2) p = 2;
        page = (uint8_t)p;
    }

    // -------- PAGE 0 --------
    if(page == 0U)
    {
        if(d1 != 0)
        {
            threshold_db = clampf(threshold_db + ((float)d1 * 0.5f), -40.0f, 0.0f);
            control_router_set_param(CTRL_PARAM_DAISY_COMP_THRESHOLD_DB, threshold_db);
        }

        if(d2 != 0)
        {
            ratio = clampf(ratio + ((float)d2 * 0.1f), 1.0f, 10.0f);
            control_router_set_param(CTRL_PARAM_DAISY_COMP_RATIO, ratio);
        }

        if(d3 != 0)
        {
            int32_t idx = (int32_t)attack_index + (int32_t)d3;
            if(idx < 0) idx = 0;
            if(idx > 5) idx = 5;
            attack_index = (uint8_t)idx;
            control_router_set_param(CTRL_PARAM_DAISY_COMP_ATTACK_S, attack_values[attack_index]);
        }
    }
    // -------- PAGE 1 --------
    else if(page == 1U)
    {
        if(d1 != 0)
        {
            int32_t idx = (int32_t)release_index + (int32_t)d1;
            if(idx < 0) idx = 0;
            if(idx > 4) idx = 4;
            release_index = (uint8_t)idx;
            control_router_set_param(CTRL_PARAM_DAISY_COMP_RELEASE_S, release_values[release_index]);
        }

        if(d2 != 0)
        {
            makeup_db = clampf(makeup_db + ((float)d2 * 0.5f), 0.0f, 24.0f);
            control_router_set_param(CTRL_PARAM_DAISY_COMP_MAKEUP_DB, makeup_db);
        }

        if(d3 != 0)
        {
            auto_makeup = (d3 > 0) ? 1.0f : 0.0f;
            control_router_set_param(CTRL_PARAM_DAISY_COMP_AUTO_MAKEUP, auto_makeup);
        }
    }
    // -------- PAGE 2 --------
    else
    {
        if(d1 != 0)
        {
            mix = clampf(mix + ((float)d1 * 0.05f), 0.0f, 1.0f);
            control_router_set_param(CTRL_PARAM_DAISY_COMP_MIX, mix);
        }


    }

    // -------- UI REFRESH --------
    ui_tick++;
    if(ui_tick < 20U)
        return;

    ui_tick = 0U;

    char line0[32];
    char line1[32];
    char line2[32];
    char line3[32];

    const uint32_t cpu_pm = cpu_load_get_permille();
    const uint32_t cpu_int = cpu_pm / 10U;

    snprintf(line0, sizeof(line0), "DAISY COMP CPU:%lu%% P:%u",
             (unsigned long)cpu_int,
             (unsigned int)page);

    if(page == 0U)
    {
        int thr_i = (int)threshold_db;
        int thr_d = (int)((threshold_db - thr_i) * 10.0f);
        if(thr_d < 0) thr_d = -thr_d;

        int rat_i = (int)ratio;
        int rat_d = (int)((ratio - rat_i) * 10.0f);
        if(rat_d < 0) rat_d = -rat_d;

        static const char* atk_str[6] = {"0.1","0.3","1","3","10","30"};

        snprintf(line1, sizeof(line1), "Threshold %d.%d dB", thr_i, thr_d);
        snprintf(line2, sizeof(line2), "Ratio     %d.%d:1", rat_i, rat_d);
        snprintf(line3, sizeof(line3), "Attack    %s ms", atk_str[attack_index]);
    }
    else if(page == 1U)
    {
        int mkp_i = (int)makeup_db;
        int mkp_d = (int)((makeup_db - mkp_i) * 10.0f);
        if(mkp_d < 0) mkp_d = -mkp_d;

        static const char* rel_str[5] = {"100","300","600","1200","2500"};

        snprintf(line1, sizeof(line1), "Release   %s ms", rel_str[release_index]);
        snprintf(line2, sizeof(line2), "Makeup    %d.%d dB", mkp_i, mkp_d);
        snprintf(line3, sizeof(line3), "Auto Mkup %s", auto_makeup >= 0.5f ? "ON" : "OFF");
    }
    else
    {
        int mix_pct = (int)(mix * 100.0f);

        snprintf(line1, sizeof(line1), "Mix       %d%%", mix_pct);
        snprintf(line2, sizeof(line2), " ");
        snprintf(line3, sizeof(line3), "Daisy Insert");
    }
    drv_display_clear_rect(0, 0, 96, 32);
    drv_display_draw_text(0, 0, line0);
    drv_display_draw_text(0, 8, line1);
    drv_display_draw_text(0, 16, line2);
    drv_display_draw_text(0, 24, line3);
    drv_display_update();
}
