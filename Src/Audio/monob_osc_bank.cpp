#include "Audio/monob_osc_bank.h"

#include <stdint.h>
#include <string.h>

namespace
{
constexpr uint8_t MONOB_OSC_COUNT = 4U;
constexpr uint8_t MONOB_MAIN_OSC_COUNT = 3U;
constexpr uint8_t MONOB_BANK_INSTANCE_COUNT = 8U;
constexpr float MONOB_MIN_FREQ_HZ = 20.0f;
constexpr float MONOB_MAX_FREQ_HZ = 16000.0f;
constexpr float MONOB_PHASE_SCALE = 4294967296.0f;
constexpr float MONOB_INV_PHASE_SCALE = 1.0f / 4294967296.0f;

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
    uint8_t wave;
    int8_t octave;
    uint8_t detune_index;
    float mix;

    uint32_t phase;
    uint32_t phase_inc;
};

struct MonobOscBank
{
    MonobOscSource sources[MONOB_OSC_COUNT];
    uint8_t active_indices[MONOB_OSC_COUNT];
    uint8_t active_count;
    float sample_rate;
    float inv_sample_rate;
    float cached_base_frequency_hz;
    float output_gain;
};

MonobOscBank g_banks[MONOB_BANK_INSTANCE_COUNT];

constexpr float k_octave_mul[9] = {
    0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f
};

constexpr float k_detune_mul[49] = {
    0.986233f, 0.986803f, 0.987374f, 0.987945f, 0.988517f, 0.989089f, 0.989662f,
    0.990235f, 0.990809f, 0.991383f, 0.991958f, 0.992533f, 0.993109f, 0.993685f,
    0.994262f, 0.994839f, 0.995417f, 0.995995f, 0.996574f, 0.997153f, 0.997733f,
    0.998313f, 0.998894f, 0.999475f, 1.000000f, 1.000578f, 1.001156f, 1.001735f,
    1.002314f, 1.002894f, 1.003474f, 1.004055f, 1.004637f, 1.005219f, 1.005801f,
    1.006384f, 1.006968f, 1.007552f, 1.008137f, 1.008722f, 1.009308f, 1.009894f,
    1.010481f, 1.011069f, 1.011657f, 1.012246f, 1.012835f, 1.013425f, 1.014016f
};

inline MonobOscBank *bank_for(uint8_t instance_id)
{
    if (instance_id >= MONOB_BANK_INSTANCE_COUNT)
    {
        return nullptr;
    }
    return &g_banks[instance_id];
}

inline float clampf_local(float value, float min_value, float max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

inline int octave_to_index(int8_t octave)
{
    if(octave < -4) octave = -4;
    else if(octave > 4) octave = 4;
    return (int)octave + 4;
}

inline uint8_t cents_to_index(float cents)
{
    int icents = (int)(cents >= 0.0f ? (cents + 0.5f) : (cents - 0.5f));
    if(icents < -24) icents = -24;
    else if(icents > 24) icents = 24;
    return (uint8_t)(icents + 24);
}

inline uint32_t freq_to_phase_inc(const MonobOscBank *bank, float frequency_hz)
{
    return (uint32_t)(frequency_hz * bank->inv_sample_rate * MONOB_PHASE_SCALE);
}

inline float compute_source_frequency(const MonobOscSource &source, float base_frequency_hz, uint8_t osc_index)
{
    float frequency_hz = base_frequency_hz * k_octave_mul[octave_to_index(source.octave)];
    if(osc_index < MONOB_MAIN_OSC_COUNT)
    {
        frequency_hz *= k_detune_mul[source.detune_index];
    }
    return clampf_local(frequency_hz, MONOB_MIN_FREQ_HZ, MONOB_MAX_FREQ_HZ);
}

inline float process_saw(uint32_t phase)
{
    const float p = ((float)phase) * MONOB_INV_PHASE_SCALE;
    return 1.0f - (2.0f * p);
}

inline float process_square(uint32_t phase)
{
    return (phase < 0x80000000u) ? 1.0f : -1.0f;
}

inline float process_tri(uint32_t phase)
{
    const float p = ((float)phase) * MONOB_INV_PHASE_SCALE;
    if(p < 0.25f) return p * 4.0f;
    if(p < 0.75f) return 2.0f - (p * 4.0f);
    return (p * 4.0f) - 4.0f;
}

inline float process_wave(uint8_t wave, uint32_t phase)
{
    switch(wave)
    {
        case MONOB_WAVE_SQUARE: return process_square(phase);
        case MONOB_WAVE_TRI:    return process_tri(phase);
        case MONOB_WAVE_SAW:    return process_saw(phase);
        case MONOB_WAVE_SINE:   return process_tri(phase);
        default:                return 0.0f;
    }
}

void refresh_active_sources(MonobOscBank *bank)
{
    uint8_t active_count = 0U;
    float mix_sum = 0.0f;

    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        const MonobOscSource &source = bank->sources[i];
        if((source.wave == MONOB_WAVE_OFF) || (source.mix <= 0.0f))
        {
            continue;
        }

        bank->active_indices[active_count++] = i;
        mix_sum += source.mix;
    }

    bank->active_count = active_count;
    bank->output_gain = (mix_sum > 1.0f) ? (1.0f / mix_sum) : 1.0f;
}

