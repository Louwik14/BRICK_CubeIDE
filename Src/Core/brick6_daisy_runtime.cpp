#include "Core/brick6_daisy_runtime.h"

#include <stddef.h>
#include "Core/brick6_daisy_math.h"
#include "Storage/memory_layout.h"
#include "Synthesis/fm2.h"
#include "Synthesis/formantosc.h"
#include "Synthesis/harmonic_osc.h"
#include "Synthesis/oscillator.h"
#include "Synthesis/oscillatorbank.h"
#include "Synthesis/variablesawosc.h"
#include "Synthesis/variableshapeosc.h"
#include "Synthesis/vosim.h"
#include "Synthesis/zoscillator.h"
#include "audio.h"

typedef struct
{
    uint8_t initialized;
    uint8_t gate;
    uint8_t active_note;
    float velocity;
    float frequency_hz;
    brick6_daisy_model_t model;
    float param[BRICK6_DAISY_PARAM_COUNT];
    daisysp::Oscillator osc;
    daisysp::VariableSawOscillator var_saw;
    daisysp::VariableShapeOscillator var_shape;
    daisysp::Fm2 fm2;
    daisysp::FormantOscillator formant;
    daisysp::VosimOscillator vosim;
    daisysp::ZOscillator z_osc;
    daisysp::OscillatorBank osc_bank;
    daisysp::HarmonicOscillator<16> harmonic;
} brick6_daisy_runtime_instance_t;

static AUDIO_HOT brick6_daisy_runtime_instance_t g_daisy_instances[BRICK6_DAISY_MAX_INSTANCES];

static uint8_t brick6_daisy_runtime_instance_valid(uint8_t instance_id)
{
    return (instance_id < (uint8_t)BRICK6_DAISY_MAX_INSTANCES) ? 1U : 0U;
}

static void brick6_daisy_runtime_apply_frequency(brick6_daisy_runtime_instance_t *inst)
{
    const float freq = inst->frequency_hz;
    inst->osc.SetFreq(freq);
    inst->var_saw.SetFreq(freq);
    inst->var_shape.SetFreq(freq);
    inst->var_shape.SetSyncFreq(freq * 2.0f);
    inst->fm2.SetFrequency(freq);
    inst->formant.SetCarrierFreq(freq);
    inst->formant.SetFormantFreq(freq * 2.0f);
    inst->vosim.SetFreq(freq);
    inst->vosim.SetForm1Freq(freq * 2.0f);
    inst->vosim.SetForm2Freq(freq * 3.0f);
    inst->z_osc.SetFreq(freq);
    inst->z_osc.SetFormantFreq(freq * 2.0f);
    inst->osc_bank.SetFreq(freq);
    inst->harmonic.SetFreq(freq);
}

static float brick6_daisy_clamp01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static float brick6_daisy_ratio_from_norm(float value, float min_ratio, float max_ratio)
{
    const float clamped = brick6_daisy_clamp01(value);
    return min_ratio + ((max_ratio - min_ratio) * clamped);
}

