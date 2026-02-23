#include "app_controls.h"
#include "app_controls_eq.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "cpu_load.h"
#include "audio_float.h"
#include <stdint.h>
#include <stdio.h>

/* Paramètres encodeurs saturation (0–127):
 * P0=tone, P1=bias, P2=drive, P3=mix */
static int16_t params[4] = {0, 64, 0, 127};

void app_controls_init(void)
{
    drv_encoders_init();
    app_controls_eq_init();

    audio_float_set_saturation_tone_ui((uint8_t)params[0]);
    audio_float_set_saturation_bias_ui((uint8_t)params[1]);
    audio_float_set_saturation_drive_ui((uint8_t)params[2]);
    audio_float_set_saturation_mix_ui((uint8_t)params[3]);
}

void app_controls_process(void)
{
    drv_encoders_poll();
    app_controls_eq_process();

    uint8_t changed = 0U;

    for(uint32_t i = 0; i < 4U; i++)
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

    if(changed)
    {
        audio_float_set_saturation_tone_ui((uint8_t)params[0]);
        audio_float_set_saturation_bias_ui((uint8_t)params[1]);
        audio_float_set_saturation_drive_ui((uint8_t)params[2]);
        audio_float_set_saturation_mix_ui((uint8_t)params[3]);
    }
}

void app_controls_render(void)
{
    char buf[32];
    const uint32_t cpu_pm = cpu_load_get_permille();

    drv_display_clear();

    snprintf(buf, sizeof(buf), "TONE:%3d BIAS:%3d", (int)params[0], (int)params[1]);
    drv_display_draw_text(0, 0, buf);

    snprintf(buf, sizeof(buf), "DRV:%3d MIX:%3d", (int)params[2], (int)params[3]);
    drv_display_draw_text(0, 10, buf);

    snprintf(buf, sizeof(buf), "CPU:%2lu.%1lu%%",
             (unsigned long)(cpu_pm / 10U),
             (unsigned long)(cpu_pm % 10U));
    drv_display_draw_text(0, 20, buf);

    drv_display_update();
}
