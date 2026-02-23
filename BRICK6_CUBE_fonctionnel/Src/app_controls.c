#include "app_controls.h"
#include "app_controls_eq.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "cpu_load.h"
#include "fx_reverb.h"
#include <stdint.h>
#include <stdio.h>

/* Paramètres reverb (0–127):
 * P0=room size, P1=damping, P2=wet */
static int16_t params[3] = {64, 64, 64};
static float room_smoothed = 0.5f;
static float damp_smoothed = 0.5f;
static float wet_smoothed  = 0.5f;
static fx_reverb_t *reverb = 0;

static float param_to_norm(int16_t p)
{
    return (float)p * (1.0f / 127.0f);
}

static void update_reverb_params(uint8_t smooth)
{
    float room_target = param_to_norm(params[0]);
    float damp_target = param_to_norm(params[1]);
    float wet_target  = param_to_norm(params[2]);

    if(smooth)
    {
        room_smoothed += 0.05f * (room_target - room_smoothed);
        damp_smoothed += 0.05f * (damp_target - damp_smoothed);
        wet_smoothed  += 0.05f * (wet_target - wet_smoothed);
    }
    else
    {
        room_smoothed = room_target;
        damp_smoothed = damp_target;
        wet_smoothed  = wet_target;
    }

    fx_reverb_set_room_size(reverb, room_smoothed);
    fx_reverb_set_damping(reverb, damp_smoothed);
    fx_reverb_set_wet(reverb, wet_smoothed);

    if(params[2] == 0)
        fx_reverb_set_bypass(reverb, 1U);
    else
        fx_reverb_set_bypass(reverb, 0U);
}

void app_controls_init(void)
{
    drv_encoders_init();
    app_controls_eq_init();

    reverb = fx_reverb_get_instance();

    if(reverb)
        update_reverb_params(0U);
}

void app_controls_process(void)
{
    drv_encoders_poll();
    app_controls_eq_process();

    uint8_t changed = 0U;

    for(uint32_t i = 0; i < 3U; i++)
    {
        int16_t d = drv_encoder_get_delta((uint8_t)i);
        if(d == 0)
            continue;

        int16_t newv = params[i] + d;
        if(newv < 0)   newv = 0;
        if(newv > 127) newv = 127;

        if(newv != params[i])
        {
            params[i] = newv;
            changed = 1U;
        }
    }

    if(reverb && changed)
        update_reverb_params(1U);
}

void app_controls_render(void)
{
    char buf[32];
    const uint32_t cpu_pm = cpu_load_get_permille();

    drv_display_clear();

    snprintf(buf, sizeof(buf), "ROOM:%3d DAMP:%3d", (int)params[0], (int)params[1]);
    drv_display_draw_text(0, 0, buf);

    snprintf(buf, sizeof(buf), "WET:%3d", (int)params[2]);
    drv_display_draw_text(0, 10, buf);

    snprintf(buf, sizeof(buf), "CPU:%2lu.%1lu%%",
             (unsigned long)(cpu_pm / 10U),
             (unsigned long)(cpu_pm % 10U));
    drv_display_draw_text(0, 20, buf);

    drv_display_update();
}
