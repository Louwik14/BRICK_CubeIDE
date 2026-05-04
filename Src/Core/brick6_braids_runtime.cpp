/**
 * @file brick6_braids_runtime.cpp
 * @brief Minimal track-aware Braids runtime wrapper.
 */

#include "Core/brick6_braids_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Storage/memory_layout.h"

#include "braids/macro_oscillator.h"
#include "braids/macro_oscillator_shape.h"

namespace {

constexpr uint32_t kBraidsRenderBlockSize = 24U;
constexpr float kBraidsPitchCoarseRange = 48.0f;
constexpr float kBraidsPitchFineRange = 2.0f;
constexpr float kBraidsPitchFmRange = 24.0f;
constexpr float kBraidsReleaseCoeff = 0.995f;
constexpr float kBraidsEditMax = 38.0f;

static const braids::MacroOscillatorShape kBraidsShapeMap[] = {
    braids::MACRO_OSC_SHAPE_CSAW,
    braids::MACRO_OSC_SHAPE_MORPH,
    braids::MACRO_OSC_SHAPE_SAW_SQUARE,
    braids::MACRO_OSC_SHAPE_SINE_TRIANGLE,
    braids::MACRO_OSC_SHAPE_BUZZ,
    braids::MACRO_OSC_SHAPE_SQUARE_SUB,
    braids::MACRO_OSC_SHAPE_SAW_SUB,
    braids::MACRO_OSC_SHAPE_SQUARE_SYNC,
    braids::MACRO_OSC_SHAPE_SAW_SYNC,
    braids::MACRO_OSC_SHAPE_TRIPLE_SAW,
    braids::MACRO_OSC_SHAPE_TRIPLE_SQUARE,
    braids::MACRO_OSC_SHAPE_TRIPLE_TRIANGLE,
    braids::MACRO_OSC_SHAPE_TRIPLE_SINE,
    braids::MACRO_OSC_SHAPE_TRIPLE_RING_MOD,
    braids::MACRO_OSC_SHAPE_SAW_SWARM,
    braids::MACRO_OSC_SHAPE_TOY,
    braids::MACRO_OSC_SHAPE_VOSIM,
    braids::MACRO_OSC_SHAPE_VOWEL,
    braids::MACRO_OSC_SHAPE_VOWEL_FOF,
    braids::MACRO_OSC_SHAPE_HARMONICS,
    braids::MACRO_OSC_SHAPE_FM,
    braids::MACRO_OSC_SHAPE_FEEDBACK_FM,
    braids::MACRO_OSC_SHAPE_CHAOTIC_FEEDBACK_FM,
    braids::MACRO_OSC_SHAPE_STRUCK_BELL,
    braids::MACRO_OSC_SHAPE_STRUCK_DRUM,
    braids::MACRO_OSC_SHAPE_KICK,
    braids::MACRO_OSC_SHAPE_CYMBAL,
    braids::MACRO_OSC_SHAPE_SNARE,
    braids::MACRO_OSC_SHAPE_WAVETABLES,
    braids::MACRO_OSC_SHAPE_WAVE_MAP,
    braids::MACRO_OSC_SHAPE_WAVE_LINE,
    braids::MACRO_OSC_SHAPE_WAVE_PARAPHONIC,
    braids::MACRO_OSC_SHAPE_FILTERED_NOISE,
    braids::MACRO_OSC_SHAPE_TWIN_PEAKS_NOISE,
    braids::MACRO_OSC_SHAPE_CLOCKED_NOISE,
    braids::MACRO_OSC_SHAPE_GRANULAR_CLOUD,
    braids::MACRO_OSC_SHAPE_PARTICLE_NOISE,
    braids::MACRO_OSC_SHAPE_DIGITAL_MODULATION,
    braids::MACRO_OSC_SHAPE_QUESTION_MARK,
};

typedef struct
{
    brick6_braids_runtime_voice_t voice;
    uint8_t has_note;
    float level;
    braids::MacroOscillator oscillator;
} brick6_braids_runtime_instance_t;

AUDIO_HOT static brick6_braids_runtime_instance_t g_braids_runtime[BRICK6_BRAIDS_MAX_INSTANCES];

static float brick6_braids_runtime_clamp(float value, float lo, float hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static int16_t brick6_braids_runtime_float_to_u15(float value)
{
    const float clamped = brick6_braids_runtime_clamp(value, 0.0f, 1.0f);
    return (int16_t)(clamped * 32767.0f + 0.5f);
}

static int16_t brick6_braids_runtime_pitch_to_q7(const brick6_braids_runtime_voice_t *voice)
{
    const float coarse = (brick6_braids_runtime_clamp(voice->coarse, 0.0f, 1.0f) - 0.5f) * kBraidsPitchCoarseRange;
    const float fine = (brick6_braids_runtime_clamp(voice->fine, 0.0f, 1.0f) - 0.5f) * kBraidsPitchFineRange;
    const float fm_mod = brick6_braids_runtime_clamp(voice->fm, 0.0f, 1.0f)
        * brick6_braids_runtime_clamp(voice->modulation, 0.0f, 1.0f)
        * kBraidsPitchFmRange;
    const float note = brick6_braids_runtime_clamp(voice->note + coarse + fine + fm_mod, 0.0f, 127.0f);
    return (int16_t)(note * 128.0f + 0.5f);
}

static braids::MacroOscillatorShape brick6_braids_runtime_shape_from_edit(float edit)
{
    const int index = (int)(brick6_braids_runtime_clamp(edit, 0.0f, kBraidsEditMax) + 0.5f);
    return kBraidsShapeMap[index];
}

static brick6_braids_runtime_instance_t *brick6_braids_runtime_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_BRAIDS_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_braids_runtime[instance_id];
}

static const brick6_braids_runtime_instance_t *brick6_braids_runtime_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_BRAIDS_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_braids_runtime[instance_id];
}

