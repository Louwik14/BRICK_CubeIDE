/**
 * @file brick6_plaits_runtime.cpp
 * @brief Minimal track-aware Plaits runtime wrapper.
 */

#include "Core/brick6_plaits_runtime.h"

#include <math.h>
#include <new>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#include "stmlib/utils/buffer_allocator.h"

namespace {

constexpr uint8_t kPlaitsModelCount = 22U;
constexpr size_t kPlaitsAllocatorBytes = 8192U;

union PlaitsEngineStorage {
    plaits::VirtualAnalogEngine virtual_analog;
    plaits::WaveshapingEngine waveshaping;
    plaits::FMEngine fm;
    plaits::GrainEngine grain;
    plaits::WavetableEngine wavetable;
    plaits::ChordEngine chord;
    plaits::SpeechEngine speech;
    plaits::SwarmEngine swarm;
    plaits::NoiseEngine noise;
    plaits::ParticleEngine particle;
    plaits::StringEngine string_engine;
    plaits::ModalEngine modal;
    plaits::AdditiveEngine additive;
    plaits::BassDrumEngine bass_drum;
    plaits::SnareDrumEngine snare_drum;
    plaits::HiHatEngine hi_hat;
    plaits::PhaseDistortionEngine phase_distortion;
    plaits::SixOpEngine six_op;
    plaits::WaveTerrainEngine wave_terrain;
    plaits::StringMachineEngine string_machine;
    plaits::ChiptuneEngine chiptune;
    plaits::VirtualAnalogVCFEngine virtual_analog_vcf;

