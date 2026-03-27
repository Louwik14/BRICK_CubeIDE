#include "Audio/monob_moog_ladder.h"

#include "Audio/moogladder.h"

namespace
{
constexpr uint8_t MONOB_LADDER_INSTANCE_COUNT = 8U;
constexpr float MONOB_MOOG_LADDER_MIN_FREQ_HZ = 5.0f;
constexpr float MONOB_MOOG_LADDER_MAX_RESONANCE = 1.8f;

infrasonic::MoogLadder g_monob_ladder[MONOB_LADDER_INSTANCE_COUNT];
float g_sample_rate[MONOB_LADDER_INSTANCE_COUNT];
float g_cutoff_hz[MONOB_LADDER_INSTANCE_COUNT];
float g_resonance[MONOB_LADDER_INSTANCE_COUNT];

float clampf_local(float value, float min_value, float max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

float max_cutoff_hz(float sample_rate)
{
    return sample_rate * 0.425f;
}

bool instance_is_valid(uint8_t instance_id)
{
    return (instance_id < MONOB_LADDER_INSTANCE_COUNT);
}

void apply_cached_settings(uint8_t instance_id)
{
    g_monob_ladder[instance_id].SetFreq(clampf_local(g_cutoff_hz[instance_id],
                                                      MONOB_MOOG_LADDER_MIN_FREQ_HZ,
                                                      max_cutoff_hz(g_sample_rate[instance_id])));
    g_monob_ladder[instance_id].SetRes(clampf_local(g_resonance[instance_id], 0.0f, MONOB_MOOG_LADDER_MAX_RESONANCE));
}
}

extern "C" void monob_moog_ladder_init_for_instance(uint8_t instance_id, float sample_rate)
{
    if (!instance_is_valid(instance_id))
    {
        return;
    }

    g_sample_rate[instance_id] = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    g_cutoff_hz[instance_id] = 16000.0f;
    g_resonance[instance_id] = 0.0f;
    g_monob_ladder[instance_id].Init(g_sample_rate[instance_id]);
    apply_cached_settings(instance_id);
}

extern "C" void monob_moog_ladder_init(float sample_rate)
{
    monob_moog_ladder_init_for_instance(0U, sample_rate);
}

extern "C" void monob_moog_ladder_reset_for_instance(uint8_t instance_id)
{
    if (!instance_is_valid(instance_id))
    {
        return;
    }

    g_monob_ladder[instance_id].Init(g_sample_rate[instance_id]);
    apply_cached_settings(instance_id);
}

extern "C" void monob_moog_ladder_reset(void)
{
    monob_moog_ladder_reset_for_instance(0U);
}

extern "C" void monob_moog_ladder_set_cutoff_for_instance(uint8_t instance_id, float cutoff_hz)
{
    if (!instance_is_valid(instance_id))
    {
        return;
    }

    g_cutoff_hz[instance_id] = cutoff_hz;
    g_monob_ladder[instance_id].SetFreq(clampf_local(g_cutoff_hz[instance_id],
                                                      MONOB_MOOG_LADDER_MIN_FREQ_HZ,
                                                      max_cutoff_hz(g_sample_rate[instance_id])));
}

extern "C" void monob_moog_ladder_set_cutoff(float cutoff_hz)
{
    monob_moog_ladder_set_cutoff_for_instance(0U, cutoff_hz);
}

extern "C" void monob_moog_ladder_set_resonance_for_instance(uint8_t instance_id, float resonance)
{
    if (!instance_is_valid(instance_id))
    {
        return;
    }

    g_resonance[instance_id] = clampf_local(resonance, 0.0f, MONOB_MOOG_LADDER_MAX_RESONANCE);
    g_monob_ladder[instance_id].SetRes(g_resonance[instance_id]);
}

extern "C" void monob_moog_ladder_set_resonance(float resonance)
{
    monob_moog_ladder_set_resonance_for_instance(0U, resonance);
}

extern "C" float monob_moog_ladder_process_sample_for_instance(uint8_t instance_id, float input)
{
    if (!instance_is_valid(instance_id))
    {
        return input;
    }

    return g_monob_ladder[instance_id].Process(input);
}

extern "C" float monob_moog_ladder_process_sample(float input)
{
    return monob_moog_ladder_process_sample_for_instance(0U, input);
}
