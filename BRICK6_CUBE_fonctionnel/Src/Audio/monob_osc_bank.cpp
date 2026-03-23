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
    float detune_cents;
    float mix;
    float drift_phase;
    float drift_rate_hz;
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

float apply_octave(float base_frequency_hz, int8_t octave)
{
    return base_frequency_hz * powf(2.0f, (float)octave);
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
        source.detune_cents = 0.0f;
        source.mix = 0.0f;
        source.drift_phase = 0.0f;
        source.drift_rate_hz = 0.11f + (0.07f * (float)i);
    }
}

extern "C" void monob_osc_bank_reset(void)
{
    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        g_bank.sources[i].osc.Reset();
        g_bank.sources[i].drift_phase = 0.0f;
    }
}

extern "C" void monob_osc_bank_note_on(void)
{
    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        g_bank.sources[i].osc.Reset();
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

    g_bank.sources[osc_index].octave = octave;
}

extern "C" void monob_osc_bank_set_detune(uint8_t osc_index, float detune_cents)
{
    if(osc_index >= MONOB_MAIN_OSC_COUNT)
    {
        return;
    }

    g_bank.sources[osc_index].detune_cents = clampf_local(detune_cents, -24.0f, 24.0f);
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

        float cents = source.detune_cents;
        if(i < MONOB_MAIN_OSC_COUNT)
        {
            source.drift_phase += source.drift_rate_hz / g_bank.sample_rate;
            if(source.drift_phase >= 1.0f)
            {
                source.drift_phase -= floorf(source.drift_phase);
            }
            cents += sinf(source.drift_phase * 6.28318530718f) * drift_depth_cents;
        }

        float frequency_hz = apply_octave(clamped_base, source.octave);
        frequency_hz *= powf(2.0f, cents / 1200.0f);
        frequency_hz = clampf_local(frequency_hz, MONOB_MIN_FREQ_HZ, MONOB_MAX_FREQ_HZ);
        source.osc.SetFreq(frequency_hz);
        mixed += source.osc.Process() * source.mix;
        mix_sum += source.mix;
    }

    if(mix_sum > 1.0f)
    {
        mixed /= mix_sum;
    }

    return mixed;
}
