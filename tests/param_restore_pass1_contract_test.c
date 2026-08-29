#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "Core/track_runtime.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"

const param_desc_t param_registry[PARAM_COUNT] = {
    [PARAM_MIX_LEVEL] = {
        .id = PARAM_MIX_LEVEL,
        .min = -1.0f,
        .max = 1.0f
    },
    [PARAM_PRISM_TUNE] = {
        .id = PARAM_PRISM_TUNE,
        .min = -60.0f,
        .max = 60.0f
    },
    [PARAM_WAVE_OSC1_POS] = {
        .id = PARAM_WAVE_OSC1_POS,
        .min = 0.0f,
        .max = 1.0f
    }
};

static track_runtime_ctx_t g_test_track_ctx = {
    .type = TRACK_RUNTIME_TYPE_PRISM
};

const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track)
{
    return (track < SEQ_LANE_CAPACITY) ? &g_test_track_ctx : NULL;
}

uint8_t track_runtime_tone_param_to_slot(track_runtime_type_t type,
                                          param_id_t param,
                                          uint8_t *out_slot)
{
    if (out_slot == NULL) return 0U;
    if (((type == TRACK_RUNTIME_TYPE_PRISM) && (param == PARAM_PRISM_TUNE))
            || ((type == TRACK_RUNTIME_TYPE_WAVE)
                && (param == PARAM_WAVE_OSC1_POS)))
    {
        *out_slot = 0U;
        return 1U;
    }
    return 0U;
}

track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param)
{
    track_runtime_param_rule_t rule = {0};
    if ((param == PARAM_PRISM_TUNE) || (param == PARAM_WAVE_OSC1_POS))
        rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
    return rule;
}

int main(void)
{
    param_registry_prepared_value_t value = {0};
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, 2.0f, &value) == 1U);
    assert(value.id == PARAM_MIX_LEVEL);
    assert(value.value == 1.0f);
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, -2.0f, &value) == 1U);
    assert(value.value == -1.0f);
    assert(param_registry_prepare_value(PARAM_RESERVED_000, 0.0f, &value) == 0U);
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, 0.0f, NULL) == 0U);

    param_registry_control_values_init();
    param_registry_control_value_set(2U, PARAM_MIX_LEVEL, 0.25f);
    float current = 0.0f;
    assert(param_registry_control_value_get(
        2U, PARAM_MIX_LEVEL, &current) == 1U);
    assert(current == 0.25f);

    param_registry_control_value_set(2U, PARAM_PRISM_TUNE, 30.0f);
    assert(param_registry_control_tone_get(2U, 0U, &current) == 1U);
    assert(current == 0.75f);
    g_test_track_ctx.type = TRACK_RUNTIME_TYPE_WAVE;
    assert(param_registry_control_value_get(
        2U, PARAM_WAVE_OSC1_POS, &current) == 1U);
    assert(current == 0.75f);
    g_test_track_ctx.type = TRACK_RUNTIME_TYPE_PRISM;
    assert(param_registry_control_value_get(
        2U, PARAM_PRISM_TUNE, &current) == 1U);
    assert(current == 30.0f);
    return 0;
}
