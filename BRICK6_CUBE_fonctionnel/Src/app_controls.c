#include "app_controls.h"
#include "app_controls_eq.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "cpu_load.h"
#include "audio_float.h"
#include "fx_granular.h"
#include "param_store.h"
#include <stdint.h>
#include <stdio.h>

typedef enum
{
    CLOUDS_PAGE_POSITION_SIZE_PITCH = 0,
    CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET,
    CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE,
    CLOUDS_PAGE_COUNT
} clouds_page_t;


static inline int clamp_int(int x, int min, int max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

clouds_page_t current_page = CLOUDS_PAGE_POSITION_SIZE_PITCH;

static uint8_t encoder_values[4] = {0U, 64U, 64U, 64U};
static uint8_t granular_density = 64U;
static uint8_t granular_pitch = 64U;
static uint8_t granular_mix = 127U;
static uint8_t granular_freeze = 0U;
static uint8_t granular_stereo = 64U; // ✅ NEW
static uint8_t granular_spread = 64U;

static float ui_0_127_to_unit_float(uint8_t value)
{
    return (float)value * (1.0f / 127.0f);
}

static float ui_0_127_to_pitch_semitones(uint8_t value)
{
    return ((float)value * (96.0f / 127.0f)) - 48.0f;
}

void clouds_control_update(uint8_t enc0, uint8_t enc1, uint8_t enc2, uint8_t enc3)
{
    static uint8_t prev_enc0 = 0U;
    static uint8_t enc0_initialized = 0U;

    if(!enc0_initialized)
    {
        prev_enc0 = enc0;
        enc0_initialized = 1U;
    }
    else
    {
        int16_t delta = (int16_t)enc0 - (int16_t)prev_enc0;

        if(delta > 64)
        {
            delta -= 128;
        }
        else if(delta < -64)
        {
            delta += 128;
        }

        while(delta > 0)
        {
            current_page = (clouds_page_t)((current_page + 1U) % CLOUDS_PAGE_COUNT);
            delta--;
        }

        while(delta < 0)
        {
            current_page = (clouds_page_t)((current_page + CLOUDS_PAGE_COUNT - 1U) % CLOUDS_PAGE_COUNT);
            delta++;
        }

        prev_enc0 = enc0;
    }

    switch(current_page)
    {
    case CLOUDS_PAGE_POSITION_SIZE_PITCH:
        granular_density = enc1;
        granular_pitch = enc2;
        granular_mix = enc3;
        break;

    case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
        granular_freeze = enc1;
        granular_spread = enc2; // ✅ SPREAD ajouté
        granular_stereo = enc3; // ✅ ENC3
        break;

    case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
    default:
        break;
    }

    fx_granular_set_density(ui_0_127_to_unit_float(granular_density));
    fx_granular_set_pitch(ui_0_127_to_pitch_semitones(granular_pitch));
    fx_granular_set_mix(ui_0_127_to_unit_float(granular_mix));
    fx_granular_set_freeze(granular_freeze >= 64U);
    fx_granular_set_spread(ui_0_127_to_unit_float(granular_spread));
    fx_granular_set_stereo_offset(ui_0_127_to_unit_float(granular_stereo)); // ✅ AJOUT
    param_store_set_staging(PARAM_GRAN_DENSITY, ui_0_127_to_unit_float(granular_density));
    param_store_set_staging(PARAM_GRAN_PITCH, ui_0_127_to_pitch_semitones(granular_pitch));
    param_store_set_staging(PARAM_GRAN_MIX, ui_0_127_to_unit_float(granular_mix));
    param_store_set_staging(PARAM_GRAN_FREEZE, (granular_freeze >= 64U) ? 1.0f : 0.0f);
    param_store_set_staging(PARAM_GRAN_SPREAD, ui_0_127_to_unit_float(granular_spread));
    param_store_set_staging(PARAM_GRAN_STEREO, ui_0_127_to_unit_float(granular_stereo));

    (void)param_store_commit_if_block_advanced();
}


void app_controls_init(void)
{
    drv_encoders_init();
    app_controls_eq_init();

    clouds_control_update(encoder_values[0], encoder_values[1], encoder_values[2], encoder_values[3]);
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

        changed = 1U;

        switch(i)
        {
        case 0: // navigation pages
        {
            while(d > 0)
            {
                current_page = (clouds_page_t)((current_page + 1U) % CLOUDS_PAGE_COUNT);
                d--;
            }
            while(d < 0)
            {
                current_page = (clouds_page_t)((current_page + CLOUDS_PAGE_COUNT - 1U) % CLOUDS_PAGE_COUNT);
                d++;
            }
        }
        break;

        case 1:
            switch(current_page)
            {
            case CLOUDS_PAGE_POSITION_SIZE_PITCH:
                granular_density = (uint8_t)clamp_int((int)granular_density + d, 0, 127);
                break;
            case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
                granular_freeze = (uint8_t)clamp_int((int)granular_freeze + d, 0, 127);
                break;
            case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
                /* unused page */
                break;
            default:
                break;
            }
            break;

        case 2:
            switch(current_page)
            {
            case CLOUDS_PAGE_POSITION_SIZE_PITCH:
                granular_pitch = (uint8_t)clamp_int((int)granular_pitch + d, 0, 127);
                break;
            case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
                granular_spread = (uint8_t)clamp_int((int)granular_spread + d, 0, 127);
                break;
            case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
                /* unused page */
                break;
            default:
                break;
            }
            break;

        case 3:
            switch(current_page)
            {
            case CLOUDS_PAGE_POSITION_SIZE_PITCH:
                granular_mix = (uint8_t)clamp_int((int)granular_mix + d, 0, 127);
                break;
            case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
                granular_stereo = (uint8_t)clamp_int((int)granular_stereo + d, 0, 127); // ✅
                break;
            case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
                /* unused page */
                break;
            default:
                break;
            }
            break;

        default:
            break;
        }
    }

    if(changed)
    {
        fx_granular_set_density(ui_0_127_to_unit_float(granular_density));
        fx_granular_set_pitch(ui_0_127_to_pitch_semitones(granular_pitch));
        fx_granular_set_mix(ui_0_127_to_unit_float(granular_mix));
        fx_granular_set_freeze(granular_freeze >= 64U);
        fx_granular_set_spread(ui_0_127_to_unit_float(granular_spread));
        fx_granular_set_stereo_offset(ui_0_127_to_unit_float(granular_stereo)); // ✅ AJOUT
        param_store_set_staging(PARAM_GRAN_DENSITY, ui_0_127_to_unit_float(granular_density));
        param_store_set_staging(PARAM_GRAN_PITCH, ui_0_127_to_pitch_semitones(granular_pitch));
        param_store_set_staging(PARAM_GRAN_MIX, ui_0_127_to_unit_float(granular_mix));
        param_store_set_staging(PARAM_GRAN_FREEZE, (granular_freeze >= 64U) ? 1.0f : 0.0f);
        param_store_set_staging(PARAM_GRAN_SPREAD, ui_0_127_to_unit_float(granular_spread));
        param_store_set_staging(PARAM_GRAN_STEREO, ui_0_127_to_unit_float(granular_stereo));

        (void)param_store_commit_if_block_advanced();
    }
}

