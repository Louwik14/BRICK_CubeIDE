#include "Audio/tb3_synth.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

namespace
{
constexpr uint8_t TB3_SYNTH_MAX_INSTANCES = 8U;
constexpr uint8_t TB3_PARAM_COUNT = 8U;
constexpr float TB3_MIN_SAMPLE_RATE = 1000.0f;
constexpr float TB3_DEFAULT_SAMPLE_RATE = 48000.0f;
constexpr float PI_F = 3.14159265358979323846f;

struct tb3_synth_instance_t
{
    uint8_t note_active;
    uint8_t current_note;
    uint8_t velocity;
    float params[TB3_PARAM_COUNT];

    float phase;
    float phase_inc;
    float target_phase_inc;
    float amp_env;
    float filter_lp;
    float filter_bp;
};

static tb3_synth_instance_t g_tb3_instances[TB3_SYNTH_MAX_INSTANCES];
static float g_tb3_sample_rate = TB3_DEFAULT_SAMPLE_RATE;

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint8_t tb3_synth_instance_valid(uint8_t instance_id)
{
    return (instance_id < TB3_SYNTH_MAX_INSTANCES) ? 1U : 0U;
}

static uint8_t tb3_synth_param_index(param_id_t param_id)
{
    switch (param_id)
    {
        case PARAM_TB3_WAVEFORM:
            return 0U;
        case PARAM_TB3_CUTOFF:
            return 1U;
        case PARAM_TB3_RESONANCE:
            return 2U;
        case PARAM_TB3_ENV_MOD:
            return 3U;
        case PARAM_TB3_DECAY:
            return 4U;
        case PARAM_TB3_ACCENT:
            return 5U;
        case PARAM_TB3_VOLUME:
            return 6U;
        case PARAM_TB3_SLIDE_TIME:
            return 7U;
        default:
            return TB3_PARAM_COUNT;
    }
}

static inline float tb3_midi_note_to_hz(uint8_t midi_note)
{
    const float semitone = ((float)midi_note - 69.0f) / 12.0f;
    return 440.0f * powf(2.0f, semitone);
}

static void tb3_instance_set_defaults(tb3_synth_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    instance->params[0] = 0.0f;   // waveform
    instance->params[1] = 100.0f; // cutoff
    instance->params[2] = 10.0f;  // resonance
    instance->params[3] = 40.0f;  // env mod
    instance->params[4] = 64.0f;  // decay
    instance->params[5] = 0.0f;   // accent
    instance->params[6] = 100.0f; // volume
    instance->params[7] = 0.0f;   // slide
}

static void tb3_instance_update_pitch_target(tb3_synth_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    const float freq = tb3_midi_note_to_hz(instance->current_note);
    instance->target_phase_inc = freq / g_tb3_sample_rate;
}
}

extern "C" {

void tb3_synth_init(float sample_rate)
{
    g_tb3_sample_rate = (sample_rate > TB3_MIN_SAMPLE_RATE) ? sample_rate : TB3_DEFAULT_SAMPLE_RATE;
    (void)memset(g_tb3_instances, 0, sizeof(g_tb3_instances));

    for (uint8_t instance = 0U; instance < TB3_SYNTH_MAX_INSTANCES; ++instance)
    {
        tb3_instance_set_defaults(&g_tb3_instances[instance]);
    }
}

uint8_t tb3_synth_instance_count(void)
{
    return TB3_SYNTH_MAX_INSTANCES;
}

void tb3_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    tb3_synth_instance_t *const instance = &g_tb3_instances[instance_id];
    instance->note_active = 1U;
    instance->current_note = midi_note;
    instance->velocity = velocity;
    tb3_instance_update_pitch_target(instance);

    if (instance->phase_inc <= 0.0f)
    {
        instance->phase_inc = instance->target_phase_inc;
    }
}

void tb3_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    if (g_tb3_instances[instance_id].current_note != midi_note)
    {
        return;
    }

    g_tb3_instances[instance_id].note_active = 0U;
}

