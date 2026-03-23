#include "Audio/monob_synth.h"

#include <math.h>
#include <string.h>

#include "Audio/monob_moog_ladder.h"
#include "Audio/monob_osc_bank.h"

#define MONOB_SYNTH_MIN_FREQ_HZ 20.0f
#define MONOB_SYNTH_MAX_FREQ_HZ 16000.0f
#define MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ 16000.0f
#define MONOB_SYNTH_OSC_GAIN 0.22f
#define MONOB_SYNTH_FILTER_MAX_RESONANCE 1.8f
#define MONOB_SYNTH_AMP_ATTACK_S 0.005f
#define MONOB_SYNTH_AMP_RELEASE_S 0.03f
#define MONOB_SYNTH_FILTER_UPDATE_PERIOD 8U
#define MONOB_SYNTH_ENV_EPSILON 0.0001f

typedef enum
{
    MONOB_ENV_IDLE = 0,
    MONOB_ENV_DELAY,
    MONOB_ENV_ATTACK,
    MONOB_ENV_DECAY,
    MONOB_ENV_SUSTAIN,
    MONOB_ENV_RELEASE
} monob_env_stage_t;

typedef struct
{
    float sample_rate;
    float amp_env;
    float base_frequency_hz;
    float filter_env;
    float filter_cutoff_hz;
    float filter_resonance;
    float filter_eg_amount;
    float filter_attack_s;
    float filter_decay_s;
    float filter_sustain;
    float filter_release_s;
    float filter_keytrack;
    float filter_env_delay_s;
    float filter_env_delay_remaining_s;
    uint8_t filter_enabled;
    uint8_t filter_env_reset;
    uint8_t note_active;
    uint8_t current_note;
    monob_env_stage_t filter_env_stage;
} monob_synth_state_t;

static monob_synth_state_t g_monob_synth;

static float monob_synth_clampf(float value, float min_value, float max_value)
{
    if(value < min_value)
    {
        return min_value;
    }

    if(value > max_value)
    {
        return max_value;
    }

    return value;
}

static float monob_synth_midi_note_to_hz(uint8_t midi_note)
{
    return 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
}

static float monob_synth_filter_keytrack_multiplier(void)
{
    const float semitone_delta = (float)((int32_t)g_monob_synth.current_note - 60);
    return powf(2.0f, (semitone_delta * g_monob_synth.filter_keytrack) / 12.0f);
}

static float monob_synth_step_linear(float current, float target, float time_s)
{
    if(time_s <= 0.0f)
    {
        return target;
    }

    const float max_delta = 1.0f / (time_s * g_monob_synth.sample_rate);
    if(current < target)
    {
        current += max_delta;
        return (current > target) ? target : current;
    }

    current -= max_delta;
    return (current < target) ? target : current;
}

static float monob_synth_step_with_delta(float current, float target, float delta)
{
    if(current < target)
    {
        current += delta;
        return (current > target) ? target : current;
    }

    current -= delta;
    return (current < target) ? target : current;
}

static float monob_synth_process_filter_env(void)
{
    switch(g_monob_synth.filter_env_stage)
    {
        case MONOB_ENV_DELAY:
            if(g_monob_synth.filter_env_delay_remaining_s > 0.0f)
            {
                g_monob_synth.filter_env_delay_remaining_s -= (1.0f / g_monob_synth.sample_rate);
            }

            if(g_monob_synth.filter_env_delay_remaining_s <= 0.0f)
            {
                g_monob_synth.filter_env_delay_remaining_s = 0.0f;
                g_monob_synth.filter_env_stage = MONOB_ENV_ATTACK;
            }
            break;

        case MONOB_ENV_ATTACK:
            g_monob_synth.filter_env = monob_synth_step_linear(g_monob_synth.filter_env, 1.0f, g_monob_synth.filter_attack_s);
            if(g_monob_synth.filter_env >= 0.9999f)
            {
                g_monob_synth.filter_env = 1.0f;
                g_monob_synth.filter_env_stage = MONOB_ENV_DECAY;
            }
            break;

        case MONOB_ENV_DECAY:
            g_monob_synth.filter_env = monob_synth_step_linear(g_monob_synth.filter_env,
                                                               g_monob_synth.filter_sustain,
                                                               g_monob_synth.filter_decay_s);
            if(fabsf(g_monob_synth.filter_env - g_monob_synth.filter_sustain) <= 0.0005f)
            {
                g_monob_synth.filter_env = g_monob_synth.filter_sustain;
                g_monob_synth.filter_env_stage = MONOB_ENV_SUSTAIN;
            }
            break;

        case MONOB_ENV_SUSTAIN:
            g_monob_synth.filter_env = g_monob_synth.filter_sustain;
            break;

        case MONOB_ENV_RELEASE:
            g_monob_synth.filter_env = monob_synth_step_linear(g_monob_synth.filter_env, 0.0f, g_monob_synth.filter_release_s);
            if(g_monob_synth.filter_env <= MONOB_SYNTH_ENV_EPSILON)
            {
                g_monob_synth.filter_env = 0.0f;
                g_monob_synth.filter_env_stage = MONOB_ENV_IDLE;
            }
            break;

        case MONOB_ENV_IDLE:
        default:
            g_monob_synth.filter_env = 0.0f;
            break;
    }

    return g_monob_synth.filter_env;
}

