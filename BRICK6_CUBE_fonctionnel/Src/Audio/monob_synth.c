#include "Audio/monob_synth.h"

#include <math.h>
#include <string.h>

#include "Audio/fx_filter_ladder_moog.h"

#define MONOB_SYNTH_MIN_FREQ_HZ 20.0f
#define MONOB_SYNTH_MAX_FREQ_HZ 16000.0f
#define MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ 16000.0f
#define MONOB_SYNTH_OSC_GAIN 0.18f
#define MONOB_SYNTH_AMP_ATTACK_S 0.005f
#define MONOB_SYNTH_AMP_RELEASE_S 0.03f

typedef enum
{
    MONOB_ENV_IDLE = 0,
    MONOB_ENV_ATTACK,
    MONOB_ENV_DECAY,
    MONOB_ENV_SUSTAIN,
    MONOB_ENV_RELEASE
} monob_env_stage_t;

typedef struct
{
    float sample_rate;
    float phase;
    float phase_increment;
    float amp_env;
    float filter_env;
    float filter_cutoff_hz;
    float filter_resonance;
    float filter_eg_amount;
    float filter_attack_s;
    float filter_decay_s;
    float filter_sustain;
    float filter_release_s;
    uint8_t filter_enabled;
    uint8_t note_active;
    uint8_t current_note;
    monob_env_stage_t filter_env_stage;
    fx_filter_ladder_moog_t ladder;
} monob_synth_state_t;

static monob_synth_state_t g_monob_synth;

static float monob_synth_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

static float monob_synth_midi_note_to_hz(uint8_t midi_note)
{
    return 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
}

static float monob_synth_step_linear(float current, float target, float time_s)
{
    if (time_s <= 0.0f)
    {
        return target;
    }

    const float max_delta = 1.0f / (time_s * g_monob_synth.sample_rate);
    if (current < target)
    {
        current += max_delta;
        return (current > target) ? target : current;
    }

    current -= max_delta;
    return (current < target) ? target : current;
}

static float monob_synth_process_filter_env(void)
{
    switch (g_monob_synth.filter_env_stage)
    {
        case MONOB_ENV_ATTACK:
            g_monob_synth.filter_env = monob_synth_step_linear(g_monob_synth.filter_env, 1.0f, g_monob_synth.filter_attack_s);
            if (g_monob_synth.filter_env >= 0.9999f)
            {
                g_monob_synth.filter_env = 1.0f;
                g_monob_synth.filter_env_stage = MONOB_ENV_DECAY;
            }
            break;

        case MONOB_ENV_DECAY:
            g_monob_synth.filter_env = monob_synth_step_linear(g_monob_synth.filter_env,
                                                               g_monob_synth.filter_sustain,
                                                               g_monob_synth.filter_decay_s);
            if (fabsf(g_monob_synth.filter_env - g_monob_synth.filter_sustain) <= 0.0005f)
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
            if (g_monob_synth.filter_env <= 0.0001f)
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

void monob_synth_init(float sample_rate)
{
    (void)memset(&g_monob_synth, 0, sizeof(g_monob_synth));
    g_monob_synth.sample_rate = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    g_monob_synth.filter_cutoff_hz = 16000.0f;
    g_monob_synth.filter_attack_s = 0.01f;
    g_monob_synth.filter_decay_s = 0.10f;
    g_monob_synth.filter_sustain = 1.0f;
    g_monob_synth.filter_release_s = 0.10f;
    fx_filter_ladder_moog_init(&g_monob_synth.ladder, g_monob_synth.sample_rate);
    fx_filter_ladder_moog_set_drive(&g_monob_synth.ladder, 1.0f);
}

void monob_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;
    g_monob_synth.current_note = midi_note;
    g_monob_synth.phase_increment = monob_synth_midi_note_to_hz(midi_note) / g_monob_synth.sample_rate;
    g_monob_synth.note_active = 1U;
    g_monob_synth.filter_env = 0.0f;
    g_monob_synth.filter_env_stage = MONOB_ENV_ATTACK;
}

void monob_synth_note_off(uint8_t midi_note)
{
    if ((g_monob_synth.note_active == 0U) || (midi_note != g_monob_synth.current_note))
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
    fx_filter_ladder_moog_reset(&g_monob_synth.ladder);
}

void monob_synth_process_block(float *mono_out, uint32_t frames)
{
    if (mono_out == NULL)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float amp_target = (g_monob_synth.note_active != 0U) ? 1.0f : 0.0f;
        const float amp_time_s = (g_monob_synth.note_active != 0U) ? MONOB_SYNTH_AMP_ATTACK_S : MONOB_SYNTH_AMP_RELEASE_S;
        g_monob_synth.amp_env = monob_synth_step_linear(g_monob_synth.amp_env, amp_target, amp_time_s);

        g_monob_synth.phase += g_monob_synth.phase_increment;
        if (g_monob_synth.phase >= 1.0f)
        {
            g_monob_synth.phase -= floorf(g_monob_synth.phase);
        }

        float sample = ((g_monob_synth.phase * 2.0f) - 1.0f) * MONOB_SYNTH_OSC_GAIN * g_monob_synth.amp_env;

        if (g_monob_synth.filter_enabled != 0U)
        {
            const float env = monob_synth_process_filter_env();
            const float modulated_cutoff_hz = g_monob_synth.filter_cutoff_hz
                                            + ((MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ - g_monob_synth.filter_cutoff_hz)
                                               * g_monob_synth.filter_eg_amount
                                               * env);
            fx_filter_ladder_moog_set_cutoff(&g_monob_synth.ladder, modulated_cutoff_hz);
            sample = fx_filter_ladder_moog_process_sample(&g_monob_synth.ladder, sample);
        }

        mono_out[i] = sample;
    }
}

void monob_synth_set_filter_type(uint8_t enabled)
{
    g_monob_synth.filter_enabled = (enabled != 0U) ? 1U : 0U;
    if (g_monob_synth.filter_enabled == 0U)
    {
        g_monob_synth.filter_env = 0.0f;
        g_monob_synth.filter_env_stage = MONOB_ENV_IDLE;
        fx_filter_ladder_moog_reset(&g_monob_synth.ladder);
    }
}

void monob_synth_set_filter_cutoff(float cutoff_hz)
{
    g_monob_synth.filter_cutoff_hz = monob_synth_clampf(cutoff_hz, MONOB_SYNTH_MIN_FREQ_HZ, MONOB_SYNTH_MAX_FILTER_CUTOFF_HZ);
    fx_filter_ladder_moog_set_cutoff(&g_monob_synth.ladder, g_monob_synth.filter_cutoff_hz);
}

void monob_synth_set_filter_resonance(float resonance)
{
    g_monob_synth.filter_resonance = monob_synth_clampf(resonance, 0.0f, 1.0f);
    fx_filter_ladder_moog_set_resonance(&g_monob_synth.ladder, g_monob_synth.filter_resonance * 4.0f);
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
