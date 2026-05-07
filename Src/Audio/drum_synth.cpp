#include "Audio/drum_synth.h"

#include <cstring>

#include "Seq/seq_types.h"

#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/engine/engine.h"

namespace
{
constexpr float kDefaultPitch = 0.0f;
constexpr float kDefaultDecay = 0.4f;
constexpr float kDefaultTone = 0.0f;
constexpr float kDefaultFm = 0.3f;

typedef struct
{
    plaits::AnalogBassDrum bd;
    drum_model_id_t model;
    float midi_note;
    float accent;
    float pitch;
    float decay;
    float tone;
    float fm;
    uint8_t initialized;
    uint8_t triggered;
    uint8_t trigger_pending;
} drum_synth_instance_t;

static drum_synth_instance_t g_drum_instances[SEQ_TRACK_COUNT];

static inline float clampf_local(float value, float lo, float hi)
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

static inline drum_synth_instance_t *drum_instance(uint8_t instance_id)
{
    return (instance_id < SEQ_TRACK_COUNT) ? &g_drum_instances[instance_id] : nullptr;
}

static void drum_instance_reset_params(drum_synth_instance_t *instance)
{
    instance->midi_note = 36.0f;
    instance->accent = 1.0f;
    instance->pitch = kDefaultPitch;
    instance->decay = kDefaultDecay;
    instance->tone = kDefaultTone;
    instance->fm = kDefaultFm;
    instance->triggered = 0U;
    instance->trigger_pending = 0U;
}

static void drum_instance_init(drum_synth_instance_t *instance)
{
    instance->bd.Init();
    instance->model = DRUM_MODEL_ID_NONE;
    drum_instance_reset_params(instance);
    instance->initialized = 1U;
}

static void drum_instance_ensure_init(drum_synth_instance_t *instance)
{
    if (instance->initialized == 0U)
    {
        drum_instance_init(instance);
    }
}
}

void drum_synth_init(float sample_rate)
{
    (void)sample_rate;

    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        drum_instance_init(&g_drum_instances[i]);
    }
}

uint8_t drum_synth_set_model_for_instance(uint8_t instance_id, drum_model_id_t model_type)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return 0U;
    }

    drum_instance_ensure_init(instance);

    if ((model_type != DRUM_MODEL_ID_NONE)
            && (model_type != DRUM_MODEL_ID_TRX_BD)
            && (model_type != DRUM_MODEL_ID_BD_ANALOG))
    {
        return 0U;
    }

    if (instance->model != model_type)
    {
        instance->model = model_type;
        instance->bd.Init();
        instance->triggered = 0U;
        instance->trigger_pending = 0U;
    }

    return 1U;
}

drum_model_id_t drum_synth_get_model_for_instance(uint8_t instance_id)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return DRUM_MODEL_ID_NONE;
    }

    drum_instance_ensure_init(instance);
    return instance->model;
}

void drum_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return;
    }

    drum_instance_ensure_init(instance);
    if (instance->model != DRUM_MODEL_ID_BD_ANALOG)
    {
        return;
    }

    instance->midi_note = (float)midi_note;
    instance->accent = clampf_local((float)velocity / 127.0f, 0.0f, 1.0f);
    instance->triggered = 1U;
    instance->trigger_pending = 1U;
}

void drum_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    (void)instance_id;
    (void)midi_note;
}

void drum_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return;
    }

    drum_instance_ensure_init(instance);
    instance->bd.Init();
    instance->triggered = 0U;
    instance->trigger_pending = 0U;
}

void drum_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames)
{
    if (mono_out == nullptr)
    {
        return;
    }

    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        std::memset(mono_out, 0, (size_t)frames * sizeof(float));
        return;
    }

    drum_instance_ensure_init(instance);
    if ((instance->model != DRUM_MODEL_ID_BD_ANALOG) || (instance->triggered == 0U))
    {
        std::memset(mono_out, 0, (size_t)frames * sizeof(float));
        return;
    }

    const bool trigger = (instance->trigger_pending != 0U);
    instance->trigger_pending = 0U;

    const float note = instance->midi_note + instance->pitch;
    const float f0 = plaits::NoteToFrequency(note);
    const float accent = clampf_local(instance->accent, 0.0f, 1.0f);
    const float tone = clampf_local(instance->tone, 0.0f, 1.0f);
    const float decay = clampf_local(instance->decay * 0.5f, 0.0f, 1.0f);
    const float attack_fm = clampf_local(instance->fm, 0.0f, 1.0f);

    instance->bd.Render(false,
                        trigger,
                        accent,
                        f0,
                        tone,
                        decay,
                        attack_fm,
                        0.0f,
                        mono_out,
                        (size_t)frames);
}

uint8_t drum_synth_set_param_for_instance(uint8_t instance_id, param_id_t param, float value)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return 0U;
    }

    drum_instance_ensure_init(instance);
    if (instance->model != DRUM_MODEL_ID_BD_ANALOG)
    {
        return 0U;
    }

    switch (param)
    {
        case PARAM_DRUM_TRX_BD_PITCH:
            instance->pitch = clampf_local(value, -48.0f, 24.0f);
            return 1U;
        case PARAM_DRUM_TRX_BD_DECAY:
            instance->decay = clampf_local(value, 0.01f, 2.0f);
            return 1U;
        case PARAM_DRUM_TRX_BD_HARMONICS:
            instance->tone = clampf_local(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            instance->fm = clampf_local(value, 0.0f, 1.0f);
            return 1U;
        default:
            return 0U;
    }
}

void drum_synth_all_notes_off_all(void)
{
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        drum_synth_all_notes_off_for_instance(i);
    }
}
