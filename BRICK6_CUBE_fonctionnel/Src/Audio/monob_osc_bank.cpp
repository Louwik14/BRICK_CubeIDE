#include "Audio/monob_osc_bank.h"

#include <math.h>

#include "Audio/oscillator.h"

using namespace daisysp;

namespace
{
constexpr uint8_t MONOB_OSC_COUNT = 4U;
constexpr uint8_t MONOB_MAIN_OSC_COUNT = 3U;
constexpr float MONOB_MAX_DRIFT_CENTS = 8.0f;
constexpr float MONOB_MIN_FREQ_HZ = 20.0f;
constexpr float MONOB_MAX_FREQ_HZ = 16000.0f;
constexpr uint32_t MONOB_DRIFT_UPDATE_PERIOD = 16U;
constexpr float MONOB_TWO_PI = 6.28318530718f;
constexpr float MONOB_FREQ_EPSILON_HZ = 0.001f;

enum MonobWave
{
    MONOB_WAVE_OFF = 0,
    MONOB_WAVE_SINE,
    MONOB_WAVE_SQUARE,
    MONOB_WAVE_TRI,
    MONOB_WAVE_SAW,
};

struct MonobOscSource
{
    Oscillator osc;
    uint8_t wave;
    int8_t octave;
    float octave_mul;
    float detune_cents;
    float detune_mul;
    float mix;
    float drift_phase;
    float drift_rate_hz;
    float drift_value_cents;
    uint32_t drift_counter;
    float current_freq_hz;
};

struct MonobOscBank
{
    MonobOscSource sources[MONOB_OSC_COUNT];
    float sample_rate;
};

MonobOscBank g_bank;

float clampf_local(float value, float min_value, float max_value)
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

uint8_t map_waveform(uint8_t wave)
{
    switch(wave)
    {
        case MONOB_WAVE_SINE: return Oscillator::WAVE_SIN;
        case MONOB_WAVE_SQUARE: return Oscillator::WAVE_POLYBLEP_SQUARE;
        case MONOB_WAVE_TRI: return Oscillator::WAVE_TRI;
        case MONOB_WAVE_SAW: return Oscillator::WAVE_POLYBLEP_SAW;
        default: return Oscillator::WAVE_SIN;
    }
}

float octave_to_mul(int8_t octave)
{
    return powf(2.0f, (float)octave);
}

float cents_to_mul(float cents)
{
    return powf(2.0f, cents / 1200.0f);
}

float triangle_lfo(float phase)
{
    const float x = phase - floorf(phase);
    if(x < 0.25f)
    {
        return x * 4.0f;
    }
    if(x < 0.75f)
    {
        return 2.0f - (x * 4.0f);
    }
    return (x * 4.0f) - 4.0f;
}

void refresh_drift(MonobOscSource &source, float sample_rate, float drift_depth_cents)
{
    source.drift_phase += (source.drift_rate_hz * (float)MONOB_DRIFT_UPDATE_PERIOD) / sample_rate;
    if(source.drift_phase >= 1.0f)
    {
        source.drift_phase -= floorf(source.drift_phase);
    }
    source.drift_value_cents = triangle_lfo(source.drift_phase) * drift_depth_cents;
    source.drift_counter = MONOB_DRIFT_UPDATE_PERIOD;
}
} // namespace

extern "C" void monob_osc_bank_init(float sample_rate)
{
    g_bank.sample_rate = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;

    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        MonobOscSource &source = g_bank.sources[i];
        source.osc.Init(g_bank.sample_rate);
        source.osc.SetAmp(1.0f);
        source.osc.SetPw(0.5f);
        source.osc.SetWaveform(Oscillator::WAVE_SIN);
        source.wave = MONOB_WAVE_OFF;
        source.octave = (i == 3U) ? -1 : 0;
        source.octave_mul = octave_to_mul(source.octave);
        source.detune_cents = 0.0f;
        source.detune_mul = 1.0f;
        source.mix = 0.0f;
        source.drift_phase = 0.0f;
        source.drift_rate_hz = 0.11f + (0.07f * (float)i);
        source.drift_value_cents = 0.0f;
        source.drift_counter = 0U;
        source.current_freq_hz = -1.0f;
    }
}

