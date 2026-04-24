/**
 * @file brick6_plaits_runtime.cpp
 * @brief Minimal track-aware Plaits runtime wrapper.
 */

#include "Core/brick6_plaits_runtime.h"

#include <string.h>

#include "Storage/memory_layout.h"

#include "plaits/dsp/dsp.h"
#include "plaits/voice.h"
#include "stmlib/utils/buffer_allocator.h"

namespace {

constexpr size_t kPlaitsAllocatorBytes = 8192U;
typedef struct
{
    brick6_plaits_runtime_voice_t voice;
    uint8_t has_note;
    alignas(8) uint8_t allocator_buffer[kPlaitsAllocatorBytes];
    stmlib::BufferAllocator allocator;
    plaits::Voice synth_voice;
} brick6_plaits_runtime_instance_t;

AUDIO_HOT static brick6_plaits_runtime_instance_t g_plaits_runtime[BRICK6_PLAITS_MAX_INSTANCES];

static float brick6_plaits_runtime_clamp(float value, float lo, float hi)
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

static uint8_t brick6_plaits_runtime_map_model(float model)
{
    static const uint8_t kModelMap[] = {
        8U,   // Virtual Analog
        9U,   // Waveshaping
        10U,  // FM
        11U,  // Grain
        13U,  // Wavetable
        14U,  // Chord
        15U,  // Speech
        16U,  // Swarm
        17U,  // Noise
        18U,  // Particle
        19U,  // String
        20U,  // Modal
        12U,  // Additive
        21U,  // Bass Drum
        22U,  // Snare Drum
        23U,  // Hi-Hat
        1U,   // Phase Distortion
        2U,   // Six Op
        5U,   // Wave Terrain
        6U,   // String Machine
        7U,   // Chiptune
        0U    // Virtual Analog VCF
    };

    const uint8_t index = (uint8_t)(brick6_plaits_runtime_clamp(model, 0.0f, (float)(sizeof(kModelMap) - 1U)) + 0.5f);
    return kModelMap[index];
}

static float brick6_plaits_runtime_pitch_offset(const brick6_plaits_runtime_voice_t *voice)
{
    static const float range_offsets[] = { -24.0f, -12.0f, 0.0f, 12.0f };
    const uint8_t range_index = (uint8_t)(brick6_plaits_runtime_clamp(voice->frequency_range, 0.0f, 1.0f) * 3.0f + 0.5f);
    const float coarse = (brick6_plaits_runtime_clamp(voice->coarse_frequency, 0.0f, 1.0f) - 0.5f) * 48.0f;
    return coarse + range_offsets[range_index];
}

static void brick6_plaits_runtime_init_instance(brick6_plaits_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    instance->voice.model = 0.0f;
    instance->voice.coarse_frequency = 0.5f;
    instance->voice.harmonics = 0.5f;
    instance->voice.timbre = 0.5f;
    instance->voice.morph = 0.5f;
    instance->voice.lpg_response = 0.0f;
    instance->voice.decay = 0.5f;
    instance->voice.frequency_range = 0.5f;
    instance->voice.note = 60.0f;
    instance->voice.velocity = 0.8f;
    instance->voice.active_note = 60U;
    instance->voice.has_active_note = 0U;
    instance->voice.gate = 0U;
    instance->voice.trigger = 0U;
    instance->has_note = 0U;

    memset(instance->allocator_buffer, 0, sizeof(instance->allocator_buffer));
    instance->allocator.Init(instance->allocator_buffer, sizeof(instance->allocator_buffer));
    instance->synth_voice.Init(&instance->allocator);
}

static brick6_plaits_runtime_instance_t *brick6_plaits_runtime_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_PLAITS_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_plaits_runtime[instance_id];
}

static const brick6_plaits_runtime_instance_t *brick6_plaits_runtime_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_PLAITS_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_plaits_runtime[instance_id];
}

}  // namespace

extern "C" {

void brick6_plaits_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_PLAITS_MAX_INSTANCES; ++instance)
    {
        brick6_plaits_runtime_init_instance(&g_plaits_runtime[instance]);
    }
}

void brick6_plaits_runtime_reset_instance(uint8_t instance_id)
{
    brick6_plaits_runtime_init_instance(brick6_plaits_runtime_get_instance_mut(instance_id));
}

void brick6_plaits_runtime_set_model(uint8_t instance_id, float model)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.model = brick6_plaits_runtime_clamp(model, 0.0f, 21.0f);
    }
}