    PlaitsEngineStorage() {}
    ~PlaitsEngineStorage() {}
};

typedef struct
{
    brick6_plaits_runtime_voice_t voice;
    uint8_t active_model;
    uint8_t engine_ready;
    uint8_t has_note;
    float envelope;
    alignas(8) uint8_t allocator_buffer[kPlaitsAllocatorBytes];
    stmlib::BufferAllocator allocator;
    PlaitsEngineStorage storage;
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

static void brick6_plaits_runtime_set_defaults(brick6_plaits_runtime_instance_t *track)
{
    if (track == NULL)
    {
        return;
    }

    track->voice.model = 0.0f;
    track->voice.coarse_frequency = 0.5f;
    track->voice.harmonics = 0.5f;
    track->voice.timbre = 0.5f;
    track->voice.morph = 0.5f;
    track->voice.lpg_response = 0.0f;
    track->voice.decay = 0.5f;
    track->voice.frequency_range = 0.5f;
    track->voice.note = 60.0f;
    track->voice.velocity = 0.8f;
    track->voice.gate = 0U;
    track->voice.trigger = 0U;
    track->active_model = 0xFFU;
    track->engine_ready = 0U;
    track->has_note = 0U;
    track->envelope = 0.0f;
    memset(track->allocator_buffer, 0, sizeof(track->allocator_buffer));
    track->allocator.Init(track->allocator_buffer, sizeof(track->allocator_buffer));
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

static plaits::Engine *brick6_plaits_runtime_engine_ptr(brick6_plaits_runtime_instance_t *track)
{
    if ((track == NULL) || (track->engine_ready == 0U))
    {
        return NULL;
    }

    switch (track->active_model)
    {
        case 0U: return &track->storage.virtual_analog;
        case 1U: return &track->storage.waveshaping;
        case 2U: return &track->storage.fm;
        case 3U: return &track->storage.grain;
        case 4U: return &track->storage.wavetable;
        case 5U: return &track->storage.chord;
        case 6U: return &track->storage.speech;
        case 7U: return &track->storage.swarm;
        case 8U: return &track->storage.noise;
        case 9U: return &track->storage.particle;
        case 10U: return &track->storage.string_engine;
        case 11U: return &track->storage.modal;
        case 12U: return &track->storage.additive;
        case 13U: return &track->storage.bass_drum;
        case 14U: return &track->storage.snare_drum;
        case 15U: return &track->storage.hi_hat;
        case 16U: return &track->storage.phase_distortion;
        case 17U: return &track->storage.six_op;
        case 18U: return &track->storage.wave_terrain;
        case 19U: return &track->storage.string_machine;
        case 20U: return &track->storage.chiptune;
        case 21U: return &track->storage.virtual_analog_vcf;
        default: return NULL;
    }
}

static void brick6_plaits_runtime_destroy_engine(brick6_plaits_runtime_instance_t *track)
{
    if ((track == NULL) || (track->engine_ready == 0U))
    {
        return;
    }

    switch (track->active_model)
    {
        case 0U: track->storage.virtual_analog.~VirtualAnalogEngine(); break;
        case 1U: track->storage.waveshaping.~WaveshapingEngine(); break;
        case 2U: track->storage.fm.~FMEngine(); break;
        case 3U: track->storage.grain.~GrainEngine(); break;
        case 4U: track->storage.wavetable.~WavetableEngine(); break;
        case 5U: track->storage.chord.~ChordEngine(); break;
        case 6U: track->storage.speech.~SpeechEngine(); break;
        case 7U: track->storage.swarm.~SwarmEngine(); break;
        case 8U: track->storage.noise.~NoiseEngine(); break;
        case 9U: track->storage.particle.~ParticleEngine(); break;
        case 10U: track->storage.string_engine.~StringEngine(); break;
        case 11U: track->storage.modal.~ModalEngine(); break;
        case 12U: track->storage.additive.~AdditiveEngine(); break;
        case 13U: track->storage.bass_drum.~BassDrumEngine(); break;
        case 14U: track->storage.snare_drum.~SnareDrumEngine(); break;
        case 15U: track->storage.hi_hat.~HiHatEngine(); break;
        case 16U: track->storage.phase_distortion.~PhaseDistortionEngine(); break;
        case 17U: track->storage.six_op.~SixOpEngine(); break;
        case 18U: track->storage.wave_terrain.~WaveTerrainEngine(); break;
        case 19U: track->storage.string_machine.~StringMachineEngine(); break;
        case 20U: track->storage.chiptune.~ChiptuneEngine(); break;
        case 21U: track->storage.virtual_analog_vcf.~VirtualAnalogVCFEngine(); break;
        default: break;
    }

    track->engine_ready = 0U;
    track->active_model = 0xFFU;
}

static void brick6_plaits_runtime_construct_engine(brick6_plaits_runtime_instance_t *track, uint8_t model)
{
    if (track == NULL)
    {
        return;
    }

    brick6_plaits_runtime_destroy_engine(track);
    track->allocator.Free();
    track->engine_ready = 1U;
    track->active_model = model;

    switch (model)
    {
        case 0U: new (&track->storage.virtual_analog) plaits::VirtualAnalogEngine(); break;
        case 1U: new (&track->storage.waveshaping) plaits::WaveshapingEngine(); break;
        case 2U: new (&track->storage.fm) plaits::FMEngine(); break;
        case 3U: new (&track->storage.grain) plaits::GrainEngine(); break;
        case 4U: new (&track->storage.wavetable) plaits::WavetableEngine(); break;
        case 5U: new (&track->storage.chord) plaits::ChordEngine(); break;
        case 6U: new (&track->storage.speech) plaits::SpeechEngine(); break;
        case 7U: new (&track->storage.swarm) plaits::SwarmEngine(); break;
        case 8U: new (&track->storage.noise) plaits::NoiseEngine(); break;
        case 9U: new (&track->storage.particle) plaits::ParticleEngine(); break;
        case 10U: new (&track->storage.string_engine) plaits::StringEngine(); break;
        case 11U: new (&track->storage.modal) plaits::ModalEngine(); break;
        case 12U: new (&track->storage.additive) plaits::AdditiveEngine(); break;
        case 13U: new (&track->storage.bass_drum) plaits::BassDrumEngine(); break;
        case 14U: new (&track->storage.snare_drum) plaits::SnareDrumEngine(); break;
        case 15U: new (&track->storage.hi_hat) plaits::HiHatEngine(); break;
        case 16U: new (&track->storage.phase_distortion) plaits::PhaseDistortionEngine(); break;
        case 17U: new (&track->storage.six_op) plaits::SixOpEngine(); break;
        case 18U: new (&track->storage.wave_terrain) plaits::WaveTerrainEngine(); break;
        case 19U: new (&track->storage.string_machine) plaits::StringMachineEngine(); break;
        case 20U: new (&track->storage.chiptune) plaits::ChiptuneEngine(); break;
        case 21U: new (&track->storage.virtual_analog_vcf) plaits::VirtualAnalogVCFEngine(); break;
        default:
            track->engine_ready = 0U;
            track->active_model = 0xFFU;
            return;
    }

    plaits::Engine *const engine = brick6_plaits_runtime_engine_ptr(track);
    if (engine != NULL)
    {
        engine->Init(&track->allocator);
        engine->Reset();
    }
}

static plaits::Engine *brick6_plaits_runtime_prepare_engine(brick6_plaits_runtime_instance_t *track)
{
    if (track == NULL)
    {
        return NULL;
    }

    const uint8_t model = (uint8_t)(brick6_plaits_runtime_clamp(track->voice.model, 0.0f, (float)(kPlaitsModelCount - 1U)) + 0.5f);
    if ((track->engine_ready == 0U) || (track->active_model != model))
    {
        brick6_plaits_runtime_construct_engine(track, model);
    }

    return brick6_plaits_runtime_engine_ptr(track);
}

static float brick6_plaits_runtime_base_note(const brick6_plaits_runtime_voice_t *voice)
{
    static const float range_offsets[] = { -24.0f, -12.0f, 0.0f, 12.0f };
    const uint8_t range_index = (uint8_t)(brick6_plaits_runtime_clamp(voice->frequency_range, 0.0f, 1.0f) * 3.0f + 0.5f);
    const float coarse = (brick6_plaits_runtime_clamp(voice->coarse_frequency, 0.0f, 1.0f) - 0.5f) * 48.0f;
    return voice->note + coarse + range_offsets[range_index];
}

}  // namespace

extern "C" {

void brick6_plaits_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_PLAITS_MAX_INSTANCES; ++instance)
    {
        brick6_plaits_runtime_set_defaults(&g_plaits_runtime[instance]);
    }
}

void brick6_plaits_runtime_reset_instance(uint8_t instance_id)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    brick6_plaits_runtime_destroy_engine(track);
    brick6_plaits_runtime_set_defaults(track);
}

