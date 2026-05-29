#include "Param/param_wave_labels.h"

#include <stddef.h>

#include "Core/track_tone_sound_state.h"

static const param_wave_param_label_t g_wave_param_labels[] = {
    { "NWidth", "NDepth", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_BIPOLAR_PERCENT },
    { "Shape", "ToneDrv", PARAM_WAVE_LABEL_VALUE_MORPH, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Shape", "-", PARAM_WAVE_LABEL_VALUE_MORPH, PARAM_WAVE_LABEL_VALUE_NONE },
    { "Fold", "SinTri", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Density", "Detune", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "PW", "Sub Mix", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_MORPH },
    { "Saw Shape", "Sub Mix", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_MORPH },
    { "Slave", "Balance", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Slave", "Balance", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Osc2", "Osc3", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "Osc2", "Osc3", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "Osc2", "Osc3", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "Osc2", "Osc3", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "Mod1", "Mod2", PARAM_WAVE_LABEL_VALUE_INTERVAL, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "Detune", "Tone", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Rate", "Mask", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_STEPPED },
    { "Formant 1", "Formant 2", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Vowel", "FShift", PARAM_WAVE_LABEL_VALUE_MORPH, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Formant Y", "Formant X", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Peak", "Spread", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Index", "Ratio", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_STEPPED },
    { "Index", "Ratio", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_STEPPED },
    { "Chaos", "Ratio", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_STEPPED },
    { "Decay", "Inharm", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Decay", "ToneNz", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Decay", "Tone", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Cutoff", "NzMix", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Body", "Snappy", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Wave", "Bank", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_ENUM },
    { "X", "Y", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Wave", "Interp", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_STEPPED },
    { "Wave", "Chord", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_ENUM },
    { "Resonance", "FltMix", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_MORPH },
    { "Q", "Spread", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_INTERVAL },
    { "Rate", "Steps", PARAM_WAVE_LABEL_VALUE_RATE, PARAM_WAVE_LABEL_VALUE_STEPPED },
    { "Grain", "PVar", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Density", "Spread", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Baud", "Data", PARAM_WAVE_LABEL_VALUE_RATE, PARAM_WAVE_LABEL_VALUE_PERCENT },
    { "Speed", "Noise", PARAM_WAVE_LABEL_VALUE_PERCENT, PARAM_WAVE_LABEL_VALUE_PERCENT },
};

uint8_t param_wave_label_count(void)
{
    return (uint8_t)(sizeof(g_wave_param_labels) / sizeof(g_wave_param_labels[0]));
}

uint8_t param_wave_edit_index_from_value(float value, uint8_t *out_index)
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

    const uint8_t count = param_wave_label_count();
    if (index >= count)
    {
        index = (uint8_t)(count - 1U);
    }

    *out_index = index;
    return 1U;
}

uint8_t param_wave_edit_index_for_track(uint8_t track, uint8_t *out_index)
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

    return param_wave_edit_index_from_value(tone->wave.edit, out_index);
}

const param_wave_param_label_t *param_wave_labels_for_edit_index(uint8_t edit_index)
{
    const uint8_t count = param_wave_label_count();
    if (edit_index >= count)
    {
        edit_index = (uint8_t)(count - 1U);
    }

    return &g_wave_param_labels[edit_index];
}

uint8_t param_wave_label_for_track_param(uint8_t track, param_id_t id, const char **out_label)
{
    if (out_label == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_WAVE_EDIT:
            *out_label = "MODEL";
            return 1U;
        case PARAM_WAVE_FINE:
            *out_label = "FINE";
            return 1U;
        case PARAM_WAVE_COARSE:
            *out_label = "PITCH";
            return 1U;
        case PARAM_WAVE_FM:
            *out_label = "FM AMT";
            return 1U;
        case PARAM_WAVE_MODULATION:
            *out_label = "A MOD";
            return 1U;
        case PARAM_WAVE_TIMBRE:
        case PARAM_WAVE_COLOR:
            break;
        default:
            return 0U;
    }

    uint8_t edit_index = 0U;
    if (param_wave_edit_index_for_track(track, &edit_index) == 0U)
    {
        return 0U;
    }

    const param_wave_param_label_t *const labels = param_wave_labels_for_edit_index(edit_index);
    *out_label = (id == PARAM_WAVE_TIMBRE) ? labels->label_a : labels->label_b;
    return 1U;
}