static void brick6_braids_runtime_init_instance(brick6_braids_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    instance->voice.edit = 0.0f;
    instance->voice.fine = 0.5f;
    instance->voice.coarse = 0.5f;
    instance->voice.fm = 0.0f;
    instance->voice.timbre = 0.5f;
    instance->voice.modulation = 0.5f;
    instance->voice.color = 0.5f;
    instance->voice.note = 60.0f;
    instance->voice.velocity = 0.8f;
    instance->voice.active_note = 60U;
    instance->voice.has_active_note = 0U;
    instance->voice.gate = 0U;
    instance->voice.trigger = 0U;
    instance->has_note = 0U;
    instance->level = 0.0f;
    instance->oscillator.Init();
    instance->oscillator.set_shape(brick6_braids_runtime_shape_from_edit(instance->voice.edit));
    instance->oscillator.set_pitch(brick6_braids_runtime_pitch_to_q7(&instance->voice));
    instance->oscillator.set_parameters(
        brick6_braids_runtime_float_to_u15(instance->voice.timbre),
        brick6_braids_runtime_float_to_u15(instance->voice.color));
}

}  // namespace

extern "C" {

void brick6_braids_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_BRAIDS_MAX_INSTANCES; ++instance)
    {
        brick6_braids_runtime_init_instance(&g_braids_runtime[instance]);
    }
}

void brick6_braids_runtime_reset_instance(uint8_t instance_id)
{
    brick6_braids_runtime_init_instance(brick6_braids_runtime_get_instance_mut(instance_id));
}

void brick6_braids_runtime_set_edit(uint8_t instance_id, float edit)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.edit = brick6_braids_runtime_clamp(edit, 0.0f, kBraidsEditMax);
        instance->oscillator.set_shape(brick6_braids_runtime_shape_from_edit(instance->voice.edit));
    }
}

void brick6_braids_runtime_set_fine(uint8_t instance_id, float fine)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.fine = brick6_braids_runtime_clamp(fine, 0.0f, 1.0f);
    }
}

void brick6_braids_runtime_set_coarse(uint8_t instance_id, float coarse)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.coarse = brick6_braids_runtime_clamp(coarse, 0.0f, 1.0f);
    }
}

void brick6_braids_runtime_set_fm(uint8_t instance_id, float fm)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.fm = brick6_braids_runtime_clamp(fm, 0.0f, 1.0f);
    }
}

