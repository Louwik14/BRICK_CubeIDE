#include "Param/param_value_policy.h"

#include <math.h>

#include "Audio/audio_fx_runtime.h"
#include "Audio/fx_modfx_global.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_registry.h"

static float clampf(float value, float min_value, float max_value)
{
    return (value < min_value) ? min_value : ((value > max_value) ? max_value : value);
}

float param_value_identity(float value, float min_value, float max_value)
{
    (void)min_value;
    (void)max_value;
    return value;
}

float param_value_percent127_to_display(float value, float min_value, float max_value)
{
    return (max_value > min_value) ? ((value - min_value) * 127.0f / (max_value - min_value)) : 0.0f;
}

float param_value_percent127_to_canonical(float value, float min_value, float max_value)
{
    return min_value + value * (max_value - min_value) / 127.0f;
}

float param_value_seconds_to_milliseconds(float value, float min_value, float max_value)
{
    (void)min_value;
    (void)max_value;
    return value * 1000.0f;
}

float param_value_milliseconds_to_seconds(float value, float min_value, float max_value)
{
    (void)min_value;
    (void)max_value;
    return value * 0.001f;
}

float param_value_prism_tune_to_display(float value, float min_value, float max_value)
{
    (void)min_value;
    (void)max_value;
    return (value - 0.5f) * 48.0f;
}

float param_value_prism_tune_to_canonical(float value, float min_value, float max_value)
{
    (void)min_value;
    (void)max_value;
    return 0.5f + value / 48.0f;
}

#define DEFINE_AFFINE_POLICY_TRANSFORMS(_name, _offset, _span) \
    static float _name##_to_display(float value, float min_value, float max_value) \
    { (void)min_value; (void)max_value; return (_offset) + (_span) * value; } \
    static float _name##_to_canonical(float value, float min_value, float max_value) \
    { (void)min_value; (void)max_value; return (value - (_offset)) / (_span); }

DEFINE_AFFINE_POLICY_TRANSFORMS(drift_delay, 0.1f, 7.9f)
DEFINE_AFFINE_POLICY_TRANSFORMS(vibe_rate, 0.01f, 11.99f)
DEFINE_AFFINE_POLICY_TRANSFORMS(percent100, 0.0f, 100.0f)
DEFINE_AFFINE_POLICY_TRANSFORMS(db24, -12.0f, 24.0f)
DEFINE_AFFINE_POLICY_TRANSFORMS(bipolar2, -1.0f, 2.0f)
DEFINE_AFFINE_POLICY_TRANSFORMS(normalized127, 0.0f, (1.0f / 127.0f))
DEFINE_AFFINE_POLICY_TRANSFORMS(modfx_width_percent, 0.0f, (100.0f / 127.0f))
DEFINE_AFFINE_POLICY_TRANSFORMS(modfx_juno_mode, 0.0f, (3.0f / 127.0f))

static float modfx_rate_to_display(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    return (value <= 61.0f) ? (0.01f * powf(30.0f, value / 61.0f))
                            : (0.3f * powf(40.0f, (value - 61.0f) / 66.0f));
}

static float modfx_rate_to_canonical(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    if (value <= 0.01f) return 0.0f;
    return (value <= 0.3f) ? (61.0f * logf(value / 0.01f) / logf(30.0f))
                           : (61.0f + 66.0f * logf(value / 0.3f) / logf(40.0f));
}

static float modfx_delay_to_display(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    const float unit = (value <= 95.0f) ? (0.75f * value / 95.0f)
                                        : (0.75f + 0.25f * (value - 95.0f) / 32.0f);
    return 0.1f + 7.9f * unit;
}

static float modfx_delay_to_canonical(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    const float unit = (value - 0.1f) / 7.9f;
    return (unit <= 0.75f) ? (unit * 95.0f / 0.75f)
                           : (95.0f + (unit - 0.75f) * 32.0f / 0.25f);
}

static float modfx_depth_to_display(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    return 100.0f * ((value <= 123.0f) ? (0.9f * value / 123.0f)
                                       : (0.9f + 0.03f * (value - 123.0f) / 4.0f));
}

static float modfx_depth_to_canonical(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    const float unit = value / 100.0f;
    return (unit <= 0.9f) ? (unit * 123.0f / 0.9f)
                          : (123.0f + (unit - 0.9f) * 4.0f / 0.03f);
}

static float modfx_feedback_to_display(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    return (value <= 64.0f) ? ((value - 64.0f) * (100.0f / 64.0f))
                            : ((value - 64.0f) * (100.0f / 63.0f));
}

static float modfx_feedback_to_canonical(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    return (value <= 0.0f) ? (64.0f + value * (64.0f / 100.0f))
                           : (64.0f + value * (63.0f / 100.0f));
}

static float drive_level_to_display(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    return (value <= 64.0f) ? (-12.0f + value * (12.0f / 64.0f))
                            : ((value - 64.0f) * (6.0f / 63.0f));
}