extern "C" void monob_osc_bank_reset(void)
{
    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        g_bank.sources[i].osc.Reset();
        g_bank.sources[i].drift_phase = 0.0f;
        g_bank.sources[i].drift_value_cents = 0.0f;
        g_bank.sources[i].drift_counter = 0U;
        g_bank.sources[i].current_freq_hz = -1.0f;
    }
}

extern "C" void monob_osc_bank_note_on(void)
{
    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        g_bank.sources[i].osc.Reset();
        g_bank.sources[i].drift_counter = 0U;
        g_bank.sources[i].current_freq_hz = -1.0f;
    }
}

extern "C" void monob_osc_bank_set_wave(uint8_t osc_index, uint8_t wave)
{
    if(osc_index >= MONOB_OSC_COUNT)
    {
        return;
    }

    MonobOscSource &source = g_bank.sources[osc_index];
    source.wave = (wave <= MONOB_WAVE_SAW) ? wave : MONOB_WAVE_OFF;
    source.current_freq_hz = -1.0f;
    if(source.wave != MONOB_WAVE_OFF)
    {
        source.osc.SetWaveform(map_waveform(source.wave));
    }
}

extern "C" void monob_osc_bank_set_octave(uint8_t osc_index, int8_t octave)
{
    if(osc_index >= MONOB_OSC_COUNT)
    {
        return;
    }

    MonobOscSource &source = g_bank.sources[osc_index];
    source.octave = octave;
    source.octave_mul = octave_to_mul(octave);
    source.current_freq_hz = -1.0f;
}

extern "C" void monob_osc_bank_set_detune(uint8_t osc_index, float detune_cents)
{
    if(osc_index >= MONOB_MAIN_OSC_COUNT)
    {
        return;
    }

    MonobOscSource &source = g_bank.sources[osc_index];
    source.detune_cents = clampf_local(detune_cents, -24.0f, 24.0f);
    source.detune_mul = cents_to_mul(source.detune_cents);
    source.current_freq_hz = -1.0f;
}

extern "C" void monob_osc_bank_set_mix(uint8_t osc_index, float mix)
{
    if(osc_index >= MONOB_OSC_COUNT)
    {
        return;
    }

    g_bank.sources[osc_index].mix = clampf_local(mix, 0.0f, 1.0f);
}

extern "C" float monob_osc_bank_process(float base_frequency_hz, float drift_amount)
{
    float mixed = 0.0f;
    float mix_sum = 0.0f;
    const float clamped_base = clampf_local(base_frequency_hz, MONOB_MIN_FREQ_HZ, MONOB_MAX_FREQ_HZ);
    const float drift_depth_cents = clampf_local(drift_amount, 0.0f, 1.0f) * MONOB_MAX_DRIFT_CENTS;

    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        MonobOscSource &source = g_bank.sources[i];
        if((source.wave == MONOB_WAVE_OFF) || (source.mix <= 0.0f))
        {
            continue;
        }

        float detune_mul = source.detune_mul;
        if(i < MONOB_MAIN_OSC_COUNT)
        {
            if(source.drift_counter == 0U)
            {
                refresh_drift(source, g_bank.sample_rate, drift_depth_cents);
            }
            else
            {
                --source.drift_counter;
            }

            detune_mul *= cents_to_mul(source.drift_value_cents);
        }

        float frequency_hz = clamped_base * source.octave_mul * detune_mul;
        frequency_hz = clampf_local(frequency_hz, MONOB_MIN_FREQ_HZ, MONOB_MAX_FREQ_HZ);

        if(fabsf(frequency_hz - source.current_freq_hz) > MONOB_FREQ_EPSILON_HZ)
        {
            source.osc.SetFreq(frequency_hz);
            source.current_freq_hz = frequency_hz;
        }

        mixed += source.osc.Process() * source.mix;
        mix_sum += source.mix;
    }

    if(mix_sum > 1.0f)
    {
        mixed /= mix_sum;
    }

    return mixed;
}