static float monob_synth_compute_modulated_cutoff(float env)
{
    float cutoff_hz = g_monob_synth.filter_cutoff_hz * monob_synth_filter_keytrack_multiplier();
    cutoff_hz = monob_synth_clampf(cutoff_hz, MONOB_SYNTH_MIN_FREQ_HZ, MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ);

    if(g_monob_synth.filter_eg_amount >= 0.0f)
    {
        cutoff_hz += (MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ - cutoff_hz) * g_monob_synth.filter_eg_amount * env;
    }
    else
    {
        cutoff_hz += (cutoff_hz - MONOB_SYNTH_MIN_FREQ_HZ) * g_monob_synth.filter_eg_amount * env;
    }

    return monob_synth_clampf(cutoff_hz, MONOB_SYNTH_MIN_FREQ_HZ, MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ);
}

void monob_synth_init(float sample_rate)
{
    (void)memset(&g_monob_synth, 0, sizeof(g_monob_synth));
    g_monob_synth.sample_rate = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    g_monob_synth.current_note = 60U;
    g_monob_synth.filter_cutoff_hz = 16000.0f;
    g_monob_synth.filter_attack_s = 0.01f;
    g_monob_synth.filter_decay_s = 0.10f;
    g_monob_synth.filter_sustain = 1.0f;
    g_monob_synth.filter_release_s = 0.10f;
    g_monob_synth.filter_env_reset = 1U;
    monob_osc_bank_init(g_monob_synth.sample_rate);
    monob_moog_ladder_init(g_monob_synth.sample_rate);
    monob_moog_ladder_set_cutoff(g_monob_synth.filter_cutoff_hz);
    monob_moog_ladder_set_resonance(g_monob_synth.filter_resonance * MONOB_SYNTH_FILTER_MAX_RESONANCE);
}

void monob_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;
    g_monob_synth.current_note = midi_note;
    g_monob_synth.base_frequency_hz = monob_synth_midi_note_to_hz(midi_note);
    g_monob_synth.note_active = 1U;
    monob_osc_bank_note_on();
    if(g_monob_synth.filter_env_reset != 0U)
    {
        g_monob_synth.filter_env = 0.0f;
    }
    g_monob_synth.filter_env_delay_remaining_s = g_monob_synth.filter_env_delay_s;
    g_monob_synth.filter_env_stage = (g_monob_synth.filter_env_delay_s > 0.0f) ? MONOB_ENV_DELAY : MONOB_ENV_ATTACK;
}

void monob_synth_note_off(uint8_t midi_note)
{
    if((g_monob_synth.note_active == 0U) || (midi_note != g_monob_synth.current_note))
    {
        return;
    }

    g_monob_synth.note_active = 0U;
    g_monob_synth.filter_env_stage = MONOB_ENV_RELEASE;
}

void monob_synth_all_notes_off(void)
{
    g_monob_synth.note_active = 0U;
    g_monob_synth.amp_env = 0.0f;
    g_monob_synth.filter_env = 0.0f;
    g_monob_synth.filter_env_stage = MONOB_ENV_IDLE;
    g_monob_synth.filter_env_delay_remaining_s = 0.0f;
    g_monob_synth.base_frequency_hz = 0.0f;
    monob_osc_bank_reset();
    monob_moog_ladder_reset();
}

void monob_synth_process_block(float *mono_out, uint32_t frames)
{
    if(mono_out == NULL)
    {
        return;
    }

    if((g_monob_synth.note_active == 0U)
    && (g_monob_synth.amp_env <= 0.0f)
    && (g_monob_synth.base_frequency_hz <= 0.0f)
    && (g_monob_synth.filter_enabled == 0U))
    {
        (void)memset(mono_out, 0, frames * sizeof(float));
        return;
    }

    const float amp_target = (g_monob_synth.note_active != 0U) ? 1.0f : 0.0f;
    const float amp_time_s = (g_monob_synth.note_active != 0U) ? MONOB_SYNTH_AMP_ATTACK_S : MONOB_SYNTH_AMP_RELEASE_S;
    const float amp_delta = (amp_time_s > 0.0f) ? (1.0f / (amp_time_s * g_monob_synth.sample_rate)) : 1.0f;

    uint32_t filter_update_countdown = 0U;

    for(uint32_t i = 0U; i < frames; ++i)
    {
        g_monob_synth.amp_env = monob_synth_step_with_delta(g_monob_synth.amp_env, amp_target, amp_delta);

        if((g_monob_synth.note_active == 0U) && (g_monob_synth.amp_env <= MONOB_SYNTH_ENV_EPSILON))
        {
            g_monob_synth.amp_env = 0.0f;
            g_monob_synth.base_frequency_hz = 0.0f;
        }

        float sample = 0.0f;
        if((g_monob_synth.base_frequency_hz > 0.0f) && (g_monob_synth.amp_env > 0.0f))
        {
            sample = monob_osc_bank_process(g_monob_synth.base_frequency_hz)
                   * MONOB_SYNTH_OSC_GAIN
                   * g_monob_synth.amp_env;
        }

        if(g_monob_synth.filter_enabled != 0U)
        {
            const float env = monob_synth_process_filter_env();

            if(filter_update_countdown == 0U)
            {
                monob_moog_ladder_set_cutoff(monob_synth_compute_modulated_cutoff(env));
                filter_update_countdown = MONOB_SYNTH_FILTER_UPDATE_PERIOD - 1U;
            }
            else
            {
                --filter_update_countdown;
            }

            sample = monob_moog_ladder_process_sample(sample);
        }

        mono_out[i] = sample;
    }
}