static void brick6_daisy_runtime_apply_params(brick6_daisy_runtime_instance_t *inst)
{
    if (inst == NULL)
    {
        return;
    }

    switch (inst->model)
    {
        case BRICK6_DAISY_MODEL_OSC:
        {
            const uint8_t wave = (uint8_t)(brick6_daisy_ratio_from_norm(inst->param[0], 0.0f, 7.0f) + 0.5f);
            inst->osc.SetWaveform(wave);
            inst->osc.SetPw(brick6_daisy_clamp01(inst->param[1]));
            break;
        }
        case BRICK6_DAISY_MODEL_VAR_SAW:
            inst->var_saw.SetWaveshape(brick6_daisy_clamp01(inst->param[0]));
            inst->var_saw.SetPW(brick6_daisy_clamp01(inst->param[1]));
            break;
        case BRICK6_DAISY_MODEL_VAR_SHAPE:
            inst->var_shape.SetWaveshape(brick6_daisy_clamp01(inst->param[0]));
            inst->var_shape.SetPW(brick6_daisy_clamp01(inst->param[1]));
            inst->var_shape.SetSync(inst->param[2] >= 0.5f);
            inst->var_shape.SetSyncFreq(inst->frequency_hz * brick6_daisy_ratio_from_norm(inst->param[3], 1.0f, 8.0f));
            break;
        case BRICK6_DAISY_MODEL_FM2:
            inst->fm2.SetRatio(brick6_daisy_ratio_from_norm(inst->param[0], 0.25f, 8.0f));
            inst->fm2.SetIndex(brick6_daisy_ratio_from_norm(inst->param[1], 0.0f, 10.0f));
            break;
        case BRICK6_DAISY_MODEL_FORMANT:
            inst->formant.SetFormantFreq(inst->frequency_hz * brick6_daisy_ratio_from_norm(inst->param[0], 0.5f, 8.0f));
            inst->formant.SetPhaseShift(brick6_daisy_ratio_from_norm(inst->param[1], -1.0f, 1.0f));
            break;
        case BRICK6_DAISY_MODEL_VOSIM:
            inst->vosim.SetForm1Freq(inst->frequency_hz * brick6_daisy_ratio_from_norm(inst->param[0], 0.5f, 8.0f));
            inst->vosim.SetForm2Freq(inst->frequency_hz * brick6_daisy_ratio_from_norm(inst->param[1], 0.5f, 12.0f));
            inst->vosim.SetShape(brick6_daisy_ratio_from_norm(inst->param[2], -1.0f, 1.0f));
            break;
        case BRICK6_DAISY_MODEL_Z_OSC:
            inst->z_osc.SetFormantFreq(inst->frequency_hz * brick6_daisy_ratio_from_norm(inst->param[0], 0.5f, 8.0f));
            inst->z_osc.SetShape(brick6_daisy_clamp01(inst->param[1]));
            inst->z_osc.SetMode(brick6_daisy_ratio_from_norm(inst->param[2], -1.0f, 1.0f));
            break;
        case BRICK6_DAISY_MODEL_OSC_BANK:
        {
            float registration[7];
            float sum = 0.0f;
            for (uint8_t i = 0U; i < 7U; ++i)
            {
                registration[i] = brick6_daisy_clamp01(inst->param[i]);
                sum += registration[i];
            }
            if (sum > 1.0f)
            {
                const float inv_sum = 1.0f / sum;
                for (uint8_t i = 0U; i < 7U; ++i)
                {
                    registration[i] *= inv_sum;
                }
            }
            inst->osc_bank.SetAmplitudes(registration);
            inst->osc_bank.SetGain(brick6_daisy_clamp01(inst->param[7]));
            break;
        }
        case BRICK6_DAISY_MODEL_HARMONIC:
        {
            float amps[16];
            float sum = 0.0f;
            inst->harmonic.SetFirstHarmIdx((int)(1.0f + (brick6_daisy_clamp01(inst->param[0]) * 15.0f) + 0.5f));
            for (uint8_t i = 0U; i < 14U; ++i)
            {
                amps[i] = brick6_daisy_clamp01(inst->param[(uint8_t)(i + 1U)]);
                sum += amps[i];
            }
            amps[14] = 0.0f;
            amps[15] = 0.0f;
            if (sum > 1.0f)
            {
                const float inv_sum = 1.0f / sum;
                for (uint8_t i = 0U; i < 14U; ++i)
                {
                    amps[i] *= inv_sum;
                }
            }
            inst->harmonic.SetAmplitudes(amps);
            break;
        }
        default:
            break;
    }
}

static void brick6_daisy_runtime_configure_defaults(brick6_daisy_runtime_instance_t *inst)
{
    static const float kOscBankRegistration[7] = {
        0.20f, 0.20f, 0.16f, 0.16f, 0.12f, 0.10f, 0.06f
    };
    static const float kHarmonicAmps[16] = {
        0.44f, 0.18f, 0.12f, 0.08f, 0.06f, 0.04f, 0.03f, 0.02f,
        0.015f, 0.012f, 0.009f, 0.007f, 0.005f, 0.004f, 0.003f, 0.002f
    };

    inst->osc.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    inst->osc.SetPw(0.5f);
    inst->osc.SetAmp(0.60f);

    inst->var_saw.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->var_saw.SetWaveshape(0.0f);
    inst->var_saw.SetPW(0.5f);

    inst->var_shape.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->var_shape.SetWaveshape(0.0f);
    inst->var_shape.SetPW(0.5f);
    inst->var_shape.SetSync(false);

    inst->fm2.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->fm2.SetRatio(2.0f);
    inst->fm2.SetIndex(0.0f);

    inst->formant.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->formant.SetPhaseShift(0.0f);

    inst->vosim.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->vosim.SetShape(0.0f);

    inst->z_osc.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->z_osc.SetShape(0.5f);
    inst->z_osc.SetMode(0.0f);

    inst->osc_bank.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->osc_bank.SetAmplitudes(kOscBankRegistration);
    inst->osc_bank.SetGain(0.75f);

    inst->harmonic.Init(BRICK6_DAISY_SAMPLE_RATE);
    inst->harmonic.SetFirstHarmIdx(1);
    inst->harmonic.SetAmplitudes(kHarmonicAmps);

    brick6_daisy_runtime_apply_frequency(inst);
    brick6_daisy_runtime_apply_params(inst);
}