static float drive_level_to_canonical(float value, float min_value, float max_value)
{
    (void)min_value; (void)max_value;
    return (value <= 0.0f) ? ((value + 12.0f) * (64.0f / 12.0f))
                           : (64.0f + value * (63.0f / 6.0f));
}

static param_value_policy_t affine_policy(param_value_transform_fn to_display,
                                          param_value_transform_fn to_canonical)
{
    const param_value_policy_t policy = {
        .canonical_to_display = to_display,
        .display_to_canonical = to_canonical,
        .normal_step_display = 1.0f,
        .fine_step_display = 0.01f,
        .automation = PARAM_AUTOMATION_LINEAR_U16
    };
    return policy;
}

static uint8_t audio_fx_model_for_param(param_id_t id, uint8_t track, uint8_t *out_model)
{
    param_id_t model_id;
    if ((id == PARAM_AUDIO_FX_P1) || (id == PARAM_AUDIO_FX_P2)
            || (id == PARAM_AUDIO_FX_P3))
        model_id = PARAM_AUDIO_FX_MODEL;
    else if ((id == PARAM_AUDIO_FX_B_P1) || (id == PARAM_AUDIO_FX_B_P2)
            || (id == PARAM_AUDIO_FX_B_P3))
        model_id = PARAM_AUDIO_FX_B_MODEL;
    else
        return 0U;

    float model = 0.0f;
    if (param_registry_get_track_value(model_id, track, &model) == 0U)
        return 0U;
    *out_model = (uint8_t)(model + 0.5f);
    return 1U;
}

param_value_policy_t param_value_policy_resolve(param_id_t id, uint8_t track)
{
    param_value_policy_t policy = param_registry[id].value_policy;
    if ((id >= PARAM_MODFX_RATE) && (id <= PARAM_MODFX_WIDTH))
    {
        const uint8_t modfx_model = (uint8_t)(param_get(PARAM_MODFX_MODEL) + 0.5f);
        if (modfx_model == FX_MODFX_DAISY_STEREO)
        {
            if ((id == PARAM_MODFX_RATE) || (id == PARAM_MODFX_RATE_B))
                return affine_policy(modfx_rate_to_display, modfx_rate_to_canonical);
            if ((id == PARAM_MODFX_OFFSET) || (id == PARAM_MODFX_DELAY_B))
                return affine_policy(modfx_delay_to_display, modfx_delay_to_canonical);
            if ((id == PARAM_MODFX_DEPTH) || (id == PARAM_MODFX_DEPTH_B))
                return affine_policy(modfx_depth_to_display, modfx_depth_to_canonical);
            if (id == PARAM_MODFX_FEEDBACK)
                return affine_policy(modfx_feedback_to_display, modfx_feedback_to_canonical);
            if (id == PARAM_MODFX_WIDTH)
                return affine_policy(modfx_width_percent_to_display,
                                     modfx_width_percent_to_canonical);
        }
        if ((modfx_model == FX_MODFX_JUNOLOGUE) && (id == PARAM_MODFX_OFFSET))
            return affine_policy(modfx_juno_mode_to_display, modfx_juno_mode_to_canonical);
        return policy;
    }
    uint8_t model = 0U;
    if (audio_fx_model_for_param(id, track, &model) == 0U)
        return policy;

    const uint8_t p1 = (uint8_t)((id == PARAM_AUDIO_FX_P1) || (id == PARAM_AUDIO_FX_B_P1));
    const uint8_t p2 = (uint8_t)((id == PARAM_AUDIO_FX_P2) || (id == PARAM_AUDIO_FX_B_P2));
    const uint8_t p3 = (uint8_t)((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3));
    if (p3 != 0U)
    {
        if (model == AUDIO_FX_MODEL_DRIVE)
            return affine_policy(drive_level_to_display, drive_level_to_canonical);
        if (model == AUDIO_FX_MODEL_POINT)
            return affine_policy(normalized127_to_display, normalized127_to_canonical);
        if ((model == AUDIO_FX_MODEL_FOLD) || (model == AUDIO_FX_MODEL_SUB)
                || (model == AUDIO_FX_MODEL_SUB_LIGHT) || (model == AUDIO_FX_MODEL_VIBE))
            return policy;
        policy.normal_step_display = 1.0f;
        policy.fine_step_display = 1.0f;
        policy.automation = PARAM_AUTOMATION_DISCRETE_STEP;
        return policy;
    }
    if ((model == AUDIO_FX_MODEL_DRIFT) && (p1 != 0U))
        return affine_policy(drift_delay_to_display, drift_delay_to_canonical);
    if ((model == AUDIO_FX_MODEL_VIBE) && (p1 != 0U))
        return affine_policy(vibe_rate_to_display, vibe_rate_to_canonical);
    if ((model == AUDIO_FX_MODEL_VIBE) && (p2 != 0U))
        return affine_policy(percent100_to_display, percent100_to_canonical);
    if ((model == AUDIO_FX_MODEL_POINT) && (p1 != 0U))
        return affine_policy(db24_to_display, db24_to_canonical);
    if ((model == AUDIO_FX_MODEL_POINT) && (p2 != 0U))
        return affine_policy(bipolar2_to_display, bipolar2_to_canonical);
    if ((model == AUDIO_FX_MODEL_DRIVE) && (p2 != 0U))
        return affine_policy(db24_to_display, db24_to_canonical);
    return policy;
}