void monob_synth_set_filter_type(uint8_t enabled)
{
    g_monob_synth.filter_enabled = (enabled != 0U) ? 1U : 0U;
    if(g_monob_synth.filter_enabled == 0U)
    {
        g_monob_synth.filter_env = 0.0f;
        g_monob_synth.filter_env_stage = MONOB_ENV_IDLE;
        g_monob_synth.filter_env_delay_remaining_s = 0.0f;
        monob_moog_ladder_reset();
    }
}

void monob_synth_set_filter_cutoff(float cutoff_hz)
{
    g_monob_synth.filter_cutoff_hz = monob_synth_clampf(cutoff_hz, MONOB_SYNTH_MIN_FREQ_HZ, MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ);
    monob_moog_ladder_set_cutoff(g_monob_synth.filter_cutoff_hz);
}

void monob_synth_set_filter_resonance(float resonance)
{
    g_monob_synth.filter_resonance = monob_synth_clampf(resonance, 0.0f, 1.0f);
    monob_moog_ladder_set_resonance(g_monob_synth.filter_resonance * MONOB_SYNTH_FILTER_MAX_RESONANCE);
}

void monob_synth_set_filter_eg_amount(float eg_amount)
{
    g_monob_synth.filter_eg_amount = monob_synth_clampf(eg_amount, 0.0f, 1.0f);
}

void monob_synth_set_filter_attack(float attack_s)
{
    g_monob_synth.filter_attack_s = monob_synth_clampf(attack_s, 0.001f, 5.0f);
}

void monob_synth_set_filter_decay(float decay_s)
{
    g_monob_synth.filter_decay_s = monob_synth_clampf(decay_s, 0.001f, 5.0f);
}

void monob_synth_set_filter_sustain(float sustain)
{
    g_monob_synth.filter_sustain = monob_synth_clampf(sustain, 0.0f, 1.0f);
}

void monob_synth_set_filter_release(float release_s)
{
    g_monob_synth.filter_release_s = monob_synth_clampf(release_s, 0.001f, 5.0f);
}

void monob_synth_set_filter_keytrack(float amount)
{
    g_monob_synth.filter_keytrack = monob_synth_clampf(amount, 0.0f, 1.0f);
}

void monob_synth_set_filter_env_reset(uint8_t enabled)
{
    g_monob_synth.filter_env_reset = (enabled != 0U) ? 1U : 0U;
}

void monob_synth_set_filter_env_delay(float delay_s)
{
    g_monob_synth.filter_env_delay_s = monob_synth_clampf(delay_s, 0.0f, 5.0f);
    if(g_monob_synth.filter_env_stage != MONOB_ENV_DELAY)
    {
        return;
    }

    g_monob_synth.filter_env_delay_remaining_s = g_monob_synth.filter_env_delay_s;
    if(g_monob_synth.filter_env_delay_s <= 0.0f)
    {
        g_monob_synth.filter_env_stage = MONOB_ENV_ATTACK;
    }
}

void monob_synth_set_osc_wave(uint8_t osc_index, uint8_t wave)
{
    monob_osc_bank_set_wave(osc_index, wave);
}

void monob_synth_set_osc_range(uint8_t osc_index, int8_t octave)
{
    if(osc_index >= 3U)
    {
        return;
    }

    monob_osc_bank_set_octave(osc_index, octave);
}

void monob_synth_set_sub_octave(int8_t octave)
{
    monob_osc_bank_set_octave(3U, octave);
}

void monob_synth_set_osc_detune(uint8_t osc_index, float detune_cents)
{
    monob_osc_bank_set_detune(osc_index, detune_cents);
}

void monob_synth_set_osc_mix(uint8_t osc_index, float mix)
{
    if(osc_index >= 3U)
    {
        return;
    }

    monob_osc_bank_set_mix(osc_index, mix);
}

void monob_synth_set_sub_mix(float mix)
{
    monob_osc_bank_set_mix(3U, mix);
}
