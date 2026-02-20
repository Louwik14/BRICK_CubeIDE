#include "app_controls.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "mixer.h"
#include <stdio.h>

/* Paramètre unique pour l’instant (0–127) */
static int16_t param_val = 127;

void app_controls_init(void)
{
    drv_encoders_init();

    /* Init DSP */
    float norm = param_val / 127.0f;
    float master = norm * 2.0f;
    mixer_set_master(master);
}

void app_controls_process(void)
{
    drv_encoders_poll();

    int16_t d = drv_encoder_get_delta(0);

    if (d != 0)
    {
        int16_t newv = param_val + d;

        /* Clamp propre */
        if (newv < 0) newv = 0;
        if (newv > 127) newv = 127;

        param_val = newv;

        /* Normalisation (clé pour Mutable) */
        float norm = param_val / 127.0f;

        /* Mapping actuel → mixer */
        float master = norm * 2.0f;
        mixer_set_master(master);

        /* 👉 PLUS TARD :
           fx_set_param(norm);
        */
    }
}

void app_controls_render(void)
{
    char buf[32];

    drv_display_clear();

    snprintf(buf, sizeof(buf), "P0: %3d", (int)param_val);
    drv_display_draw_text(0, 0, buf);

    drv_display_update();
}
