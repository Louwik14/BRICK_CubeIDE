#include "app_controls.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "mixer.h"
#include "fx_warps.h"
#include <stdio.h>

/* Encoder 1: master gain (0-127) */
static int16_t param_master = 127;
/* Encoder 0: Warps algorithm (0-127) */
static int16_t param_algo = 0;
/* Encoder 2: Warps parameter/timbre (0-127) */
static int16_t param_warps = 127;
/* Encoder 3: Warps dry/wet (0-127) */
static int16_t param_drywet = 0;

static int16_t clamp_0_127(int16_t v)
{
    if(v < 0) return 0;
    if(v > 127) return 127;
    return v;
}

void app_controls_init(void)
{
    drv_encoders_init();

    mixer_set_master((param_master / 127.0f) * 2.0f);
    fx_warps_set_algorithm(param_algo / 127.0f);
    fx_warps_set_parameter(param_warps / 127.0f);
    fx_warps_set_drywet(param_drywet / 127.0f);
}

void app_controls_process(void)
{
    drv_encoders_poll();

    {
        int16_t d1 = drv_encoder_get_delta(1);
        if(d1 != 0)
        {
            param_master = clamp_0_127(param_master + d1);
            mixer_set_master((param_master / 127.0f) * 2.0f);
        }
    }

    {
        int16_t d0 = drv_encoder_get_delta(0);
        if(d0 != 0)
        {
            param_algo = clamp_0_127(param_algo + d0);
            fx_warps_set_algorithm(param_algo / 127.0f);
        }
    }

    {
        int16_t d2 = drv_encoder_get_delta(2);
        if(d2 != 0)
        {
            param_warps = clamp_0_127(param_warps + d2);
            fx_warps_set_parameter(param_warps / 127.0f);
        }
    }

    {
        int16_t d3 = drv_encoder_get_delta(3);
        if(d3 != 0)
        {
            param_drywet = clamp_0_127(param_drywet + d3);
            fx_warps_set_drywet(param_drywet / 127.0f);
        }
    }
}

void app_controls_render(void)
{
    char buf[32];

    drv_display_clear();

    snprintf(buf, sizeof(buf), "M:%3d A:%3d", (int)param_master, (int)param_algo);
    drv_display_draw_text(0, 0, buf);

    snprintf(buf, sizeof(buf), "P:%3d W:%3d", (int)param_warps, (int)param_drywet);
    drv_display_draw_text(0, 12, buf);

    drv_display_update();
}
