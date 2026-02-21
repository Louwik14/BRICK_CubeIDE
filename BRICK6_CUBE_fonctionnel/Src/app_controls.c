#include "app_controls.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "mixer.h"
#include "cpu_load.h"
#include "audio_float.h"
#include <stdint.h>
#include <stdio.h>

/* Paramètres encodeurs (0–127):
 * P0=master, P1=LOW EQ, P2=MID EQ, P3=HIGH EQ */
static int16_t params[4] = {127, 64, 64, 64};

static float param_to_gain(int16_t p)
{
    return ((float)p / 127.0f) * 2.0f;
}

static float param_to_eq_db(int16_t p)
{
    const float range_db = 12.0f;
    float db = ((float)p - 64.0f) * (range_db / 63.0f);

    if(p <= 2)
        db = -60.0f;

    if(db < -60.0f)
        db = -60.0f;
    if(db > 12.0f)
        db = 12.0f;

    return db;
}

void app_controls_init(void)
{
    drv_encoders_init();

    mixer_set_master(param_to_gain(params[0]));
    audio_float_set_dj_eq_low_db(param_to_eq_db(params[1]));
    audio_float_set_dj_eq_mid_db(param_to_eq_db(params[2]));
    audio_float_set_dj_eq_high_db(param_to_eq_db(params[3]));
}

void app_controls_process(void)
{
    drv_encoders_poll();

    for(uint32_t i = 0; i < 4U; i++)
    {
        int16_t d = drv_encoder_get_delta(i);

        if(d != 0)
        {
            int16_t newv = params[i] + d;

            if(newv < 0)
                newv = 0;
            if(newv > 127)
                newv = 127;

            params[i] = newv;
        }
    }

    mixer_set_master(param_to_gain(params[0]));
    audio_float_set_dj_eq_low_db(param_to_eq_db(params[1]));
    audio_float_set_dj_eq_mid_db(param_to_eq_db(params[2]));
    audio_float_set_dj_eq_high_db(param_to_eq_db(params[3]));
}

void app_controls_render(void)
{
    char buf[32];
    const uint32_t cpu_pm = cpu_load_get_permille();

    drv_display_clear();

    snprintf(buf, sizeof(buf), "P0:%3d P1:%3d", (int)params[0], (int)params[1]);
    drv_display_draw_text(0, 0, buf);

    snprintf(buf, sizeof(buf), "P2:%3d P3:%3d", (int)params[2], (int)params[3]);
    drv_display_draw_text(0, 10, buf);

    snprintf(buf, sizeof(buf), "CPU:%2lu.%1lu%%", 
             (unsigned long)(cpu_pm / 10U),
             (unsigned long)(cpu_pm % 10U));
    drv_display_draw_text(0, 20, buf);

    drv_display_update();
}
