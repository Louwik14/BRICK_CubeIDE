#include "Audio/juno_synth.h"
#include "Audio/audio_float.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if __cplusplus < 201703L
namespace std {
template <typename T>
constexpr const T& clamp(const T& value, const T& low, const T& high)
{
    return (value < low) ? low : ((high < value) ? high : value);
}
} // namespace std
#endif

#include "../../Juno/KR106LFO.h"
#include "../../Juno/KR106VcfFreqJ6.h"
#include "../../Juno/KR106Voice.h"

namespace {

constexpr uint32_t JUNO_SYNTH_NUM_VOICES = 4U;
constexpr uint8_t JUNO_TEST_MIDI_NOTE = 48U;
constexpr float JUNO_TEST_VELOCITY = 0.85f;
constexpr float JUNO_PORTAMENTO = 0.22f;

struct JunoVoiceSlot
{
    uint8_t active;
    uint8_t note;
    uint32_t age;
};

struct JunoSynthState
{
    uint8_t initialized;
    uint8_t enabled;
    uint8_t test_mode;
    uint8_t test_note_held;
    uint8_t unison_prepared;
    float sample_rate;
    uint32_t block_size;
    uint32_t voice_age_counter;
    kr106::LFO lfo;
    kr106::Voice<float> voices[JUNO_SYNTH_NUM_VOICES];
    JunoVoiceSlot slots[JUNO_SYNTH_NUM_VOICES];
    float lfo_buffer[AUDIO_BLOCK_SIZE];
    float lfo_raw_buffer[AUDIO_BLOCK_SIZE];
};

static JunoSynthState g_juno;

static float midi_note_to_pitch(uint8_t midi_note)
{
    return (static_cast<float>(midi_note) - 69.0f) * (1.0f / 12.0f);
}

static void juno_voice_apply_patch(kr106::Voice<float>& voice, uint32_t voice_index)
{
    voice.InitVariance(static_cast<int>(voice_index));
    voice.mADSR.mJ6Mode = true;
    voice.mSawOn = true;
    voice.mPulseOn = true;
    voice.mSubOn = true;
    voice.mPwmMode = 0;
    voice.mVcaMode = 0;
    voice.mVcfEnvInvert = 1;

    voice.mDcoPwm = 0.38f;
    voice.mDcoSub = 0.32f;
    voice.mDcoNoise = 0.0f;
    voice.mDcoLfo = kr106::Voice<float>::dcoLfoDepth6(0.08f);

    voice.mVcfFreq = kr106::j6_vcf_freq_from_slider(0.58f);
    voice.mVcfRes = 0.18f;
    voice.mVcfEnv = 0.42f;
    voice.mVcfLfo = kr106::Voice<float>::vcfLfoDepth6(0.06f);
    voice.mVcfKbd = 0.55f;
    voice.mBendDco = 0.0f;
    voice.mBendVcf = 0.0f;
    voice.mBendLfo = 0.0f;
    voice.mRawBend = 0.0f;
    voice.mBenderModAmt = 0.0f;
    voice.mOctTranspose = 0.0f;

    voice.mPortaEnabled = true;
    voice.mPortaRateParam = JUNO_PORTAMENTO;
    voice.mADSR.SetAttackTau(0.010f);
    voice.mADSR.SetDecayTau(0.280f);
    voice.mADSR.SetSustain(0.68f);
    voice.mADSR.SetReleaseTau(0.360f);
    voice.SetSampleRateAndBlockSize(g_juno.sample_rate, static_cast<int>(g_juno.block_size));
}

static void juno_all_notes_off(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.slots[i].active != 0U)
        {
            g_juno.voices[i].Release();
            g_juno.slots[i].active = 0U;
        }
    }

    g_juno.test_note_held = 0U;
}

static uint32_t juno_find_voice_for_note_on(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.voices[i].GetBusy() == false)
            return i;
    }

    uint32_t oldest_index = 0U;
    uint32_t oldest_age = g_juno.slots[0].age;

    for(uint32_t i = 1U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.slots[i].age < oldest_age)
        {
            oldest_age = g_juno.slots[i].age;
            oldest_index = i;
        }
    }

    return oldest_index;
}

