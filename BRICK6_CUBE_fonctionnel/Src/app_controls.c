#include "app_controls.h"
#include "app_controls_eq.h"
#include "drv_encoders.h"
#include "drv_display.h"
#include "cpu_load.h"
#include "audio_float.h"
#include "fx_clouds.h"
#include <stdint.h>
#include <stdio.h>

typedef enum
{
    CLOUDS_PAGE_POSITION_SIZE_PITCH = 0,
    CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET,
    CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE,
    CLOUDS_PAGE_COUNT
} clouds_page_t;

clouds_page_t current_page = CLOUDS_PAGE_POSITION_SIZE_PITCH;

static uint8_t encoder_values[4] = {0U, 64U, 64U, 64U};
static uint8_t cloud_position = 64U;
static uint8_t cloud_size = 64U;
static uint8_t cloud_pitch = 64U;
static uint8_t cloud_density = 64U;
static uint8_t cloud_texture = 64U;
static uint8_t cloud_dry_wet = 127U;
static uint8_t cloud_feedback = 38U;
static uint8_t cloud_stereo_spread = 127U;
static uint8_t cloud_freeze = 0U;

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

    if (!enc0_initialized)
    {
        prev_enc0 = enc0;
        enc0_initialized = 1U;
    }
    else
    {
        int16_t delta = (int16_t)enc0 - (int16_t)prev_enc0;

        if (delta > 64)
        {
            delta -= 128;
        }
        else if (delta < -64)
        {
            delta += 128;
        }

        while (delta > 0)
        {
            current_page = (clouds_page_t)((current_page + 1U) % CLOUDS_PAGE_COUNT);
            delta--;
        }

        while (delta < 0)
        {
            current_page = (clouds_page_t)((current_page + CLOUDS_PAGE_COUNT - 1U) % CLOUDS_PAGE_COUNT);
            delta++;
        }

        prev_enc0 = enc0;
    }

    switch (current_page)
    {
    case CLOUDS_PAGE_POSITION_SIZE_PITCH:
        cloud_position = enc1;
        cloud_size = enc2;
        cloud_pitch = enc3;
        break;

    case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
        cloud_density = enc1;
        cloud_texture = enc2;
        cloud_dry_wet = enc3;
        break;

    case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
        cloud_feedback = enc1;
        cloud_stereo_spread = enc2;
        cloud_freeze = enc3;
        break;

    default:
        break;
    }

    fx_clouds_set_position(ui_0_127_to_unit_float(cloud_position));
    fx_clouds_set_size(ui_0_127_to_unit_float(cloud_size));
    fx_clouds_set_pitch(ui_0_127_to_pitch_semitones(cloud_pitch));
    fx_clouds_set_density(ui_0_127_to_unit_float(cloud_density));
    fx_clouds_set_texture(ui_0_127_to_unit_float(cloud_texture));
    fx_clouds_set_dry_wet(ui_0_127_to_unit_float(cloud_dry_wet));
    fx_clouds_set_feedback(ui_0_127_to_unit_float(cloud_feedback));
    fx_clouds_set_stereo_spread(ui_0_127_to_unit_float(cloud_stereo_spread));
    fx_clouds_set_freeze(cloud_freeze >= 64U);
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

        int16_t newv = (int16_t)encoder_values[i] + d;
        if(newv < 0)   newv = 0;
        if(newv > 127) newv = 127;

        if(newv != encoder_values[i])
        {
            encoder_values[i] = (uint8_t)newv;
            changed = 1U;
        }
    }

    if(changed)
    {
        clouds_control_update(encoder_values[0], encoder_values[1], encoder_values[2], encoder_values[3]);
    }
}

void app_controls_render(void)
{
    char buf[32];
    const uint32_t cpu_pm = cpu_load_get_permille();

    const char* label_a = "POS";
    const char* label_b = "SZ";
    const char* label_c = "PIT";

    uint8_t value_a = cloud_position;
    uint8_t value_b = cloud_size;
    uint8_t value_c = cloud_pitch;

    switch (current_page)
    {
    case CLOUDS_PAGE_POSITION_SIZE_PITCH:
        label_a = "POS";
        label_b = "SZ";
        label_c = "PIT";
        value_a = cloud_position;
        value_b = cloud_size;
        value_c = cloud_pitch;
        break;

    case CLOUDS_PAGE_DENSITY_TEXTURE_DRYWET:
        label_a = "DEN";
        label_b = "TEX";
        label_c = "MIX";
        value_a = cloud_density;
        value_b = cloud_texture;
        value_c = cloud_dry_wet;
        break;

    case CLOUDS_PAGE_FEEDBACK_SPREAD_FREEZE:
        label_a = "FBK";
        label_b = "SPR";
        label_c = "FRZ";
        value_a = cloud_feedback;
        value_b = cloud_stereo_spread;
        value_c = cloud_freeze;
        break;

    default:
        break;
    }

    drv_display_clear();

    snprintf(buf, sizeof(buf), "PG:%1d %s:%3d %s:%3d", (int)current_page, label_a, (int)value_a, label_b, (int)value_b);
    drv_display_draw_text(0, 0, buf);

    snprintf(buf, sizeof(buf), "%s:%3d", label_c, (int)value_c);
    drv_display_draw_text(0, 10, buf);

    snprintf(buf, sizeof(buf), "CPU:%2lu.%1lu%%",
             (unsigned long)(cpu_pm / 10U),
             (unsigned long)(cpu_pm % 10U));
    drv_display_draw_text(0, 20, buf);

    drv_display_update();
}
