#include "Audio/tb3_synth.h"
#include "Storage/memory_layout.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "../../TB-3/rosic_Open303.h"

// Build-system note:
// CubeIDE currently compiles this translation unit for TB-3 runtime glue. The Open303 sources are
// pulled in here to keep the existing integration unchanged (no build graph refactor in this pass).
#include "../../TB-3/GlobalFunctions.cpp"
#include "../../TB-3/rosic_Complex.cpp"
#include "../../TB-3/rosic_FourierTransformerRadix2.cpp"
#include "../../TB-3/rosic_MipMappedWaveTable.cpp"
#include "../../TB-3/rosic_BlendOscillator.cpp"
#include "../../TB-3/rosic_BiquadFilter.cpp"
#include "../../TB-3/rosic_OnePoleFilter.cpp"
#include "../../TB-3/rosic_TeeBeeFilter.cpp"
#include "../../TB-3/rosic_AnalogEnvelope.cpp"
#include "../../TB-3/rosic_DecayEnvelope.cpp"
#include "../../TB-3/rosic_LeakyIntegrator.cpp"
#include "../../TB-3/rosic_EllipticQuarterBandFilter.cpp"
#include "../../TB-3/rosic_MidiNoteEvent.cpp"
#include "../../TB-3/rosic_AcidPattern.cpp"
#include "../../TB-3/rosic_AcidSequencer.cpp"
#include "../../TB-3/rosic_Open303.cpp"

namespace
{
constexpr uint8_t TB3_SYNTH_MAX_INSTANCES = 1U;
constexpr float TB3_MIN_SAMPLE_RATE = 1000.0f;
constexpr float TB3_DEFAULT_SAMPLE_RATE = 48000.0f;

struct tb3_synth_instance_t
{
    rosic::Open303 synth;
};

// Open303 carries large internal state (~432 KB per instance).
// 8 instances would overflow internal SRAM domains; keep them in SDRAM cold pool.
static AUDIO_COLD_SDRAM tb3_synth_instance_t g_tb3_instances[TB3_SYNTH_MAX_INSTANCES];
static float g_tb3_sample_rate = TB3_DEFAULT_SAMPLE_RATE;

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline double lin_map(float v, float in_lo, float in_hi, double out_lo, double out_hi)
{
    const float t = clampf((v - in_lo) / (in_hi - in_lo), 0.0f, 1.0f);
    return out_lo + (out_hi - out_lo) * (double)t;
}

static uint8_t tb3_synth_instance_valid(uint8_t instance_id)
{
    return (instance_id < TB3_SYNTH_MAX_INSTANCES) ? 1U : 0U;
}

static void tb3_apply_default_params(rosic::Open303 &synth)
{
    synth.setWaveform(0.0);
    synth.setCutoff(1000.0);
    synth.setResonance(0.0);
    synth.setEnvMod(25.0);
    synth.setDecay(1000.0);
    synth.setAccent(0.0);
    synth.setVolume(-12.0);
    synth.setSlideTime(60.0);
}

static void tb3_apply_param(rosic::Open303 &synth, param_id_t param_id, float value)
{
    switch (param_id)
    {
        case PARAM_TB3_WAVEFORM:
            // Keep V1 semantics (enum-like 0/1), but still pass a continuous range to Open303.
            synth.setWaveform(clampf(value, 0.0f, 1.0f));
            break;
        case PARAM_TB3_CUTOFF:
            // 303 nominal range measured in Open303 internals is roughly 313..2394 Hz.
            synth.setCutoff(lin_map(value, 0.0f, 127.0f, 313.815, 2394.412));
            break;
        case PARAM_TB3_RESONANCE:
            synth.setResonance(lin_map(value, 0.0f, 127.0f, 0.0, 100.0));
            break;
        case PARAM_TB3_ENV_MOD:
            synth.setEnvMod(lin_map(value, 0.0f, 127.0f, 0.0, 100.0));
            break;
        case PARAM_TB3_DECAY:
            synth.setDecay(lin_map(value, 0.0f, 127.0f, 200.0, 2000.0));
            break;
        case PARAM_TB3_ACCENT:
            synth.setAccent(lin_map(value, 0.0f, 127.0f, 0.0, 100.0));
            break;
        case PARAM_TB3_VOLUME:
            // Open303 expects dB value.
            synth.setVolume(lin_map(value, 0.0f, 127.0f, -36.0, 6.0));
            break;
        case PARAM_TB3_SLIDE_TIME:
            synth.setSlideTime(lin_map(value, 0.0f, 127.0f, 0.0, 120.0));
            break;
        default:
            break;
    }
}
}

extern "C" {

void tb3_synth_init(float sample_rate)
{
    g_tb3_sample_rate = (sample_rate > TB3_MIN_SAMPLE_RATE) ? sample_rate : TB3_DEFAULT_SAMPLE_RATE;

    for (uint8_t i = 0U; i < TB3_SYNTH_MAX_INSTANCES; ++i)
    {
        g_tb3_instances[i].synth.setSampleRate((double)g_tb3_sample_rate);
        g_tb3_instances[i].synth.allNotesOff();
        tb3_apply_default_params(g_tb3_instances[i].synth);
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

    g_tb3_instances[instance_id].synth.noteOn((int)midi_note, (int)velocity);
}

void tb3_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    g_tb3_instances[instance_id].synth.noteOn((int)midi_note, 0);
}

void tb3_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    g_tb3_instances[instance_id].synth.allNotesOff();
}

void tb3_synth_all_notes_off_all(void)
{
    for (uint8_t i = 0U; i < TB3_SYNTH_MAX_INSTANCES; ++i)
    {
        g_tb3_instances[i].synth.allNotesOff();
    }
}

void tb3_synth_set_param_for_instance(uint8_t instance_id, param_id_t param_id, float value)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    tb3_apply_param(g_tb3_instances[instance_id].synth, param_id, value);
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

    rosic::Open303 &synth = g_tb3_instances[instance_id].synth;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        mono_out[i] = (float)synth.getSample();
    }
}

} // extern "C"