static void juno_note_on(uint8_t midi_note, float velocity)
{
    const uint32_t voice_index = juno_find_voice_for_note_on();
    kr106::Voice<float>& voice = g_juno.voices[voice_index];
    JunoVoiceSlot& slot = g_juno.slots[voice_index];

    voice.mPitch = midi_note_to_pitch(midi_note);
    voice.mMidiNote = static_cast<int>(midi_note);
    slot.note = midi_note;
    slot.active = 1U;
    slot.age = ++g_juno.voice_age_counter;
    voice.Trigger(velocity, false);
}

static void juno_refresh_test_note(void)
{
    if((g_juno.enabled == 0U) || (g_juno.test_mode == 0U))
    {
        if(g_juno.test_note_held != 0U)
            juno_all_notes_off();
        return;
    }

    if(g_juno.test_note_held == 0U)
    {
        if(g_juno.unison_prepared != 0U)
        {
            for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
                juno_note_on(JUNO_TEST_MIDI_NOTE, JUNO_TEST_VELOCITY);
        }
        else
        {
            juno_note_on(JUNO_TEST_MIDI_NOTE, JUNO_TEST_VELOCITY);
        }

        g_juno.test_note_held = 1U;
    }
}

static uint8_t juno_any_voice_busy(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.voices[i].GetBusy())
            return 1U;
    }

    return 0U;
}

} // namespace

extern "C" {

void juno_synth_init(float sample_rate, uint32_t block_size)
{
	g_juno = JunoSynthState{};

    g_juno.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    g_juno.block_size = (block_size <= AUDIO_BLOCK_SIZE) ? block_size : AUDIO_BLOCK_SIZE;
    g_juno.enabled = 0U;
    g_juno.test_mode = 0U;
    g_juno.unison_prepared = 0U;

    g_juno.lfo.mJ6Mode = true;
    g_juno.lfo.SetRate(0.18f, g_juno.sample_rate);
    g_juno.lfo.SetDelay(0.0f);
    g_juno.lfo.SetMode(0);
    g_juno.lfo.SetTrigger(false);

    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        juno_voice_apply_patch(g_juno.voices[i], i);
        g_juno.slots[i].active = 0U;
        g_juno.slots[i].note = 0U;
        g_juno.slots[i].age = 0U;
    }

    g_juno.initialized = 1U;
}

void juno_synth_set_enabled(uint8_t enabled)
{
    g_juno.enabled = enabled ? 1U : 0U;

    if(g_juno.enabled == 0U)
        juno_all_notes_off();
}

uint8_t juno_synth_is_enabled(void)
{
    return g_juno.enabled;
}

void juno_synth_set_test_mode(uint8_t enabled)
{
    g_juno.test_mode = enabled ? 1U : 0U;

    if(g_juno.test_mode == 0U)
        juno_all_notes_off();
}

void juno_synth_process_block(float *mono_out, uint32_t frames)
{
    if(mono_out == nullptr)
        return;

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    memset(mono_out, 0, frames * sizeof(float));

    if((g_juno.initialized == 0U) || (g_juno.enabled == 0U) || (frames == 0U))
        return;

    juno_refresh_test_note();

    const uint8_t voice_active = juno_any_voice_busy();
    g_juno.lfo.SetVoiceActive(voice_active != 0U);

    for(uint32_t i = 0U; i < frames; ++i)
    {
        g_juno.lfo_buffer[i] = g_juno.lfo.Process();
        g_juno.lfo_raw_buffer[i] = g_juno.lfo.mLastTri;
    }

    float *inputs[2] = {
        g_juno.lfo_buffer,
        g_juno.lfo_raw_buffer,
    };
    float *outputs[1] = {
        mono_out,
    };

    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if((g_juno.slots[i].active == 0U) && (g_juno.voices[i].GetBusy() == false))
            continue;

        g_juno.voices[i].ProcessSamplesAccumulating(inputs, outputs, 2, 1, 0, static_cast<int>(frames));

        if(g_juno.voices[i].GetBusy() == false)
            g_juno.slots[i].active = 0U;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        if(!std::isfinite(mono_out[i]))
            mono_out[i] = 0.0f;
    }
}

} // extern "C"
