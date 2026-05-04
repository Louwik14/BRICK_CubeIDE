/**
 * @file brick6_opal_runtime.cpp
 * @brief Minimal track-aware Opal runtime wrapper backed by the Mutable Plaits 6-op DSP.
 */

#include "Core/brick6_opal_runtime.h"

#include <string.h>

#include "Storage/memory_layout.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/resources.h"
#include "stmlib/utils/buffer_allocator.h"

namespace {

constexpr size_t kPlaitsAllocatorBytes = 8192U;
constexpr float kOpalFixedPitchOffset = 0.0f;
typedef struct
{
    brick6_opal_runtime_voice_t voice;
    uint8_t has_note;
    uint8_t bank_loaded;
    alignas(8) uint8_t allocator_buffer[kPlaitsAllocatorBytes];
    stmlib::BufferAllocator allocator;
    plaits::SixOpEngine synth_voice;
} brick6_opal_runtime_instance_t;

AUDIO_HOT static brick6_opal_runtime_instance_t g_opal_runtime[BRICK6_PLAITS_MAX_INSTANCES];

static float brick6_opal_runtime_clamp(float value, float lo, float hi)
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

static void brick6_opal_runtime_init_instance(brick6_opal_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    instance->voice.patch = 0.5f;
    instance->voice.index = 0.5f;
    instance->voice.time = 0.5f;
    instance->voice.note = 60.0f;
    instance->voice.velocity = 0.8f;
    instance->voice.active_note = 60U;
    instance->voice.has_active_note = 0U;
    instance->voice.gate = 0U;
    instance->voice.trigger = 0U;
    instance->has_note = 0U;
    instance->bank_loaded = 0U;

    memset(instance->allocator_buffer, 0, sizeof(instance->allocator_buffer));
    instance->allocator.Init(instance->allocator_buffer, sizeof(instance->allocator_buffer));
    instance->synth_voice.Init(&instance->allocator);
    instance->synth_voice.LoadUserData(plaits::fm_patches_table[0]);
    instance->synth_voice.Reset();
    instance->bank_loaded = 1U;
}

static brick6_opal_runtime_instance_t *brick6_opal_runtime_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_PLAITS_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_opal_runtime[instance_id];
}

static const brick6_opal_runtime_instance_t *brick6_opal_runtime_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_PLAITS_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_opal_runtime[instance_id];
}

}  // namespace

extern "C" {

void brick6_opal_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_PLAITS_MAX_INSTANCES; ++instance)
    {
        brick6_opal_runtime_init_instance(&g_opal_runtime[instance]);
    }
}

void brick6_opal_runtime_reset_instance(uint8_t instance_id)
{
    brick6_opal_runtime_init_instance(brick6_opal_runtime_get_instance_mut(instance_id));
}

void brick6_opal_runtime_set_harmonics(uint8_t instance_id, float patch)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.patch = brick6_opal_runtime_clamp(patch, 0.0f, 1.0f);
    }
}

void brick6_opal_runtime_set_timbre(uint8_t instance_id, float index)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.index = brick6_opal_runtime_clamp(index, 0.0f, 1.0f);
    }
}

void brick6_opal_runtime_set_morph(uint8_t instance_id, float time)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.time = brick6_opal_runtime_clamp(time, 0.0f, 1.0f);
    }
}

void brick6_opal_runtime_note_on(uint8_t instance_id, float note, float velocity)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        const float clamped_velocity = brick6_opal_runtime_clamp(velocity, 0.0f, 1.0f);
        const uint8_t midi_note = (uint8_t)brick6_opal_runtime_clamp(note, 0.0f, 127.0f);
        if (clamped_velocity <= 0.0f)
        {
            brick6_opal_runtime_note_off(instance_id, midi_note);
            return;
        }

        instance->voice.note = note;
        instance->voice.velocity = clamped_velocity;
        instance->voice.active_note = midi_note;
        instance->voice.has_active_note = 1U;
        instance->voice.gate = 1U;
        instance->voice.trigger = 1U;
        instance->has_note = 1U;
    }
}

void brick6_opal_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        if ((instance->voice.has_active_note == 0U) || (instance->voice.active_note != note))
        {
            return;
        }

        instance->voice.has_active_note = 0U;
        instance->voice.gate = 0U;
        instance->voice.trigger = 0U;
    }
}

void brick6_opal_runtime_all_notes_off(uint8_t instance_id)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->has_note = 0U;
        instance->voice.has_active_note = 0U;
        instance->voice.gate = 0U;
        instance->voice.trigger = 0U;
    }
}

void brick6_opal_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.trigger = 0U;
    }
}

void brick6_opal_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((instance->has_note == 0U) && (instance->voice.gate == 0U) && (instance->voice.trigger == 0U))
    {
        memset(out_mono, 0, frames * sizeof(float));
        return;
    }

    const int trigger = ((instance->voice.trigger != 0U) ? (int)plaits::TRIGGER_RISING_EDGE : (int)plaits::TRIGGER_LOW)
        | ((instance->voice.gate != 0U) ? (int)plaits::TRIGGER_HIGH : (int)plaits::TRIGGER_LOW);
    const float note = instance->voice.note + kOpalFixedPitchOffset;
    const float timbre = instance->voice.index;
    const float morph = instance->voice.time;
    const float harmonics = instance->voice.patch;
    const float accent = instance->voice.velocity;

    plaits::EngineParameters parameters;
    parameters.trigger = trigger;
    parameters.note = note;
    parameters.timbre = timbre;
    parameters.morph = morph;
    parameters.harmonics = harmonics;
    parameters.accent = accent;

    uint32_t offset = 0U;
    while (offset < frames)
    {
        const size_t block = ((frames - offset) > plaits::kMaxBlockSize) ? plaits::kMaxBlockSize : (size_t)(frames - offset);
        float out_block[plaits::kMaxBlockSize];
        float aux_block[plaits::kMaxBlockSize];
        bool already_enveloped = false;
        instance->synth_voice.Render(parameters, out_block, aux_block, block, &already_enveloped);

        for (size_t i = 0U; i < block; ++i)
        {
            (void)aux_block;
            (void)already_enveloped;
            out_mono[offset + i] = -out_block[i];
        }

        offset += (uint32_t)block;
    }

    instance->voice.trigger = 0U;
}

const brick6_opal_runtime_voice_t *brick6_opal_runtime_get_voice(uint8_t instance_id)
{
    const brick6_opal_runtime_instance_t *const instance = brick6_opal_runtime_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}

}  // extern "C"