void brick6_braids_runtime_set_timbre(uint8_t instance_id, float timbre)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.timbre = brick6_braids_runtime_clamp(timbre, 0.0f, 1.0f);
    }
}

void brick6_braids_runtime_set_modulation(uint8_t instance_id, float modulation)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.modulation = brick6_braids_runtime_clamp(modulation, 0.0f, 1.0f);
    }
}

void brick6_braids_runtime_set_color(uint8_t instance_id, float color)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.color = brick6_braids_runtime_clamp(color, 0.0f, 1.0f);
    }
}

void brick6_braids_runtime_note_on(uint8_t instance_id, float note, float velocity)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    const float clamped_velocity = brick6_braids_runtime_clamp(velocity, 0.0f, 1.0f);
    const uint8_t midi_note = (uint8_t)brick6_braids_runtime_clamp(note, 0.0f, 127.0f);
    if (clamped_velocity <= 0.0f)
    {
        brick6_braids_runtime_note_off(instance_id, midi_note);
        return;
    }

    instance->voice.note = brick6_braids_runtime_clamp(note, 0.0f, 127.0f);
    instance->voice.velocity = clamped_velocity;
    instance->voice.active_note = midi_note;
    instance->voice.has_active_note = 1U;
    instance->voice.gate = 1U;
    instance->voice.trigger = 1U;
    instance->has_note = 1U;
}

void brick6_braids_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    if ((instance->voice.has_active_note == 0U) || (instance->voice.active_note != note))
    {
        return;
    }

    instance->voice.has_active_note = 0U;
    instance->voice.gate = 0U;
    instance->voice.trigger = 0U;
}

void brick6_braids_runtime_all_notes_off(uint8_t instance_id)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    instance->voice.has_active_note = 0U;
    instance->voice.gate = 0U;
    instance->voice.trigger = 0U;
}

void brick6_braids_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.trigger = 0U;
    }
}

void brick6_braids_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((instance->has_note == 0U) && (instance->voice.gate == 0U) && (instance->voice.trigger == 0U) && (instance->level <= 1.0e-5f))
    {
        memset(out_mono, 0, frames * sizeof(float));
        return;
    }

    const float velocity_gain = 0.2f + (brick6_braids_runtime_clamp(instance->voice.velocity, 0.0f, 1.0f) * 0.8f);
    const float gate_target = (instance->voice.gate != 0U) ? velocity_gain : 0.0f;
    instance->oscillator.set_shape(brick6_braids_runtime_shape_from_edit(instance->voice.edit));
    instance->oscillator.set_pitch(brick6_braids_runtime_pitch_to_q7(&instance->voice));
    instance->oscillator.set_parameters(
        brick6_braids_runtime_float_to_u15(instance->voice.timbre + ((instance->voice.modulation - 0.5f) * 0.5f)),
        brick6_braids_runtime_float_to_u15(instance->voice.color));

    if (instance->voice.trigger != 0U)
    {
        instance->oscillator.Strike();
    }

    uint32_t offset = 0U;
    static const uint8_t sync_block[kBraidsRenderBlockSize] = { 0 };
    int16_t sample_block[kBraidsRenderBlockSize];

    while (offset < frames)
    {
        const size_t block = ((frames - offset) > kBraidsRenderBlockSize) ? (size_t)kBraidsRenderBlockSize : (size_t)(frames - offset);
        instance->oscillator.Render(sync_block, sample_block, block);

        for (size_t i = 0U; i < block; ++i)
        {
            const float coeff = (gate_target > instance->level) ? 0.05f : (1.0f - kBraidsReleaseCoeff);
            instance->level += (gate_target - instance->level) * coeff;
            out_mono[offset + i] = brick6_braids_runtime_clamp(((float)sample_block[i] / 32768.0f) * instance->level, -1.0f, 1.0f);
        }

        offset += (uint32_t)block;
    }

    instance->voice.trigger = 0U;
    if ((instance->voice.gate == 0U) && (instance->level <= 1.0e-5f))
    {
        instance->level = 0.0f;
        instance->has_note = 0U;
    }
}

const brick6_braids_runtime_voice_t *brick6_braids_runtime_get_voice(uint8_t instance_id)
{
    const brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}

}  // extern "C"
