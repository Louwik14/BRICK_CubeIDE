#include "Audio/microdexed_synth.h"

#include <algorithm>

#include "Storage/memory_layout.h"

#ifndef MICRODEXED_MINIMAL
#define MICRODEXED_MINIMAL
#endif
#include "../../Micro_Dexed/dexed.h"
#include "../../Micro_Dexed/microdexed_marki_minimal.h"

namespace
{
AUDIO_WARM MicroDexedMarkIMinimal g_microdexed;
CTRL_STATE uint8_t g_microdexed_initialized = 0U;
CTRL_STATE uint8_t g_microdexed_enabled = 0U;
AUDIO_HOT ALIGN32 int16_t g_render_buffer[DEXED_RENDER_MAX_FRAMES];

constexpr float kInt16ToFloat = 1.0f / 32768.0f;
constexpr uint8_t kDexedVoiceOffset = DEXED_VOICE_OFFSET;
constexpr uint8_t kDefaultPitchBendStep = 0U;
constexpr uint8_t kDefaultPortamentoMode = 0U;
constexpr uint8_t kDefaultPortamentoGliss = 0U;
constexpr uint8_t kDefaultOperatorMask = 0x3FU;

struct microdexed_runtime_state_t
{
    uint8_t pitch_bend_range;
    uint8_t pitch_bend_step;
    uint8_t portamento_mode;
    uint8_t portamento_glissando;
    uint8_t portamento_time;
    uint8_t mono_mode;
    uint8_t operator_mask;
};

CTRL_STATE microdexed_runtime_state_t g_microdexed_state = {
    .pitch_bend_range = 2U,
    .pitch_bend_step = kDefaultPitchBendStep,
    .portamento_mode = kDefaultPortamentoMode,
    .portamento_glissando = kDefaultPortamentoGliss,
    .portamento_time = 0U,
    .mono_mode = 0U,
    .operator_mask = kDefaultOperatorMask,
};

static uint8_t microdexed_clamp_u8(float value, uint8_t min_value, uint8_t max_value)
{
    if (value <= (float)min_value)
    {
        return min_value;
    }

    if (value >= (float)max_value)
    {
        return max_value;
    }

    return (uint8_t)(value + 0.5f);
}

static uint8_t microdexed_operator_output_offset(uint8_t operator_index)
{
    return (uint8_t)((operator_index * 21U) + DEXED_OP_OUTPUT_LEV);
}

static void microdexed_apply_runtime_state(void)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    g_microdexed.setPitchBendRange(g_microdexed_state.pitch_bend_range,
                                   g_microdexed_state.pitch_bend_step);
    g_microdexed.setPortamento(g_microdexed_state.portamento_mode,
                               g_microdexed_state.portamento_glissando,
                               g_microdexed_state.portamento_time);
    g_microdexed.setMonoMode(g_microdexed_state.mono_mode != 0U);
    g_microdexed.setOperatorMask(g_microdexed_state.operator_mask);
}

static void microdexed_apply_patch_default_state(void)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    g_microdexed_state.pitch_bend_range = 2U;
    g_microdexed_state.pitch_bend_step = kDefaultPitchBendStep;
    g_microdexed_state.portamento_mode = kDefaultPortamentoMode;
    g_microdexed_state.portamento_glissando = kDefaultPortamentoGliss;
    g_microdexed_state.portamento_time = 0U;
    g_microdexed_state.mono_mode = 0U;
    g_microdexed_state.operator_mask = kDefaultOperatorMask;

    microdexed_apply_runtime_state();
}
}

extern "C" {

void microdexed_synth_init(float sample_rate, uint32_t block_size)
{
    (void)block_size;

    const int rate = (sample_rate > 0.0f) ? static_cast<int>(sample_rate + 0.5f) : SAMPLE_RATE;

    g_microdexed_initialized = g_microdexed.init(rate) ? 1U : 0U;
    g_microdexed_enabled = 0U;

    if (g_microdexed_initialized != 0U)
    {
        g_microdexed.loadDefaultPatch();
        g_microdexed.allNotesOff();
        microdexed_apply_patch_default_state();
    }
}

void microdexed_synth_set_enabled(uint8_t enabled)
{
    g_microdexed_enabled = (enabled != 0U) ? 1U : 0U;

    if ((g_microdexed_enabled == 0U) && (g_microdexed_initialized != 0U))
    {
        g_microdexed.allNotesOff();
    }
}

uint8_t microdexed_synth_is_enabled(void)
{
    return (uint8_t)((g_microdexed_initialized != 0U) && (g_microdexed_enabled != 0U));
}

void microdexed_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    if (microdexed_synth_is_enabled() == 0U)
    {
        return;
    }

    if (velocity == 0U)
    {
        velocity = 1U;
    }

    g_microdexed.noteOn(midi_note, velocity);
}

void microdexed_synth_note_off(uint8_t midi_note)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    g_microdexed.noteOff(midi_note);
}

void microdexed_synth_all_notes_off(void)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    g_microdexed.allNotesOff();
}

void microdexed_synth_process_block(float *mono_out, uint32_t frames)
{
    if (mono_out == nullptr || frames == 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        mono_out[i] = 0.0f;
    }

    if (microdexed_synth_is_enabled() == 0U)
    {
        return;
    }

    uint32_t offset = 0U;
    while (offset < frames)
    {
        const uint32_t chunk = std::min<uint32_t>(frames - offset, DEXED_RENDER_MAX_FRAMES);
        g_microdexed.render(g_render_buffer, static_cast<int>(chunk));

        for (uint32_t i = 0U; i < chunk; ++i)
        {
            mono_out[offset + i] = (float)g_render_buffer[i] * kInt16ToFloat;
        }

        offset += chunk;
    }
}

