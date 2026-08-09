#include "Param/param_prism_labels.h"

#include <stddef.h>

#include "Core/track_tone_sound_state.h"
#include "Core/brick6_braids_runtime.h"
#include "Param/param_registry.h"

static const param_prism_param_label_t g_prism_param_labels[] = {
    { "NWidth", "NDepth", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_BIPOLAR_PERCENT },
    { "Shape", "ToneDrv", PARAM_PRISM_LABEL_VALUE_MORPH, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Shape", "-", PARAM_PRISM_LABEL_VALUE_MORPH, PARAM_PRISM_LABEL_VALUE_NONE },
    { "Fold", "SinTri", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Density", "Detune", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "PW", "Sub Mix", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_MORPH },
    { "Saw Shape", "Sub Mix", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_MORPH },
    { "Slave", "Balance", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Slave", "Balance", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Osc2", "Osc3", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "Osc2", "Osc3", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "Osc2", "Osc3", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "Osc2", "Osc3", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "Mod1", "Mod2", PARAM_PRISM_LABEL_VALUE_INTERVAL, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "Detune", "Tone", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Rate", "Mask", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_STEPPED },
    { "Formant 1", "Formant 2", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Vowel", "FShift", PARAM_PRISM_LABEL_VALUE_MORPH, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Formant Y", "Formant X", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "NWidth", "NDepth", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_BIPOLAR_PERCENT },
    { "Index", "Ratio", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_STEPPED },
    { "Index", "Ratio", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_STEPPED },
    { "Chaos", "Ratio", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_STEPPED },
    { "WTbl", "Bank", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_ENUM },
    { "X", "Y", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "WTbl", "Interp", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_STEPPED },
    { "WTbl", "Chord", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_ENUM },
    { "Resonance", "FltMix", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_MORPH },
    { "Q", "Spread", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_INTERVAL },
    { "Rate", "Steps", PARAM_PRISM_LABEL_VALUE_RATE, PARAM_PRISM_LABEL_VALUE_STEPPED },
    { "Grain", "PVar", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Density", "Spread", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Baud", "Data", PARAM_PRISM_LABEL_VALUE_RATE, PARAM_PRISM_LABEL_VALUE_PERCENT },
    { "Speed", "Noise", PARAM_PRISM_LABEL_VALUE_PERCENT, PARAM_PRISM_LABEL_VALUE_PERCENT },
};

_Static_assert((sizeof(g_prism_param_labels) / sizeof(g_prism_param_labels[0])) == BRICK6_PRISM_MODEL_COUNT,
               "Prism parameter labels and active model count must stay aligned");

uint8_t param_prism_label_count(void)
{
    return (uint8_t)(sizeof(g_prism_param_labels) / sizeof(g_prism_param_labels[0]));
}

uint8_t param_prism_edit_index_from_value(float value, uint8_t *out_index)
{
    if (out_index == NULL)
    {
        return 0U;
    }

    uint8_t index = 0U;
    if (value > 0.0f)
    {
        index = (uint8_t)(value + 0.5f);
    }

    const uint8_t count = param_prism_label_count();
    if (index >= count)
    {
        index = (uint8_t)(count - 1U);
    }

    *out_index = index;
    return 1U;
}

uint8_t param_prism_edit_index_for_track(uint8_t track, uint8_t *out_index)
{
    if (out_index == NULL)
    {
        return 0U;
    }

    const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
    if (tone == NULL)
    {
        return 0U;
    }

    return param_prism_edit_index_from_value(tone->prism.edit[0], out_index);
}

const param_prism_param_label_t *param_prism_labels_for_edit_index(uint8_t edit_index)
{
    const uint8_t count = param_prism_label_count();
    if (edit_index >= count)
    {
        edit_index = (uint8_t)(count - 1U);
    }

    return &g_prism_param_labels[edit_index];
}

uint8_t param_prism_label_for_track_param(uint8_t track, param_id_t id, const char **out_label)
{
    if (out_label == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_PRISM_EDIT:
        case PARAM_PRISM_OSC2_EDIT:
            *out_label = "MODEL";
            return 1U;
        case PARAM_PRISM_FINE:
        case PARAM_PRISM_OSC2_FINE:
            *out_label = "FINE";
            return 1U;
        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_OSC2_COARSE:
            *out_label = "TUNE";
            return 1U;
        case PARAM_PRISM_FM:
        case PARAM_PRISM_OSC2_FM:
            *out_label = "FM AMT";
            return 1U;
        case PARAM_PRISM_MODULATION:
        case PARAM_PRISM_OSC2_MODULATION:
            *out_label = "A MOD";
            return 1U;
        case PARAM_PRISM_LEVEL:
        case PARAM_PRISM_OSC2_LEVEL:
            *out_label = "LVL";
            return 1U;
        case PARAM_PRISM_TIMBRE:
        case PARAM_PRISM_COLOR:
        case PARAM_PRISM_OSC2_TIMBRE:
        case PARAM_PRISM_OSC2_COLOR:
            break;
        default:
            return 0U;
    }

    uint8_t edit_index = 0U;
    const param_id_t edit_param = ((id == PARAM_PRISM_OSC2_TIMBRE) || (id == PARAM_PRISM_OSC2_COLOR))
        ? PARAM_PRISM_OSC2_EDIT
        : PARAM_PRISM_EDIT;
    float edit_value = 0.0f;
    if (param_registry_get_track_value(edit_param, track, &edit_value) == 0U)
    {
        return 0U;
    }
    (void)param_prism_edit_index_from_value(edit_value, &edit_index);

    const param_prism_param_label_t *const labels = param_prism_labels_for_edit_index(edit_index);
    *out_label = ((id == PARAM_PRISM_TIMBRE) || (id == PARAM_PRISM_OSC2_TIMBRE)) ? labels->label_a : labels->label_b;
    return 1U;
}
