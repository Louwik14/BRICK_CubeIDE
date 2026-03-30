#include "Audio/tb3_synth.h"
#include "Storage/memory_layout.h"

#include <math.h>
#include <memory>
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
#include "../../TB-3/rosic_Open303.cpp"

namespace
{
constexpr uint8_t TB3_SYNTH_MAX_INSTANCES = 1U;
constexpr float TB3_MIN_SAMPLE_RATE = 1000.0f;
constexpr float TB3_DEFAULT_SAMPLE_RATE = 48000.0f;

struct tb3_synth_instance_t
{
    alignas(rosic::Open303) uint8_t synth_storage[sizeof(rosic::Open303)];
};

// Open303 carries large internal state (~432 KB per instance):
// - hot voice/runtime state kept in internal RAM (D2)
// - runtime construction in tb3_synth_init (no pre-main C++ object construction)
static SEQ_STATE_D2 tb3_synth_instance_t g_tb3_instances[TB3_SYNTH_MAX_INSTANCES];
static CTRL_STATE float g_tb3_sample_rate = TB3_DEFAULT_SAMPLE_RATE;
static CTRL_STATE uint8_t g_tb3_instance_constructed[TB3_SYNTH_MAX_INSTANCES];
static uint32_t g_tb3_instance_magic[TB3_SYNTH_MAX_INSTANCES];
constexpr uint32_t TB3_INSTANCE_MAGIC = 0x54423331UL; // "TB31"
static uint8_t tb3_synth_instance_valid(uint8_t instance_id);

static tb3_synth_instance_t *tb3_instances(void)
{
    return &g_tb3_instances[0];
}

static rosic::Open303 *tb3_synth_for_instance(uint8_t instance_id)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return nullptr;
    }

    if ((g_tb3_instance_constructed[instance_id] == 0U)
        || (g_tb3_instance_magic[instance_id] != TB3_INSTANCE_MAGIC))
    {
        return nullptr;
    }

    return reinterpret_cast<rosic::Open303 *>(&tb3_instances()[instance_id].synth_storage[0]);
}

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

volatile CTRL_STATE uint32_t g_tb3_debug_stage = 0U;
volatile CTRL_STATE uint32_t g_tb3_debug_init_instance = 0U;
volatile CTRL_STATE uint32_t g_tb3_debug_first_process_seen = 0U;
volatile CTRL_STATE uint32_t g_tb3_debug_first_note_on_seen = 0U;
volatile CTRL_STATE uint32_t g_tb3_debug_last_param_id = 0U;
volatile CTRL_STATE uint32_t g_tb3_debug_last_param_instance = 0U;
volatile CTRL_STATE uint32_t g_tb3_debug_all_notes_off_all_seen = 0U;

void tb3_synth_init(float sample_rate)
{
    g_tb3_debug_stage = 1U; // entered tb3_synth_init
    g_tb3_sample_rate = (sample_rate > TB3_MIN_SAMPLE_RATE) ? sample_rate : TB3_DEFAULT_SAMPLE_RATE;

    for (uint8_t i = 0U; i < TB3_SYNTH_MAX_INSTANCES; ++i)
    {
        rosic::Open303 *const synth_ptr =
            reinterpret_cast<rosic::Open303 *>(&tb3_instances()[i].synth_storage[0]);

        if ((g_tb3_instance_constructed[i] != 0U) && (g_tb3_instance_magic[i] == TB3_INSTANCE_MAGIC))
        {
            synth_ptr->~Open303();
        }

        (void)memset(tb3_instances()[i].synth_storage, 0, sizeof(tb3_instances()[i].synth_storage));
        g_tb3_instance_constructed[i] = 0U;
        g_tb3_instance_magic[i] = 0U;

        std::allocator<rosic::Open303> synth_allocator;
        synth_allocator.construct(synth_ptr);
        g_tb3_instance_constructed[i] = 1U;
        g_tb3_instance_magic[i] = TB3_INSTANCE_MAGIC;

        g_tb3_debug_stage = 4U; // before default param setup
        g_tb3_debug_init_instance = i;
        rosic::Open303 &synth = *synth_ptr;
        synth.setSampleRate((double)g_tb3_sample_rate);
        synth.allNotesOff();
        tb3_apply_default_params(synth);
    }
    g_tb3_debug_stage = 5U; // tb3_synth_init finished
}

uint8_t tb3_synth_instance_count(void)
{
    return TB3_SYNTH_MAX_INSTANCES;
}

void tb3_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    if (g_tb3_debug_first_note_on_seen == 0U)
    {
        g_tb3_debug_first_note_on_seen = 1U;
    }

    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    rosic::Open303 *synth = tb3_synth_for_instance(instance_id);
    if (synth == nullptr)
    {
        return;
    }

    synth->noteOn((int)midi_note, (int)velocity);
}

void tb3_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    rosic::Open303 *synth = tb3_synth_for_instance(instance_id);
    if (synth == nullptr)
    {
        return;
    }

    synth->noteOn((int)midi_note, 0);
}

void tb3_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    rosic::Open303 *synth = tb3_synth_for_instance(instance_id);
    if (synth == nullptr)
    {
        return;
    }

    synth->allNotesOff();
}

void tb3_synth_all_notes_off_all(void)
{
    g_tb3_debug_all_notes_off_all_seen++;
    for (uint8_t i = 0U; i < TB3_SYNTH_MAX_INSTANCES; ++i)
    {
        rosic::Open303 *synth = tb3_synth_for_instance(i);
        if (synth != nullptr)
        {
            synth->allNotesOff();
        }
    }
}

void tb3_synth_set_param_for_instance(uint8_t instance_id, param_id_t param_id, float value)
{
    g_tb3_debug_last_param_id = (uint32_t)param_id;
    g_tb3_debug_last_param_instance = (uint32_t)instance_id;

    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        return;
    }

    rosic::Open303 *synth = tb3_synth_for_instance(instance_id);
    if (synth == nullptr)
    {
        return;
    }

    tb3_apply_param(*synth, param_id, value);
}

void tb3_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames)
{
    if (g_tb3_debug_first_process_seen == 0U)
    {
        g_tb3_debug_first_process_seen = 1U;
    }

    if (mono_out == NULL)
    {
        return;
    }

    if (tb3_synth_instance_valid(instance_id) == 0U)
    {
        (void)memset(mono_out, 0, sizeof(float) * frames);
        return;
    }

    rosic::Open303 *synth = tb3_synth_for_instance(instance_id);
    if (synth == nullptr)
    {
        (void)memset(mono_out, 0, sizeof(float) * frames);
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        mono_out[i] = (float)synth->getSample();
    }
}

} // extern "C"