void brick6_daisy_runtime_reset_instance(uint8_t instance_id)
{
    if (brick6_daisy_runtime_instance_valid(instance_id) == 0U)
    {
        return;
    }

    brick6_daisy_runtime_instance_t *const inst = &g_daisy_instances[instance_id];
    inst->initialized = 0U;
    inst->gate = 0U;
    inst->active_note = 60U;
    inst->velocity = 0.0f;
    inst->frequency_hz = brick6_daisy_mtof((float)inst->active_note);
    inst->model = BRICK6_DAISY_MODEL_OSC;
    for (uint8_t i = 0U; i < BRICK6_DAISY_PARAM_COUNT; ++i)
    {
        inst->param[i] = 0.5f;
    }
    inst->param[0] = 0.0f;
    brick6_daisy_runtime_configure_defaults(inst);
    inst->initialized = 1U;
}

void brick6_daisy_runtime_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)BRICK6_DAISY_MAX_INSTANCES; ++i)
    {
        brick6_daisy_runtime_reset_instance(i);
    }
}

void brick6_daisy_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity)
{
    if (brick6_daisy_runtime_instance_valid(instance_id) == 0U)
    {
        return;
    }

    brick6_daisy_runtime_instance_t *const inst = &g_daisy_instances[instance_id];
    if (inst->initialized == 0U)
    {
        brick6_daisy_runtime_reset_instance(instance_id);
    }

    inst->active_note = note;
    inst->velocity = ((float)velocity) * (1.0f / 127.0f);
    inst->frequency_hz = brick6_daisy_mtof((float)note);
    inst->gate = 1U;
    brick6_daisy_runtime_apply_frequency(inst);
    brick6_daisy_runtime_apply_params(inst);
}

void brick6_daisy_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    if (brick6_daisy_runtime_instance_valid(instance_id) == 0U)
    {
        return;
    }

    brick6_daisy_runtime_instance_t *const inst = &g_daisy_instances[instance_id];
    if (inst->active_note == note)
    {
        inst->gate = 0U;
        inst->velocity = 0.0f;
    }
}

void brick6_daisy_runtime_all_notes_off(uint8_t instance_id)
{
    if (brick6_daisy_runtime_instance_valid(instance_id) == 0U)
    {
        return;
    }

    g_daisy_instances[instance_id].gate = 0U;
    g_daisy_instances[instance_id].velocity = 0.0f;
}

void brick6_daisy_runtime_set_model(uint8_t instance_id, brick6_daisy_model_t model)
{
    if (brick6_daisy_runtime_instance_valid(instance_id) == 0U)
    {
        return;
    }

    if (model >= BRICK6_DAISY_MODEL_COUNT)
    {
        model = BRICK6_DAISY_MODEL_OSC;
    }

    brick6_daisy_runtime_instance_t *const inst = &g_daisy_instances[instance_id];
    inst->model = model;
    brick6_daisy_runtime_apply_params(inst);
}

void brick6_daisy_runtime_set_param(uint8_t instance_id, uint8_t param_index, float value)
{
    if ((brick6_daisy_runtime_instance_valid(instance_id) == 0U)
            || (param_index >= BRICK6_DAISY_PARAM_COUNT))
    {
        return;
    }

    brick6_daisy_runtime_instance_t *const inst = &g_daisy_instances[instance_id];
    inst->param[param_index] = brick6_daisy_clamp01(value);
    brick6_daisy_runtime_apply_params(inst);
}

uint8_t brick6_daisy_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    if ((out_mono == NULL) || (frames == 0U) || (brick6_daisy_runtime_instance_valid(instance_id) == 0U))
    {
        return 0U;
    }

    brick6_daisy_runtime_instance_t *const inst = &g_daisy_instances[instance_id];
    if ((inst->initialized == 0U) || (inst->gate == 0U) || (inst->velocity <= 0.0f))
    {
        return 0U;
    }

    const float level = inst->velocity * 0.45f;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        float sample = 0.0f;
        switch (inst->model)
        {
            case BRICK6_DAISY_MODEL_VAR_SAW:
                sample = inst->var_saw.Process();
                break;
            case BRICK6_DAISY_MODEL_VAR_SHAPE:
                sample = inst->var_shape.Process();
                break;
            case BRICK6_DAISY_MODEL_FM2:
                sample = inst->fm2.Process();
                break;
            case BRICK6_DAISY_MODEL_FORMANT:
                sample = inst->formant.Process();
                break;
            case BRICK6_DAISY_MODEL_VOSIM:
                sample = inst->vosim.Process();
                break;
            case BRICK6_DAISY_MODEL_Z_OSC:
                sample = inst->z_osc.Process();
                break;
            case BRICK6_DAISY_MODEL_OSC_BANK:
                sample = inst->osc_bank.Process();
                break;
            case BRICK6_DAISY_MODEL_HARMONIC:
                sample = inst->harmonic.Process();
                break;
            case BRICK6_DAISY_MODEL_OSC:
            default:
                sample = inst->osc.Process();
                break;
        }
        out_mono[i] = sample * level;
    }

    return 1U;
}