void microdexed_synth_set_param(microdexed_param_t param, float value)
{
    if (g_microdexed_initialized == 0U)
    {
        return;
    }

    switch (param)
    {
        case MICRODEXED_PARAM_ALGORITHM:
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_ALGORITHM),
                                                 microdexed_clamp_u8(value, 0U, 31U));
            break;

        case MICRODEXED_PARAM_FEEDBACK:
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_FEEDBACK),
                                                 microdexed_clamp_u8(value, 0U, 7U));
            break;

        case MICRODEXED_PARAM_TRANSPOSE:
        {
            const int32_t transpose = (int32_t)value;
            const int32_t dx7_value = transpose + 24;
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_TRANSPOSE),
                                                 microdexed_clamp_u8((float)dx7_value, 0U, 48U));
            break;
        }

        case MICRODEXED_PARAM_LFO_SPEED:
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_SPEED),
                                                 microdexed_clamp_u8(value, 0U, 99U));
            break;

        case MICRODEXED_PARAM_LFO_DELAY:
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_DELAY),
                                                 microdexed_clamp_u8(value, 0U, 99U));
            break;

        case MICRODEXED_PARAM_LFO_PITCH_MOD_DEPTH:
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_PITCH_MOD_DEP),
                                                 microdexed_clamp_u8(value, 0U, 99U));
            break;

        case MICRODEXED_PARAM_LFO_AMP_MOD_DEPTH:
            (void)g_microdexed.setVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_AMP_MOD_DEP),
                                                 microdexed_clamp_u8(value, 0U, 99U));
            break;

        case MICRODEXED_PARAM_PITCH_BEND_RANGE:
            g_microdexed_state.pitch_bend_range = microdexed_clamp_u8(value, 0U, 12U);
            microdexed_apply_runtime_state();
            break;

        case MICRODEXED_PARAM_PORTAMENTO_TIME:
            g_microdexed_state.portamento_time = microdexed_clamp_u8(value, 0U, 127U);
            g_microdexed_state.portamento_mode = (g_microdexed_state.portamento_time > 0U) ? 127U : 0U;
            microdexed_apply_runtime_state();
            break;

        case MICRODEXED_PARAM_MONO_MODE:
            g_microdexed_state.mono_mode = (value >= 0.5f) ? 1U : 0U;
            microdexed_apply_runtime_state();
            break;

        case MICRODEXED_PARAM_OPERATOR_MASK:
            g_microdexed_state.operator_mask = microdexed_clamp_u8(value, 0U, 63U);
            microdexed_apply_runtime_state();
            break;

        case MICRODEXED_PARAM_OPERATOR_1_LEVEL:
        case MICRODEXED_PARAM_OPERATOR_2_LEVEL:
        case MICRODEXED_PARAM_OPERATOR_3_LEVEL:
        case MICRODEXED_PARAM_OPERATOR_4_LEVEL:
        {
            const uint8_t operator_index = (uint8_t)(param - MICRODEXED_PARAM_OPERATOR_1_LEVEL);
            (void)g_microdexed.setVoiceParameter(microdexed_operator_output_offset(operator_index),
                                                 microdexed_clamp_u8(value, 0U, 99U));
            break;
        }

        default:
            break;
    }
}

float microdexed_synth_get_param(microdexed_param_t param)
{
    if (g_microdexed_initialized == 0U)
    {
        return 0.0f;
    }

    switch (param)
    {
        case MICRODEXED_PARAM_ALGORITHM:
            return (float)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_ALGORITHM));

        case MICRODEXED_PARAM_FEEDBACK:
            return (float)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_FEEDBACK));

        case MICRODEXED_PARAM_TRANSPOSE:
            return (float)((int32_t)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_TRANSPOSE)) - 24);

        case MICRODEXED_PARAM_LFO_SPEED:
            return (float)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_SPEED));

        case MICRODEXED_PARAM_LFO_DELAY:
            return (float)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_DELAY));

        case MICRODEXED_PARAM_LFO_PITCH_MOD_DEPTH:
            return (float)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_PITCH_MOD_DEP));

        case MICRODEXED_PARAM_LFO_AMP_MOD_DEPTH:
            return (float)g_microdexed.getVoiceParameter((uint8_t)(kDexedVoiceOffset + DEXED_LFO_AMP_MOD_DEP));

        case MICRODEXED_PARAM_PITCH_BEND_RANGE:
            return (float)g_microdexed_state.pitch_bend_range;

        case MICRODEXED_PARAM_PORTAMENTO_TIME:
            return (float)g_microdexed_state.portamento_time;

        case MICRODEXED_PARAM_MONO_MODE:
            return (float)g_microdexed_state.mono_mode;

        case MICRODEXED_PARAM_OPERATOR_MASK:
            return (float)g_microdexed_state.operator_mask;

        case MICRODEXED_PARAM_OPERATOR_1_LEVEL:
        case MICRODEXED_PARAM_OPERATOR_2_LEVEL:
        case MICRODEXED_PARAM_OPERATOR_3_LEVEL:
        case MICRODEXED_PARAM_OPERATOR_4_LEVEL:
        {
            const uint8_t operator_index = (uint8_t)(param - MICRODEXED_PARAM_OPERATOR_1_LEVEL);
            return (float)g_microdexed.getVoiceParameter(microdexed_operator_output_offset(operator_index));
        }

        default:
            return 0.0f;
    }
}

} /* extern "C" */