void brick6_plaits_runtime_set_model(uint8_t instance_id, float model)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.model = brick6_plaits_runtime_clamp(model, 0.0f, (float)(kPlaitsModelCount - 1U));
    }
}

void brick6_plaits_runtime_set_coarse_frequency(uint8_t instance_id, float coarse_frequency)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.coarse_frequency = brick6_plaits_runtime_clamp(coarse_frequency, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_harmonics(uint8_t instance_id, float harmonics)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.harmonics = brick6_plaits_runtime_clamp(harmonics, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_timbre(uint8_t instance_id, float timbre)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.timbre = brick6_plaits_runtime_clamp(timbre, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_morph(uint8_t instance_id, float morph)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.morph = brick6_plaits_runtime_clamp(morph, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_lpg_response(uint8_t instance_id, float lpg_response)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.lpg_response = brick6_plaits_runtime_clamp(lpg_response, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_decay(uint8_t instance_id, float decay)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.decay = brick6_plaits_runtime_clamp(decay, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_set_frequency_range(uint8_t instance_id, float frequency_range)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.frequency_range = brick6_plaits_runtime_clamp(frequency_range, 0.0f, 1.0f);
    }
}

void brick6_plaits_runtime_note_on(uint8_t instance_id, float note, float velocity)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.note = note;
        track->voice.velocity = velocity;
        track->voice.gate = 1U;
        track->voice.trigger = 1U;
        track->has_note = 1U;
    }
}

void brick6_plaits_runtime_note_off(uint8_t instance_id)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.gate = 0U;
        track->voice.trigger = 0U;
    }
}

void brick6_plaits_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if (track != NULL)
    {
        track->voice.trigger = 0U;
    }
}

void brick6_plaits_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance_mut(instance_id);
    if ((track == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((track->has_note == 0U) && (track->voice.gate == 0U) && (track->voice.trigger == 0U))
    {
        memset(out_mono, 0, frames * sizeof(float));
        return;
    }

    plaits::Engine *const engine = brick6_plaits_runtime_prepare_engine(track);
    if (engine == NULL)
    {
        memset(out_mono, 0, frames * sizeof(float));
        return;
    }

    uint32_t offset = 0U;
    while (offset < frames)
    {
        const size_t block = ((frames - offset) > plaits::kMaxBlockSize) ? plaits::kMaxBlockSize : (size_t)(frames - offset);
        float out_block[plaits::kMaxBlockSize] = { 0.0f };
        float aux_block[plaits::kMaxBlockSize] = { 0.0f };
        bool already_enveloped = false;

        plaits::EngineParameters parameters;
        parameters.trigger = track->voice.trigger != 0U
                ? plaits::TRIGGER_RISING_EDGE
                : (track->voice.gate != 0U ? plaits::TRIGGER_HIGH : plaits::TRIGGER_LOW);
        parameters.note = brick6_plaits_runtime_base_note(&track->voice);
        parameters.timbre = brick6_plaits_runtime_clamp(track->voice.timbre, 0.0f, 1.0f);
        parameters.morph = brick6_plaits_runtime_clamp(track->voice.morph, 0.0f, 1.0f);
        parameters.harmonics = brick6_plaits_runtime_clamp(track->voice.harmonics, 0.0f, 1.0f);
        parameters.accent = brick6_plaits_runtime_clamp(track->voice.velocity, 0.0f, 1.0f);

        engine->Render(parameters, out_block, aux_block, block, &already_enveloped);

        float gain = engine->post_processing_settings.out_gain;
        if (gain <= 0.0f)
        {
            gain = 1.0f;
        }

        const float lpg_response = brick6_plaits_runtime_clamp(track->voice.lpg_response, 0.0f, 1.0f);
        const float decay = brick6_plaits_runtime_clamp(track->voice.decay, 0.0f, 1.0f);
        const float sustain = (track->voice.gate != 0U) ? (0.15f + 0.75f * lpg_response) : 0.0f;
        const float decay_rate = 0.0005f + (1.0f - decay) * 0.01f;

        for (size_t i = 0U; i < block; ++i)
        {
            if (track->voice.trigger != 0U)
            {
                track->envelope = 1.0f;
            }

            track->envelope += (sustain - track->envelope) * decay_rate;

            const float envelope = already_enveloped ? 1.0f : track->envelope;
            out_mono[offset + i] = out_block[i] * gain * 0.2f * envelope;
        }

        offset += (uint32_t)block;
    }

    track->voice.trigger = 0U;
}

const brick6_plaits_runtime_voice_t *brick6_plaits_runtime_get_voice(uint8_t instance_id)
{
    const brick6_plaits_runtime_instance_t *const track = brick6_plaits_runtime_get_instance(instance_id);
    return (track != NULL) ? &track->voice : NULL;
}

}  // extern "C"