void refresh_phase_increments(MonobOscBank *bank, float base_frequency_hz)
{
    for(uint8_t n = 0U; n < bank->active_count; ++n)
    {
        const uint8_t osc_index = bank->active_indices[n];
        MonobOscSource &source = bank->sources[osc_index];
        source.phase_inc = freq_to_phase_inc(bank, compute_source_frequency(source, base_frequency_hz, osc_index));
    }

    bank->cached_base_frequency_hz = base_frequency_hz;
}
} // namespace

extern "C" void monob_osc_bank_init_for_instance(uint8_t instance_id, float sample_rate)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if (bank == nullptr)
    {
        return;
    }

    memset(bank, 0, sizeof(*bank));
    bank->sample_rate = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    bank->inv_sample_rate = 1.0f / bank->sample_rate;
    bank->cached_base_frequency_hz = -1.0f;
    bank->output_gain = 1.0f;

    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        MonobOscSource &source = bank->sources[i];
        source.wave = MONOB_WAVE_OFF;
        source.octave = (i == 3U) ? -1 : 0;
        source.detune_index = 24U;
        source.mix = 0.0f;
        source.phase = 0U;
        source.phase_inc = 0U;
    }
}

extern "C" void monob_osc_bank_init(float sample_rate)
{
    monob_osc_bank_init_for_instance(0U, sample_rate);
}

extern "C" void monob_osc_bank_reset_for_instance(uint8_t instance_id)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if (bank == nullptr)
    {
        return;
    }

    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        bank->sources[i].phase = 0U;
        bank->sources[i].phase_inc = 0U;
    }

    bank->cached_base_frequency_hz = -1.0f;
}

extern "C" void monob_osc_bank_reset(void)
{
    monob_osc_bank_reset_for_instance(0U);
}

extern "C" void monob_osc_bank_note_on_for_instance(uint8_t instance_id)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if (bank == nullptr)
    {
        return;
    }

    for(uint8_t i = 0U; i < MONOB_OSC_COUNT; ++i)
    {
        bank->sources[i].phase = 0U;
    }

    bank->cached_base_frequency_hz = -1.0f;
}

extern "C" void monob_osc_bank_note_on(void)
{
    monob_osc_bank_note_on_for_instance(0U);
}

extern "C" void monob_osc_bank_set_wave_for_instance(uint8_t instance_id, uint8_t osc_index, uint8_t wave)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if ((bank == nullptr) || (osc_index >= MONOB_OSC_COUNT))
    {
        return;
    }

    MonobOscSource &source = bank->sources[osc_index];
    source.wave = (wave <= MONOB_WAVE_SAW) ? wave : MONOB_WAVE_OFF;
    refresh_active_sources(bank);
}

extern "C" void monob_osc_bank_set_wave(uint8_t osc_index, uint8_t wave)
{
    monob_osc_bank_set_wave_for_instance(0U, osc_index, wave);
}

extern "C" void monob_osc_bank_set_octave_for_instance(uint8_t instance_id, uint8_t osc_index, int8_t octave)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if ((bank == nullptr) || (osc_index >= MONOB_OSC_COUNT))
    {
        return;
    }

    bank->sources[osc_index].octave = octave;
    bank->cached_base_frequency_hz = -1.0f;
}

extern "C" void monob_osc_bank_set_octave(uint8_t osc_index, int8_t octave)
{
    monob_osc_bank_set_octave_for_instance(0U, osc_index, octave);
}

extern "C" void monob_osc_bank_set_detune_for_instance(uint8_t instance_id, uint8_t osc_index, float detune_cents)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if ((bank == nullptr) || (osc_index >= MONOB_MAIN_OSC_COUNT))
    {
        return;
    }

    bank->sources[osc_index].detune_index = cents_to_index(detune_cents);
    bank->cached_base_frequency_hz = -1.0f;
}

extern "C" void monob_osc_bank_set_detune(uint8_t osc_index, float detune_cents)
{
    monob_osc_bank_set_detune_for_instance(0U, osc_index, detune_cents);
}

extern "C" void monob_osc_bank_set_mix_for_instance(uint8_t instance_id, uint8_t osc_index, float mix)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if ((bank == nullptr) || (osc_index >= MONOB_OSC_COUNT))
    {
        return;
    }

    bank->sources[osc_index].mix = clampf_local(mix, 0.0f, 1.0f);
    refresh_active_sources(bank);
}

extern "C" void monob_osc_bank_set_mix(uint8_t osc_index, float mix)
{
    monob_osc_bank_set_mix_for_instance(0U, osc_index, mix);
}

extern "C" float monob_osc_bank_process_for_instance(uint8_t instance_id, float base_frequency_hz)
{
    MonobOscBank *const bank = bank_for(instance_id);
    if ((bank == nullptr) || (bank->active_count == 0U))
    {
        return 0.0f;
    }

    const float clamped_base = clampf_local(base_frequency_hz, MONOB_MIN_FREQ_HZ, MONOB_MAX_FREQ_HZ);
    if(clamped_base != bank->cached_base_frequency_hz)
    {
        refresh_phase_increments(bank, clamped_base);
    }

    float mixed = 0.0f;
    for(uint8_t n = 0U; n < bank->active_count; ++n)
    {
        MonobOscSource &source = bank->sources[bank->active_indices[n]];
        mixed += process_wave(source.wave, source.phase) * source.mix;
        source.phase += source.phase_inc;
    }

    return mixed * bank->output_gain;
}

extern "C" float monob_osc_bank_process(float base_frequency_hz)
{
    return monob_osc_bank_process_for_instance(0U, base_frequency_hz);
}
