#include "app_controls.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "mixer.h"
#include "cpu_load.h"
#include "audio_float.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

/* Paramètres encodeurs (0–127):
 * P0=master, P1=LOW EQ, P2=MID EQ, P3=HIGH EQ */
static int16_t params[4] = {127, 64, 64, 64};

/* Cache des dernières valeurs EQ (évite recalculs coûteux) */
static float last_low_db  = 0.0f;
static float last_mid_db  = 0.0f;
static float last_high_db = 0.0f;

static float param_to_gain(int16_t p)
{
    return ((float)p / 127.0f) * 2.0f;
}

static float param_to_eq_db(int16_t p)
{
    float x = ((float)p - 64.0f) / 63.0f;

    float sign = (x >= 0.0f) ? 1.0f : -1.0f;
    float mag = fabsf(x);

    float shaped = mag * mag;  // cheap non-linear

    return sign * shaped * 48.0f;  // range fort
}

void app_controls_init(void)
{
    drv_encoders_init();

    mixer_set_master(param_to_gain(params[0]));

    last_low_db  = param_to_eq_db(params[1]);
    last_mid_db  = param_to_eq_db(params[2]);
    last_high_db = param_to_eq_db(params[3]);

    audio_float_set_dj_eq_low_db(last_low_db);
    audio_float_set_dj_eq_mid_db(last_mid_db);
    audio_float_set_dj_eq_high_db(last_high_db);
}

void app_controls_process(void)
{
    drv_encoders_poll();

    uint8_t changed_master = 0U;
    uint8_t changed_low = 0U;
    uint8_t changed_mid = 0U;
    uint8_t changed_high = 0U;

    for(uint32_t i = 0; i < 4U; i++)
    {
        int16_t d = drv_encoder_get_delta(i);
        if(d == 0)
            continue;

        int16_t newv = params[i] + d;
        if(newv < 0)   newv = 0;
        if(newv > 127) newv = 127;

        if(newv != params[i])
        {
            params[i] = newv;
            if(i == 0) changed_master = 1U;
            if(i == 1) changed_low = 1U;
            if(i == 2) changed_mid = 1U;
            if(i == 3) changed_high = 1U;
        }
    }

    if(changed_master)
        mixer_set_master(param_to_gain(params[0]));

    if(changed_low)
    {
        float db = param_to_eq_db(params[1]);
        if(fabsf(db - last_low_db) > 0.1f)
        {
            last_low_db = db;
            audio_float_set_dj_eq_low_db(db);
        }
    }

    if(changed_mid)
    {
        float db = param_to_eq_db(params[2]);
        if(fabsf(db - last_mid_db) > 0.1f)
        {
            last_mid_db = db;
            audio_float_set_dj_eq_mid_db(db);
        }
    }

    if(changed_high)
    {
        float db = param_to_eq_db(params[3]);
        if(fabsf(db - last_high_db) > 0.1f)
        {
            last_high_db = db;
            audio_float_set_dj_eq_high_db(db);
        }
    }
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