void brick6_plaits_runtime_set_coarse_frequency(uint8_t instance_id, float coarse_frequency)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.coarse_frequency = brick6_plaits_runtime_clamp(coarse_frequency, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_harmonics(uint8_t instance_id, float harmonics)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.harmonics = brick6_plaits_runtime_clamp(harmonics, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_timbre(uint8_t instance_id, float timbre)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.timbre = brick6_plaits_runtime_clamp(timbre, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_morph(uint8_t instance_id, float morph)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.morph = brick6_plaits_runtime_clamp(morph, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_lpg_response(uint8_t instance_id, float lpg_response)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.lpg_response = brick6_plaits_runtime_clamp(lpg_response, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_decay(uint8_t instance_id, float decay)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.decay = brick6_plaits_runtime_clamp(decay, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_frequency_range(uint8_t instance_id, float frequency_range)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.frequency_range = brick6_plaits_runtime_clamp(frequency_range, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_note_on(uint8_t instance_id, float note, float velocity)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        const float clamped_velocity = brick6_plaits_runtime_clamp(velocity, 0.0f, 1.0f);
        const uint8_t midi_note = (uint8_t)brick6_plaits_runtime_clamp(note, 0.0f, 127.0f);
        if (clamped_velocity <= 0.0f)
        {
            brick6_plaits_runtime_note_off(instance_id, midi_note);
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

void brick6_plaits_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
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

void brick6_plaits_runtime_all_notes_off(uint8_t instance_id)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.has_active_note = 0U;
        instance->voice.gate = 0U;
        instance->voice.trigger = 0U;
    }
}

void brick6_plaits_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.trigger = 0U;
    }
}

void brick6_plaits_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((instance->has_note == 0U) && (instance->voice.gate == 0U) && (instance->voice.trigger == 0U))
    {
        memset(out_mono, 0, frames * sizeof(float));
        return;
    }

    plaits::Patch patch;
    patch.note = brick6_plaits_runtime_pitch_offset(&instance->voice);
    patch.harmonics = brick6_plaits_runtime_clamp(instance->voice.harmonics, 0.0f, 1.0f);
    patch.timbre = brick6_plaits_runtime_clamp(instance->voice.timbre, 0.0f, 1.0f);
    patch.morph = brick6_plaits_runtime_clamp(instance->voice.morph, 0.0f, 1.0f);
    patch.frequency_modulation_amount = 0.0f;
    patch.timbre_modulation_amount = 0.0f;
    patch.morph_modulation_amount = 0.0f;
    patch.engine = brick6_plaits_runtime_map_model(instance->voice.model);
    patch.decay = brick6_plaits_runtime_clamp(instance->voice.decay, 0.0f, 1.0f);
    patch.lpg_colour = brick6_plaits_runtime_clamp(instance->voice.lpg_response, 0.0f, 1.0f);

    plaits::Modulations modulations;
    modulations.engine = 0.0f;
    modulations.note = instance->voice.note;
    modulations.frequency = 0.0f;
    modulations.harmonics = 0.0f;
    modulations.timbre = 0.0f;
    modulations.morph = 0.0f;
    modulations.trigger = (instance->voice.gate != 0U || instance->voice.trigger != 0U) ? 1.0f : 0.0f;
    modulations.level = brick6_plaits_runtime_clamp(instance->voice.velocity, 0.0f, 1.0f);
    modulations.frequency_patched = false;
    modulations.timbre_patched = false;
    modulations.morph_patched = false;
    modulations.trigger_patched = true;
    modulations.level_patched = true;

    uint32_t offset = 0U;
    while (offset < frames)
    {
        const size_t block = ((frames - offset) > plaits::kMaxBlockSize) ? plaits::kMaxBlockSize : (size_t)(frames - offset);
        plaits::Voice::Frame frame_block[plaits::kMaxBlockSize];
        instance->synth_voice.Render(patch, modulations, frame_block, block);

        for (size_t i = 0U; i < block; ++i)
        {
            out_mono[offset + i] = -((float)frame_block[i].out / 32768.0f);
        }

        offset += (uint32_t)block;
    }

    instance->voice.trigger = 0U;
}

const brick6_plaits_runtime_voice_t *brick6_plaits_runtime_get_voice(uint8_t instance_id)
{
    const brick6_plaits_runtime_instance_t *const instance = brick6_plaits_runtime_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}

}  // extern "C"