void tb3_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    g_tb3_instances[instance_id].note_active = 0U;
    g_tb3_instances[instance_id].velocity = 0U;
}

void tb3_synth_all_notes_off_all(void)
{
    for (uint8_t instance = 0U; instance < TB3_SYNTH_MAX_INSTANCES; ++instance)
    {
        tb3_synth_all_notes_off_for_instance(instance);
    }
}

void tb3_synth_set_param_for_instance(uint8_t instance_id, param_id_t param_id, float value)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    const uint8_t param_index = tb3_synth_param_index(param_id);
    if (param_index >= TB3_PARAM_COUNT)
    {
        return;
    }

    g_tb3_instances[instance_id].params[param_index] = value;
}

void tb3_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames)
{
    if (mono_out == NULL)
    {
        return;
    }

    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        (void)memset(mono_out, 0, sizeof(float) * frames);
        return;
    }

    tb3_synth_instance_t *const instance = &g_tb3_instances[instance_id];

    const float waveform = clampf(instance->params[0], 0.0f, 1.0f);
    const float cutoff_norm = clampf(instance->params[1] / 127.0f, 0.0f, 1.0f);
    const float resonance_norm = clampf(instance->params[2] / 127.0f, 0.0f, 1.0f);
    const float envmod_norm = clampf(instance->params[3] / 127.0f, 0.0f, 1.0f);
    const float decay_norm = clampf(instance->params[4] / 127.0f, 0.0f, 1.0f);
    const float accent_norm = clampf(instance->params[5] / 127.0f, 0.0f, 1.0f);
    const float volume = clampf(instance->params[6] / 127.0f, 0.0f, 1.0f);
    const float slide_norm = clampf(instance->params[7] / 127.0f, 0.0f, 1.0f);

    const float slide_time_ms = 2.0f + (slide_norm * 200.0f);
    const float slide_coeff = expf(-1.0f / ((slide_time_ms * 0.001f * g_tb3_sample_rate) + 1.0f));

    const float attack_coeff = expf(-1.0f / (0.003f * g_tb3_sample_rate));
    const float decay_ms = 20.0f + (decay_norm * 1200.0f);
    const float release_coeff = expf(-1.0f / ((decay_ms * 0.001f * g_tb3_sample_rate) + 1.0f));
    const float env_target = (instance->note_active != 0U) ? ((float)instance->velocity / 127.0f) : 0.0f;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        instance->phase_inc = (slide_coeff * instance->phase_inc) + ((1.0f - slide_coeff) * instance->target_phase_inc);
        instance->phase += instance->phase_inc;
        if (instance->phase >= 1.0f)
        {
            instance->phase -= floorf(instance->phase);
        }

        if (instance->note_active != 0U)
        {
            instance->amp_env = (attack_coeff * instance->amp_env) + ((1.0f - attack_coeff) * env_target);
        }
        else
        {
            instance->amp_env *= release_coeff;
        }

        const float saw = (2.0f * instance->phase) - 1.0f;
        const float square = (instance->phase < 0.5f) ? 1.0f : -1.0f;
        float osc = ((1.0f - waveform) * saw) + (waveform * square);

        const float cutoff_hz = 80.0f + (cutoff_norm * 7000.0f) + (envmod_norm * instance->amp_env * 5000.0f);
        const float f = clampf(2.0f * sinf((PI_F * cutoff_hz) / g_tb3_sample_rate), 0.01f, 0.99f);
        const float q = 0.15f + (resonance_norm * 1.7f);

        const float hp = osc - instance->filter_lp - (q * instance->filter_bp);
        instance->filter_bp += f * hp;
        instance->filter_lp += f * instance->filter_bp;

        const float accent_gain = 1.0f + (accent_norm * 0.7f * instance->amp_env);
        osc = instance->filter_lp * instance->amp_env * accent_gain;

        mono_out[i] = osc * volume * 0.4f;
    }
}

} // extern "C"
