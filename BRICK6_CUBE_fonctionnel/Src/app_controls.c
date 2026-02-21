#include "app_controls.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "mixer.h"
#include "fx_warps.h"
#include <stdio.h>

/* Encoder 0: master gain (0-127) */
static int16_t param_master = 127;
/* Encoder 1: Warps algorithm (0-127) */
static int16_t param_algo = 0;
/* Encoder 2: Warps parameter/timbre (0-127) */
static int16_t param_warps = 127;
/* Encoder 3: Warps drive (0-127) */
static int16_t param_drive = 64;

static int16_t clamp_0_127(int16_t v)
{
    if(v < 0) return 0;
    if(v > 127) return 127;
    return v;
}

void app_controls_init(void)
{
    drv_encoders_init();

    {
        float norm = param_master / 127.0f;
        mixer_set_master(norm * 2.0f);
    }

    {
        float norm = param_algo / 127.0f;
        fx_warps_set_algorithm(norm);
    }

    {
        float norm = param_warps / 127.0f;
        fx_warps_set_parameter(norm);
    }

    {
        float d = param_drive / 127.0f;
        fx_warps_set_drive(d, d);
    }
}

void app_controls_process(void)
{
    drv_encoders_poll();

    {
        int16_t d0 = drv_encoder_get_delta(0);
        if(d0 != 0)
        {
            param_master = clamp_0_127(param_master + d0);

            float norm = param_master / 127.0f;
            mixer_set_master(norm * 2.0f);
        }
    }

    {
        int16_t d1 = drv_encoder_get_delta(1);
        if(d1 != 0)
        {
            param_algo = clamp_0_127(param_algo + d1);

            float norm = param_algo / 127.0f;
            fx_warps_set_algorithm(norm);
        }
    }

    {
        int16_t d2 = drv_encoder_get_delta(2);
        if(d2 != 0)
        {
            param_warps = clamp_0_127(param_warps + d2);

            float norm = param_warps / 127.0f;
            fx_warps_set_parameter(norm);
        }
    }

    {
        int16_t d3 = drv_encoder_get_delta(3);
        if(d3 != 0)
        {
            param_drive = clamp_0_127(param_drive + d3);

            float d = param_drive / 127.0f;
            fx_warps_set_drive(d, d);
        }
    }
}

void app_controls_render(void)
{
    char buf[32];

    drv_display_clear();

    snprintf(buf, sizeof(buf), "P0 M:%3d", (int)param_master);
    drv_display_draw_text(0, 0, buf);

    snprintf(buf, sizeof(buf), "P1 A:%3d", (int)param_algo);
    drv_display_draw_text(0, 12, buf);

    snprintf(buf, sizeof(buf), "P2 P:%3d", (int)param_warps);
    drv_display_draw_text(0, 24, buf);

    snprintf(buf, sizeof(buf), "P3 D:%3d", (int)param_drive);
    drv_display_draw_text(0, 36, buf);

    drv_display_update();
}
