#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "Audio/audio_fx_runtime.h"
#include "Param/param_registry.h"

#define LINEAR_DESC(_id, _min, _max, _display, _to_display, _to_canonical) \
    [(_id)] = { .id = (_id), .type = PARAM_TYPE_FLOAT, .min = (_min), .max = (_max), .step = 0.01f, \
        .display_type = (_display), .value_policy = { .canonical_to_display = (_to_display), \
        .display_to_canonical = (_to_canonical), .normal_step_display = 1.0f, .fine_step_display = 0.01f, \
        .automation = PARAM_AUTOMATION_LINEAR_U16 } }

const param_desc_t param_registry[PARAM_COUNT] = {
    LINEAR_DESC(PARAM_MIX_LEVEL, 0.0f, 2.0f, PARAM_DISPLAY_FLOAT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_FILTER_CUTOFF, 0.0f, 127.0f, PARAM_DISPLAY_FLOAT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_BUS_COMP_ATTACK_INDEX, 0.0001f, 0.1f, PARAM_DISPLAY_TIME_MS,
                param_value_seconds_to_milliseconds, param_value_milliseconds_to_seconds),
    LINEAR_DESC(PARAM_MIX_SEND1, 0.0f, 1.0f, PARAM_DISPLAY_PERCENT,
                param_value_percent127_to_display, param_value_percent127_to_canonical),
    LINEAR_DESC(PARAM_MIX_SEND2, 0.0f, 1.0f, PARAM_DISPLAY_PERCENT,
                param_value_percent127_to_display, param_value_percent127_to_canonical),
    LINEAR_DESC(PARAM_SAMPLER_TUNE, -24.0f, 24.0f, PARAM_DISPLAY_FLOAT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_WAVE_TUNE, -60.0f, 60.0f, PARAM_DISPLAY_INT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_WAVE_DETUNE, -24.0f, 24.0f, PARAM_DISPLAY_INT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_CFG_TEMPO, 40.0f, 300.0f, PARAM_DISPLAY_FLOAT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_LFO1_RATE, -20.0f, 16.0f, PARAM_DISPLAY_FLOAT,
                param_value_identity, param_value_identity),
    LINEAR_DESC(PARAM_AUDIO_FX_P1, 0.0f, 1.0f, PARAM_DISPLAY_PERCENT,
                param_value_percent127_to_display, param_value_percent127_to_canonical),
    [PARAM_AUDIO_FX_MODEL] = { .id = PARAM_AUDIO_FX_MODEL, .type = PARAM_TYPE_ENUM,
        .min = 0.0f, .max = 13.0f, .step = 1.0f,
        .value_policy = { .canonical_to_display = param_value_identity,
            .display_to_canonical = param_value_identity, .normal_step_display = 1.0f,
            .fine_step_display = 1.0f, .automation = PARAM_AUTOMATION_DISCRETE_STEP } }
};

static uint8_t g_audio_fx_model;

float param_get(param_id_t id)
{
    (void)id;
    return 0.0f;
}

uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    (void)track;
    if ((id != PARAM_AUDIO_FX_MODEL) || (out_value == 0)) return 0U;
    *out_value = (float)g_audio_fx_model;
    return 1U;
}

static void expect_near(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static void verify_standard_steps(param_id_t id, float start, float min_value, float max_value)
{
    const float display = param_value_policy_canonical_to_display(id, 0U, start);
    const float normal = param_value_policy_apply_delta(id, 0U, start, 1, 0U, min_value, max_value);
    const float fine = param_value_policy_apply_delta(id, 0U, start, 1, 1U, min_value, max_value);
    expect_near(param_value_policy_canonical_to_display(id, 0U, normal), display + 1.0f, 0.0002f);
    expect_near(param_value_policy_canonical_to_display(id, 0U, fine), display + 0.01f, 0.0002f);
    const uint16_t encoded = param_value_policy_encode_u16(&param_registry[id], fine);
    const float decoded = param_value_policy_decode_u16(&param_registry[id], encoded);
    expect_near(param_value_policy_canonical_to_display(id, 0U, decoded), display + 0.01f, 0.005f);
}

int main(void)
{
    verify_standard_steps(PARAM_MIX_LEVEL, 0.5f, 0.0f, 2.0f);
    verify_standard_steps(PARAM_FILTER_CUTOFF, 60.0f, 0.0f, 127.0f);
    verify_standard_steps(PARAM_BUS_COMP_ATTACK_INDEX, 0.050f, 0.0001f, 0.1f);
    verify_standard_steps(PARAM_MIX_SEND1, 0.4f, 0.0f, 1.0f);
    verify_standard_steps(PARAM_MIX_SEND2, 0.4f, 0.0f, 1.0f);
    verify_standard_steps(PARAM_SAMPLER_TUNE, 0.0f, -24.0f, 24.0f);
    verify_standard_steps(PARAM_WAVE_TUNE, 0.0f, -60.0f, 60.0f);
    verify_standard_steps(PARAM_WAVE_DETUNE, 0.0f, -24.0f, 24.0f);
    verify_standard_steps(PARAM_CFG_TEMPO, 120.0f, 40.0f, 300.0f);
    verify_standard_steps(PARAM_LFO1_RATE, -10.0f, -20.0f, 16.0f);

    g_audio_fx_model = AUDIO_FX_MODEL_DRIFT;
    verify_standard_steps(PARAM_AUDIO_FX_P1, 0.5f, 0.0f, 1.0f);
    expect_near(param_value_policy_canonical_to_display(PARAM_AUDIO_FX_P1, 0U, 0.0f), 0.1f, 0.0001f);
    expect_near(param_value_policy_canonical_to_display(PARAM_AUDIO_FX_P1, 0U, 1.0f), 8.0f, 0.0001f);
    return 0;
}