const char *param_value_policy_display_unit(param_id_t id, uint8_t track)
{
    (void)track;
    if ((id >= PARAM_MODFX_RATE) && (id <= PARAM_MODFX_WIDTH))
    {
        const uint8_t model = (uint8_t)(param_get(PARAM_MODFX_MODEL) + 0.5f);
        if (model == FX_MODFX_DAISY_STEREO)
        {
            if ((id == PARAM_MODFX_RATE) || (id == PARAM_MODFX_RATE_B)) return "Hz";
            if ((id == PARAM_MODFX_OFFSET) || (id == PARAM_MODFX_DELAY_B)) return "ms";
            return "%";
        }
        if ((model == FX_MODFX_JUNOLOGUE) && (id == PARAM_MODFX_OFFSET)) return "mode";
    }
    return param_registry[id].unit;
}

float param_value_policy_canonical_to_display(param_id_t id, uint8_t track, float value)
{
    const param_desc_t *const desc = &param_registry[id];
    const param_value_policy_t policy = param_value_policy_resolve(id, track);
    return policy.canonical_to_display(value, desc->min, desc->max);
}

float param_value_policy_display_to_canonical(param_id_t id, uint8_t track, float value)
{
    const param_desc_t *const desc = &param_registry[id];
    const param_value_policy_t policy = param_value_policy_resolve(id, track);
    return policy.display_to_canonical(value, desc->min, desc->max);
}

static uint8_t is_lfo_rate(param_id_t id)
{
    return (uint8_t)((id == PARAM_LFO1_RATE) || (id == PARAM_LFO2_RATE) || (id == PARAM_LFO3_RATE));
}

float param_value_policy_apply_delta(param_id_t id,
                                     uint8_t track,
                                     float canonical_value,
                                     int16_t delta,
                                     uint8_t fine,
                                     float min_value,
                                     float max_value)
{
    if (delta == 0)
        return canonical_value;

    if (is_lfo_rate(id) != 0U)
    {
        const int8_t direction = (delta > 0) ? 1 : -1;
        if (canonical_value > 0.0001f)
        {
            float next = canonical_value + (float)direction;
            if (next < 0.5f) next = 0.0f;
            return clampf(next, 0.0f, (float)MOD_LFO_SYNC_RATE_COUNT);
        }
        if (canonical_value < -0.0001f)
        {
            const float step = (fine != 0U) ? 0.01f : 1.0f;
            float next = canonical_value + (float)delta * step;
            if (next > -0.0001f) next = 0.0f;
            return clampf(next, min_value, 0.0f);
        }
        return (direction > 0) ? 1.0f : ((fine != 0U) ? -0.01f : -1.0f);
    }

    const param_value_policy_t policy = param_value_policy_resolve(id, track);
    if (policy.automation == PARAM_AUTOMATION_DISCRETE_STEP)
    {
        float step = param_registry[id].step;
        if (step <= 0.0f) step = 1.0f;
        return clampf(canonical_value + (float)delta * step, min_value, max_value);
    }

    const float display = policy.canonical_to_display(canonical_value, min_value, max_value);
    const float step = (fine != 0U) ? policy.fine_step_display : policy.normal_step_display;
    const float canonical = policy.display_to_canonical(display + (float)delta * step, min_value, max_value);
    return clampf(canonical, min_value, max_value);
}

uint16_t param_value_policy_encode_u16(const struct param_desc *raw_desc, float value)
{
    const param_desc_t *const desc = (const param_desc_t *)raw_desc;
    value = clampf(value, desc->min, desc->max);
    if ((desc->value_policy.automation == PARAM_AUTOMATION_LINEAR_U16) && (desc->max > desc->min))
    {
        const float normalized = (value - desc->min) / (desc->max - desc->min);
        return (uint16_t)(normalized * 65535.0f + 0.5f);
    }
    float step = desc->step;
    if (step <= 0.0f) step = 1.0f;
    const float encoded = (value - desc->min) / step;
    return (encoded <= 0.0f) ? 0U : ((encoded >= 65535.0f) ? 65535U : (uint16_t)(encoded + 0.5f));
}

float param_value_policy_decode_u16(const struct param_desc *raw_desc, uint16_t value)
{
    const param_desc_t *const desc = (const param_desc_t *)raw_desc;
    if ((desc->value_policy.automation == PARAM_AUTOMATION_LINEAR_U16) && (desc->max > desc->min))
        return desc->min + ((float)value / 65535.0f) * (desc->max - desc->min);
    float step = desc->step;
    if (step <= 0.0f) step = 1.0f;
    return clampf(desc->min + (float)value * step, desc->min, desc->max);
}