void app_controls_render(void)
{
    char buf[32];
    const uint32_t cpu_pm = cpu_load_get_permille();

    drv_display_clear();

    switch (current_page)
    {
    case CLOUDS_PAGE_POSITION_SIZE_PITCH:
        snprintf(buf, sizeof(buf), "PG:%1d DEN:%3d PIT:%3d",
                 (int)current_page,
                 (int)granular_density,
                 (int)granular_pitch);
        drv_display_draw_text(0, 0, buf);

        snprintf(buf, sizeof(buf), "MIX:%3d",
                 (int)granular_mix);
        drv_display_draw_text(0, 10, buf);
        break;

    case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
        // Ligne 1 : page + freeze
        snprintf(buf, sizeof(buf), "PG:%1d FRZ:%1d",
                 (int)current_page,
                 (granular_freeze >= 64U) ? 1 : 0);
        drv_display_draw_text(0, 0, buf);

        // Ligne 2 : spread + stereo
        snprintf(buf, sizeof(buf), "SPR:%3d STR:%3d",
                 (int)granular_spread,
                 (int)granular_stereo);
        drv_display_draw_text(0, 10, buf);

        break;
    case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
        snprintf(buf, sizeof(buf), "PG:%1d ---",
                 (int)current_page);
        drv_display_draw_text(0, 0, buf);

        break;

    default:
        break;
    }

    snprintf(buf, sizeof(buf), "CPU:%2lu.%1lu%%",
             (unsigned long)(cpu_pm / 10U),
             (unsigned long)(cpu_pm % 10U));
    drv_display_draw_text(0, 20, buf);

    drv_display_update();
}
