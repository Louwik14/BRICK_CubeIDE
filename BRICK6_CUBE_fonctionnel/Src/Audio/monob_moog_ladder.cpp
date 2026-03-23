#include "Audio/monob_moog_ladder.h"

#include "Audio/moogladder.h"

namespace
{
constexpr float MONOB_MOOG_LADDER_MIN_FREQ_HZ = 5.0f;
constexpr float MONOB_MOOG_LADDER_MAX_RESONANCE = 1.8f;

infrasonic::MoogLadder g_monob_ladder;
float g_sample_rate = 48000.0f;
float g_cutoff_hz = 16000.0f;
float g_resonance = 0.0f;

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

float max_cutoff_hz(void)
{
    return g_sample_rate * 0.425f;
}

void apply_cached_settings(void)
{
    g_monob_ladder.SetFreq(clampf_local(g_cutoff_hz, MONOB_MOOG_LADDER_MIN_FREQ_HZ, max_cutoff_hz()));
    g_monob_ladder.SetRes(clampf_local(g_resonance, 0.0f, MONOB_MOOG_LADDER_MAX_RESONANCE));
}
}

extern "C" void monob_moog_ladder_init(float sample_rate)
{
    g_sample_rate = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    g_monob_ladder.Init(g_sample_rate);
    apply_cached_settings();
}

extern "C" void monob_moog_ladder_reset(void)
{
    g_monob_ladder.Init(g_sample_rate);
    apply_cached_settings();
}

extern "C" void monob_moog_ladder_set_cutoff(float cutoff_hz)
{
    g_cutoff_hz = cutoff_hz;
    g_monob_ladder.SetFreq(clampf_local(g_cutoff_hz, MONOB_MOOG_LADDER_MIN_FREQ_HZ, max_cutoff_hz()));
}

extern "C" void monob_moog_ladder_set_resonance(float resonance)
{
    g_resonance = clampf_local(resonance, 0.0f, MONOB_MOOG_LADDER_MAX_RESONANCE);
    g_monob_ladder.SetRes(g_resonance);
}

extern "C" float monob_moog_ladder_process_sample(float input)
{
    return g_monob_ladder.Process(input);
}
